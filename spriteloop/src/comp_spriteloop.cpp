#include "spla_defold.h"
#include "res_spriteloop.h"

#include <dmsdk/dlib/configfile_gen.hpp>
#include <dmsdk/dlib/hash.h>
#include <dmsdk/dlib/log.h>
#include <dmsdk/gameobject/component.h>
#include <dmsdk/render/render.h>
#include <dmsdk/resource/resource.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

// Production Defold component type for SpriteLoop.
//
// The component loads a .spla package referenced by a compiled .spriteloopc resource, advances
// its player during update, and submits geometry through Defold's component render-list APIs using
// Defold-managed buffers, textures, and materials.
namespace spla_defold {

namespace {

constexpr const char* max_count_property = "spriteloop.max_count";
const dmhash_t position_property = dmHashString64("position");
const dmhash_t rotation_property = dmHashString64("rotation");
const dmhash_t scale_property = dmHashString64("scale");

// Vertex layout matching /spriteloop/spriteloop/materials/spriteloop.material.
struct SpriteLoopVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

// Per-component geometry counts collected before Defold render-list allocation.
struct SpriteLoopGeometryCounts {
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    uint32_t render_object_count = 0;
};

// Shared component-type state created once when the component type is registered.
struct SpriteLoopContext {
    dmResource::HFactory factory = 0;
    dmGraphics::HContext graphics_context = 0;
    dmRender::HRenderContext render_context = 0;
    uint32_t max_components_per_world = 1024;
};

// Per-collection storage for this component type.
// Defold creates one such world for each runtime collection context that can contain SpriteLoop
// components; it stores the live components and transient render buffers for that context.
struct SpriteLoopWorld {
    SpriteLoopContext* context = nullptr;
    std::vector<SplaDefoldComponent*> components;
    std::vector<SpriteLoopGeometryCounts> geometry_counts;
    std::vector<dmRender::RenderObject> render_objects;
    std::vector<SpriteLoopVertex> vertex_data;
    std::vector<uint32_t> index_data;
    dmGraphics::HVertexDeclaration vertex_declaration = 0;
    dmGraphics::HVertexBuffer vertex_buffer = 0;
    dmGraphics::HIndexBuffer index_buffer = 0;
};

constexpr float pi = 3.14159265358979323846f;

// Returns value when it is a non-empty string, otherwise fallback.
const char* non_empty_or_default(const char* value, const char* fallback)
{
    return value != nullptr && value[0] != '\0' ? value : fallback;
}

// Removes a component pointer from a world without deleting the component itself.
void remove_component(SpriteLoopWorld* world, SplaDefoldComponent* component)
{
    if (world == nullptr || component == nullptr) {
        return;
    }

    world->components.erase(std::remove(world->components.begin(), world->components.end(),
                                        component),
                            world->components.end());
}

// Converts Defold component user data back into the SpriteLoop component pointer.
SplaDefoldComponent* component_from_userdata(uintptr_t user_data)
{
    return reinterpret_cast<SplaDefoldComponent*>(user_data);
}

// Converts a Defold property variable to Vector3.
dmVMath::Vector3 property_var_to_vector3(const dmGameObject::PropertyVar& property)
{
    return dmVMath::Vector3(property.m_V4[0], property.m_V4[1], property.m_V4[2]);
}

// Converts a Defold property variable to Point3.
dmVMath::Point3 property_var_to_point3(const dmGameObject::PropertyVar& property)
{
    return dmVMath::Point3(property.m_V4[0], property.m_V4[1], property.m_V4[2]);
}

// Converts a Defold property variable to Quat.
dmVMath::Quat property_var_to_quat(const dmGameObject::PropertyVar& property)
{
    return dmVMath::Quat(property.m_V4[0], property.m_V4[1], property.m_V4[2],
                         property.m_V4[3]);
}

// Converts degrees to radians for SpriteLoop manifest rotation values.
float degrees_to_radians(float degrees)
{
    return degrees * pi / 180.0f;
}

// Rotates v by quaternion q without relying on engine helper overloads.
dmVMath::Vector3 rotate_vector(const dmVMath::Quat& q, const dmVMath::Vector3& v)
{
    const dmVMath::Vector3 quat_vector(q.getX(), q.getY(), q.getZ());
    const dmVMath::Vector3 uv = dmVMath::Cross(quat_vector, v);
    const dmVMath::Vector3 uuv = dmVMath::Cross(quat_vector, uv);
    return v + ((uv * q.getW()) + uuv) * 2.0f;
}

// Applies the component-local transform to a centered SpriteLoop point.
// x and y are local canvas coordinates relative to the component origin.
dmVMath::Vector4 transform_component_local_point(const SplaDefoldInstance& instance,
                                                 float x,
                                                 float y)
{
    const dmVMath::Vector3 scaled(x * instance.local_scale.getX(),
                                  y * instance.local_scale.getY(), 0.0f);
    const dmVMath::Vector3 rotated = rotate_vector(instance.local_rotation, scaled);
    return dmVMath::Vector4(rotated.getX() + instance.local_position.getX(),
                            rotated.getY() + instance.local_position.getY(),
                            rotated.getZ() + instance.local_position.getZ(), 1.0f);
}

// Transforms one source image pixel coordinate into one world-space render vertex.
// source_x/source_y are part image coordinates, texture_width/height normalize UVs, part supplies
// the pivot, transform supplies the frame transform, and instance supplies component state.
SpriteLoopVertex transform_vertex(float source_x,
                                  float source_y,
                                  float texture_width,
                                  float texture_height,
                                  const spriteloop::SplaAtlasUvRect& uv_rect,
                                  const spriteloop::SplaPart& part,
                                  const spriteloop::SplaTransform& transform,
                                  const SplaDefoldInstance& instance)
{
    const spriteloop::SplaPackage& package = instance_package(instance);
    const float scale_x = transform.scale_x * instance.scale_x;
    const float scale_y = transform.scale_y * instance.scale_y;
    const float local_x = (source_x - part.pivot.x) * scale_x;
    const float local_y = (part.pivot.y - source_y) * scale_y;
    const float radians = degrees_to_radians(-transform.rotation_degrees);
    const float cos_r = std::cos(radians);
    const float sin_r = std::sin(radians);
    const float x = local_x * cos_r - local_y * sin_r;
    const float y = local_x * sin_r + local_y * cos_r;
    const float local_spla_x = transform.x * instance.scale_x + x;
    const float local_spla_y =
        (static_cast<float>(package.canvas_height) - transform.y) *
            instance.scale_y +
        y;

    const dmVMath::Matrix4& world = dmGameObject::GetWorldMatrix(instance.game_object);
    const float centered_x =
        local_spla_x - static_cast<float>(package.canvas_width) * 0.5f;
    const float centered_y =
        local_spla_y - static_cast<float>(package.canvas_height) * 0.5f;
    const dmVMath::Vector4 local =
        transform_component_local_point(instance, centered_x, centered_y);
    const dmVMath::Vector4 transformed = world * local;

    SpriteLoopVertex vertex;
    vertex.x = transformed.getX();
    vertex.y = transformed.getY();
    vertex.z = transformed.getZ();
    const float source_u = source_x / texture_width;
    const float source_v = source_y / texture_height;
    vertex.u = uv_rect.u0 + (uv_rect.u1 - uv_rect.u0) * source_u;
    vertex.v = uv_rect.v0 + (uv_rect.v1 - uv_rect.v0) * source_v;
    vertex.a = std::max(0.0f, std::min(transform.opacity, 1.0f));
    return vertex;
}

// Builds the four vertices for one image part quad.
// vertices receives corners in top-left, top-right, bottom-right, bottom-left order.
void build_quad(const SplaDefoldInstance& instance,
                const spriteloop::SplaPart& part,
                const spriteloop::SplaTransform& transform,
                const SplaDefoldImageResource& image,
                SpriteLoopVertex (&vertices)[4])
{
    const float width = static_cast<float>(image.width);
    const float height = static_cast<float>(image.height);
    const spriteloop::SplaAtlasUvRect& uv = image.atlas_region.uv;
    vertices[0] = transform_vertex(0.0f, 0.0f, width, height, uv, part, transform, instance);
    vertices[1] = transform_vertex(width, 0.0f, width, height, uv, part, transform, instance);
    vertices[2] = transform_vertex(width, height, width, height, uv, part, transform, instance);
    vertices[3] = transform_vertex(0.0f, height, width, height, uv, part, transform, instance);
}

// Returns true when a component has enough state to emit geometry this frame.
bool component_should_render(const SplaDefoldComponent* component)
{
    return component != nullptr && component->visible && component->instance != nullptr &&
           component->instance->visible && component->instance->player != nullptr &&
           component->instance->game_object != 0 && component->instance->player->current_frame() != nullptr;
}

// Returns the same component-local origin position used by emitted vertices so Defold's render
// list can sort components that share one parent game object by their component-local Z.
dmVMath::Point3 component_render_sort_position(const SplaDefoldComponent* component)
{
    if (component == nullptr || component->instance == nullptr) {
        return dmVMath::Point3(0.0f, 0.0f, 0.0f);
    }

    const SplaDefoldInstance& instance = *component->instance;
    const dmVMath::Matrix4& world = dmGameObject::GetWorldMatrix(component->game_object);
    const dmVMath::Vector4 local_origin(instance.local_position.getX(),
                                        instance.local_position.getY(),
                                        instance.local_position.getZ(), 1.0f);
    const dmVMath::Vector4 position = world * local_origin;
    return dmVMath::Point3(position.getX(), position.getY(), position.getZ());
}

// Counts vertices, indices, and render objects needed for one component's current frame.
// counts is reset before being filled.
void count_component_geometry(const SplaDefoldComponent* component,
                              SpriteLoopGeometryCounts& counts)
{
    counts = {};
    if (!component_should_render(component)) {
        return;
    }

    const SplaDefoldInstance& instance = *component->instance;
    const spriteloop::SplaPackage& package = instance_package(instance);
    const std::vector<SplaDefoldImageResource>& image_resources =
        instance_image_resources(instance);
    const spriteloop::SplaFrame* frame = instance.player->current_frame();
    for (const spriteloop::SplaFramePart& frame_part : frame->parts) {
        if (frame_part.part_index < 0 ||
            frame_part.part_index >= static_cast<int>(package.parts.size())) {
            continue;
        }
        const std::size_t part_index = static_cast<std::size_t>(frame_part.part_index);
        if (part_index >= image_resources.size() ||
            instance_atlas_texture(instance) == 0) {
            continue;
        }
        counts.vertex_count += 4;
        counts.index_count += 6;
    }
    counts.render_object_count = counts.index_count > 0 ? 1 : 0;
}

// Fills one Defold render object and submits it to the render context.
// index_start/index_count select the part quad inside the shared index buffer.
void fill_render_object(SpriteLoopWorld* world,
                        dmRender::HRenderContext render_context,
                        dmRender::RenderObject& render_object,
                        dmGraphics::HTexture texture,
                        dmRender::HMaterial material,
                        uint32_t index_start,
                        uint32_t index_count)
{
    render_object.Init();
    render_object.m_VertexDeclaration = world->vertex_declaration;
    render_object.m_VertexBuffer = world->vertex_buffer;
    render_object.m_IndexBuffer = world->index_buffer;
    render_object.m_IndexType = dmGraphics::TYPE_UNSIGNED_INT;
    render_object.m_PrimitiveType = dmGraphics::PRIMITIVE_TRIANGLES;
    render_object.m_VertexStart = index_start * sizeof(uint32_t);
    render_object.m_VertexCount = index_count;
    render_object.m_Textures[0] = texture;
    render_object.m_Material = material;
    render_object.m_SetBlendFactors = 1;
    render_object.m_SourceBlendFactor = dmGraphics::BLEND_FACTOR_SRC_ALPHA;
    render_object.m_DestinationBlendFactor = dmGraphics::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    dmRender::AddToRender(render_context, &render_object);
}

// Appends vertices/indices for one visible component into the world's transient render buffers.
// Returns the number of indices appended for this component.
uint32_t append_component_geometry(SpriteLoopWorld* world,
                                   const SplaDefoldComponent* component)
{
    if (!component_should_render(component)) {
        return 0;
    }

    const SplaDefoldInstance& instance = *component->instance;
    const spriteloop::SplaPackage& package = instance_package(instance);
    const std::vector<SplaDefoldImageResource>& image_resources =
        instance_image_resources(instance);
    const spriteloop::SplaFrame* frame = instance.player->current_frame();
    dmGraphics::HTexture atlas_texture = instance_atlas_texture(instance);
    if (atlas_texture == 0) {
        return 0;
    }
    uint32_t component_index_count = 0;

    for (const spriteloop::SplaFramePart& frame_part : frame->parts) {
        if (frame_part.part_index < 0 ||
            frame_part.part_index >= static_cast<int>(package.parts.size())) {
            continue;
        }

        const std::size_t part_index = static_cast<std::size_t>(frame_part.part_index);
        if (part_index >= image_resources.size()) {
            continue;
        }

        const SplaDefoldImageResource& image = image_resources[part_index];
        SpriteLoopVertex vertices[4];
        build_quad(instance, package.parts[part_index], frame_part.transform, image,
                   vertices);

        const uint32_t base_vertex = static_cast<uint32_t>(world->vertex_data.size());
        world->vertex_data.insert(world->vertex_data.end(), vertices, vertices + 4);
        world->index_data.push_back(base_vertex + 0);
        world->index_data.push_back(base_vertex + 1);
        world->index_data.push_back(base_vertex + 2);
        world->index_data.push_back(base_vertex + 0);
        world->index_data.push_back(base_vertex + 2);
        world->index_data.push_back(base_vertex + 3);
        component_index_count += 6;
    }

    return component_index_count;
}

// Defold component render-list dispatch callback.
// The render list is Defold's per-frame queue of things to draw. BEGIN clears transient data,
// BATCH emits render objects for requested components, and END uploads the shared vertex/index
// buffers used by all submitted render objects.
void render_list_dispatch(const dmRender::RenderListDispatchParams& params)
{
    SpriteLoopWorld* world = static_cast<SpriteLoopWorld*>(params.m_UserData);
    switch (params.m_Operation) {
    case dmRender::RENDER_LIST_OPERATION_BEGIN:
        world->render_objects.clear();
        world->vertex_data.clear();
        world->index_data.clear();
        break;
    case dmRender::RENDER_LIST_OPERATION_BATCH:
    {
        dmRender::HMaterial current_material = 0;
        dmGraphics::HTexture current_texture = 0;
        uint32_t batch_index_start = static_cast<uint32_t>(world->index_data.size());
        uint32_t batch_index_count = 0;

        auto flush_batch = [&]() {
            if (batch_index_count == 0 || current_texture == 0 || current_material == 0) {
                return;
            }

            world->render_objects.emplace_back();
            fill_render_object(world, params.m_Context, world->render_objects.back(),
                               current_texture, current_material, batch_index_start,
                               batch_index_count);
            batch_index_start = static_cast<uint32_t>(world->index_data.size());
            batch_index_count = 0;
        };

        for (uint32_t* i = params.m_Begin; i != params.m_End; ++i) {
            const uint32_t component_index = static_cast<uint32_t>(params.m_Buf[*i].m_UserData);
            if (component_index < world->components.size()) {
                const SplaDefoldComponent* component = world->components[component_index];
                if (!component_should_render(component)) {
                    continue;
                }

                dmRender::HMaterial material = component->resource->material->m_Material;
                dmGraphics::HTexture texture = instance_atlas_texture(*component->instance);
                if (texture == 0 || material == 0) {
                    continue;
                }

                if (batch_index_count > 0 &&
                    (texture != current_texture || material != current_material)) {
                    flush_batch();
                }

                if (batch_index_count == 0) {
                    current_texture = texture;
                    current_material = material;
                    batch_index_start = static_cast<uint32_t>(world->index_data.size());
                }

                batch_index_count += append_component_geometry(world, component);
            }
        }
        flush_batch();
        break;
    }
    case dmRender::RENDER_LIST_OPERATION_END:
        if (!world->vertex_data.empty() && !world->index_data.empty()) {
            dmGraphics::SetVertexBufferData(
                world->vertex_buffer,
                static_cast<uint32_t>(world->vertex_data.size() * sizeof(SpriteLoopVertex)),
                world->vertex_data.data(), dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
            dmGraphics::SetIndexBufferData(
                world->index_buffer,
                static_cast<uint32_t>(world->index_data.size() * sizeof(uint32_t)),
                world->index_data.data(), dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
        }
        break;
    }
}

// Copies transform properties supplied by Defold component creation/property updates.
// property_set is Defold's callback bundle for reading position, rotation, and scale.
void apply_component_property_set(SplaDefoldInstance* instance,
                                  const dmGameObject::PropertySet& property_set)
{
    if (instance == nullptr || property_set.m_GetPropertyCallback == nullptr) {
        return;
    }

    dmGameObject::PropertyVar property;
    if (property_set.m_GetPropertyCallback(nullptr, property_set.m_UserData,
                                           position_property, property) ==
            dmGameObject::PROPERTY_RESULT_OK &&
        property.m_Type == dmGameObject::PROPERTY_TYPE_VECTOR3) {
        instance->local_position = property_var_to_point3(property);
    }

    if (property_set.m_GetPropertyCallback(nullptr, property_set.m_UserData,
                                           rotation_property, property) ==
            dmGameObject::PROPERTY_RESULT_OK &&
        property.m_Type == dmGameObject::PROPERTY_TYPE_QUAT) {
        instance->local_rotation = property_var_to_quat(property);
    }

    if (property_set.m_GetPropertyCallback(nullptr, property_set.m_UserData,
                                           scale_property, property) ==
            dmGameObject::PROPERTY_RESULT_OK &&
        property.m_Type == dmGameObject::PROPERTY_TYPE_VECTOR3) {
        instance->local_scale = property_var_to_vector3(property);
    }
}

// Loads the .spla package configured on a component and creates its runtime instance.
// context supplies the resource factory, component receives the instance, and package_path is the
// project resource path stored in the .spriteloopc DDF.
bool load_component_package(SpriteLoopContext* context,
                            SplaDefoldComponent* component,
                            const char* package_path)
{
    SplaDefoldSharedPackageResource* shared_resource =
        retain_shared_package_resource(package_path);
    if (shared_resource != nullptr) {
        component->instance = create_instance_from_shared_resource(shared_resource);
        if (component->instance != nullptr) {
            component->instance->game_object = component->game_object;
            component->instance->visible = component->visible;
            return true;
        }
        return false;
    }

    SplaPackageBytesResource* package_resource = nullptr;
    dmResource::Result resource_result =
        dmResource::Get(context->factory, package_path, reinterpret_cast<void**>(&package_resource));
    if (resource_result != dmResource::RESULT_OK || package_resource == nullptr ||
        package_resource->bytes.empty()) {
        dmLogError("Could not load SpriteLoop package resource '%s' for component resource '%s'",
                   package_path,
                   component->resource != nullptr ? component->resource->path.c_str()
                                                  : "<unknown>");
        return false;
    }

    std::string error;
    shared_resource = acquire_shared_package_resource(
        package_path, package_resource->bytes.data(), package_resource->bytes.size(), error);
    dmResource::Release(context->factory, package_resource);
    component->instance = create_instance_from_shared_resource(shared_resource);
    if (component->instance == nullptr) {
        dmLogError("Could not create SpriteLoop component instance from package '%s' "
                   "for component resource '%s': %s",
                   package_path,
                   component->resource != nullptr ? component->resource->path.c_str()
                                                  : "<unknown>",
                   error.c_str());
        return false;
    }

    component->instance->game_object = component->game_object;
    component->instance->visible = component->visible;
    return true;
}

// Component callback: creates SpriteLoop's per-collection storage.
// params supplies component type context and max instance count; params.m_World receives the
// SpriteLoopWorld pointer that will hold all SpriteLoop components in this collection context.
dmGameObject::CreateResult component_new_world(
    const dmGameObject::ComponentNewWorldParams& params)
{
    SpriteLoopWorld* world = new SpriteLoopWorld;
    world->context = static_cast<SpriteLoopContext*>(params.m_Context);
    world->components.reserve(params.m_MaxComponentInstances == 0xFFFFFFFF
                                  ? world->context->max_components_per_world
                                  : params.m_MaxComponentInstances);
    world->geometry_counts.reserve(world->components.capacity());

    dmGraphics::HVertexStreamDeclaration stream_declaration =
        dmGraphics::NewVertexStreamDeclaration(world->context->graphics_context);
    dmGraphics::AddVertexStream(stream_declaration, "position", 3, dmGraphics::TYPE_FLOAT,
                                false);
    dmGraphics::AddVertexStream(stream_declaration, "texcoord0", 2, dmGraphics::TYPE_FLOAT,
                                false);
    dmGraphics::AddVertexStream(stream_declaration, "color", 4, dmGraphics::TYPE_FLOAT, false);
    world->vertex_declaration =
        dmGraphics::NewVertexDeclaration(world->context->graphics_context, stream_declaration);
    world->vertex_buffer = dmGraphics::NewVertexBuffer(
        world->context->graphics_context, 0, 0, dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
    world->index_buffer = dmGraphics::NewIndexBuffer(
        world->context->graphics_context, 0, 0, dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
    dmGraphics::DeleteVertexStreamDeclaration(stream_declaration);

    *params.m_World = world;
    return dmGameObject::CREATE_RESULT_OK;
}

// Component callback: destroys a collection world and its graphics buffers.
dmGameObject::CreateResult component_delete_world(
    const dmGameObject::ComponentDeleteWorldParams& params)
{
    SpriteLoopWorld* world = static_cast<SpriteLoopWorld*>(params.m_World);
    dmGraphics::DeleteVertexDeclaration(world->vertex_declaration);
    dmGraphics::DeleteVertexBuffer(world->vertex_buffer);
    dmGraphics::DeleteIndexBuffer(world->index_buffer);
    delete world;
    return dmGameObject::CREATE_RESULT_OK;
}

// Component callback: creates one component instance from a compiled .spriteloopc resource.
// params provides the game object instance, resource data, initial transform, and property set.
dmGameObject::CreateResult component_create(const dmGameObject::ComponentCreateParams& params)
{
    SpriteLoopWorld* world = static_cast<SpriteLoopWorld*>(params.m_World);
    SpriteLoopContext* context = static_cast<SpriteLoopContext*>(params.m_Context);
    SpriteLoopResource* resource = static_cast<SpriteLoopResource*>(params.m_Resource);
    if (world->components.size() >= context->max_components_per_world) {
        dmLogError("SpriteLoop component limit reached (%u). See '%s' in game.project",
                   context->max_components_per_world, max_count_property);
        return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
    }
    if (resource == nullptr || resource->ddf == nullptr) {
        dmLogError("SpriteLoop component resource is missing");
        return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
    }

    const char* package_path = resource->ddf->m_Package;
    if (package_path == nullptr || package_path[0] == '\0') {
        dmLogError("SpriteLoop component resource '%s' has no package path",
                   resource->path.empty() ? "<unknown>" : resource->path.c_str());
        return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
    }

    SplaDefoldComponent* component = new SplaDefoldComponent;
    component->resource = resource;
    component->game_object = params.m_Instance;
    component->package_path = package_path;
    component->default_animation =
        non_empty_or_default(resource->ddf->m_DefaultAnimation, "idle");
    component->playback_rate = resource->ddf->m_PlaybackRate;
    component->loop = resource->ddf->m_Loop;
    component->visible = resource->ddf->m_Visible;
    component->autoplay = resource->ddf->m_Autoplay;

    if (load_component_package(context, component, package_path)) {
        component->instance->player->set_loop_override(component->loop);
        component->instance->local_position = params.m_Position;
        component->instance->local_rotation = params.m_Rotation;
        component->instance->local_scale = params.m_Scale;
        apply_component_property_set(component->instance, params.m_PropertySet);
    } else {
        component->visible = false;
        dmLogError("SpriteLoop component for package '%s' will be disabled",
                   package_path);
    }

    world->components.push_back(component);
    world->geometry_counts.resize(world->components.size());
    *params.m_UserData = reinterpret_cast<uintptr_t>(component);
    return dmGameObject::CREATE_RESULT_OK;
}

// Component callback: starts the configured default animation after creation when autoplay is on.
dmGameObject::CreateResult component_init(const dmGameObject::ComponentInitParams& params)
{
    SplaDefoldComponent* component = component_from_userdata(*params.m_UserData);
    if (component != nullptr && component->instance != nullptr && component->autoplay &&
        !component->default_animation.empty()) {
        const bool played =
            component->instance->player->play(component->default_animation.c_str());
        if (!played) {
            dmLogWarning("SpriteLoop default animation '%s' was not found in '%s'",
                         component->default_animation.c_str(),
                         component->package_path.c_str());
        }
    }
    return dmGameObject::CREATE_RESULT_OK;
}

// Component callback: destroys one component instance and its package instance.
dmGameObject::CreateResult component_destroy(const dmGameObject::ComponentDestroyParams& params)
{
    SpriteLoopWorld* world = static_cast<SpriteLoopWorld*>(params.m_World);
    SplaDefoldComponent* component = component_from_userdata(*params.m_UserData);
    if (component != nullptr) {
        remove_component(world, component);
        world->geometry_counts.resize(world->components.size());
        destroy_instance(component->instance, dmGraphics::GetInstalledContext());
        component->instance = nullptr;
        delete component;
        *params.m_UserData = 0;
    }
    return dmGameObject::CREATE_RESULT_OK;
}

// Component callback: opts SpriteLoop into Defold's update phase.
// The actual per-frame animation advance happens in component_update.
dmGameObject::CreateResult component_add_to_update(
    const dmGameObject::ComponentAddToUpdateParams& params)
{
    (void)params;
    return dmGameObject::CREATE_RESULT_OK;
}

// Component callback: refreshes component-local transform values when Defold sets properties.
dmGameObject::PropertyResult component_set_properties(
    const dmGameObject::ComponentSetPropertiesParams& params)
{
    SplaDefoldComponent* component = component_from_userdata(*params.m_UserData);
    if (component == nullptr || component->instance == nullptr) {
        return dmGameObject::PROPERTY_RESULT_INVALID_INSTANCE;
    }

    apply_component_property_set(component->instance, params.m_PropertySet);
    return dmGameObject::PROPERTY_RESULT_OK;
}

// Component callback: advances playback for every SpriteLoop component stored in this
// per-collection SpriteLoopWorld.
// params carries delta time and result reports whether component transforms changed.
dmGameObject::UpdateResult component_update(
    const dmGameObject::ComponentsUpdateParams& params,
    dmGameObject::ComponentsUpdateResult& result)
{
    SpriteLoopWorld* world = static_cast<SpriteLoopWorld*>(params.m_World);
    const float dt = params.m_UpdateContext != nullptr ? params.m_UpdateContext->m_DT : 0.0f;
    for (SplaDefoldComponent* component : world->components) {
        if (component != nullptr && component->instance != nullptr &&
            component->instance->player != nullptr) {
            component->instance->player->update(dt * component->playback_rate);
            component->instance->visible = component->visible;
        }
    }
    result.m_TransformsUpdated = false;
    return dmGameObject::UPDATE_RESULT_OK;
}

// Component callback: submits visible SpriteLoop components to Defold's component render list.
// Geometry is generated later by render_list_dispatch so batching/order stay under Defold control.
dmGameObject::UpdateResult component_render(const dmGameObject::ComponentsRenderParams& params)
{
    SpriteLoopContext* context = static_cast<SpriteLoopContext*>(params.m_Context);
    SpriteLoopWorld* world = static_cast<SpriteLoopWorld*>(params.m_World);
    dmRender::HRenderContext render_context = context->render_context;
    const uint32_t component_count = static_cast<uint32_t>(world->components.size());
    if (component_count == 0 || render_context == 0) {
        return dmGameObject::UPDATE_RESULT_OK;
    }

    world->geometry_counts.resize(component_count);
    uint32_t visible_count = 0;
    uint32_t total_vertex_count = 0;
    uint32_t total_index_count = 0;
    uint32_t total_render_object_count = 0;
    for (uint32_t i = 0; i < component_count; ++i) {
        count_component_geometry(world->components[i], world->geometry_counts[i]);
        if (world->geometry_counts[i].index_count > 0) {
            ++visible_count;
            total_vertex_count += world->geometry_counts[i].vertex_count;
            total_index_count += world->geometry_counts[i].index_count;
            total_render_object_count += world->geometry_counts[i].render_object_count;
        }
    }

    if (visible_count == 0) {
        return dmGameObject::UPDATE_RESULT_OK;
    }
    world->vertex_data.reserve(total_vertex_count);
    world->index_data.reserve(total_index_count);
    world->render_objects.reserve(total_render_object_count);

    dmRender::RenderListEntry* render_list =
        dmRender::RenderListAlloc(render_context, visible_count);
    dmRender::HRenderListDispatch dispatch =
        dmRender::RenderListMakeDispatch(render_context, &render_list_dispatch, 0, world);
    dmRender::RenderListEntry* write_ptr = render_list;

    for (uint32_t i = 0; i < component_count; ++i) {
        if (world->geometry_counts[i].index_count == 0) {
            continue;
        }

        SplaDefoldComponent* component = world->components[i];
        dmRender::HMaterial material = component->resource->material->m_Material;
        dmGraphics::HTexture texture = instance_atlas_texture(*component->instance);
        HashState32 state;
        dmHashInit32(&state, false);
        dmHashUpdateBuffer32(&state, &material, sizeof(material));
        dmHashUpdateBuffer32(&state, &texture, sizeof(texture));
        uint32_t batch_key = dmHashFinal32(&state);

        write_ptr->m_WorldPosition = component_render_sort_position(component);
        write_ptr->m_UserData = i;
        write_ptr->m_BatchKey = batch_key;
        write_ptr->m_TagListKey = dmRender::GetMaterialTagListKey(material);
        write_ptr->m_Dispatch = dispatch;
        write_ptr->m_MinorOrder = 0;
        write_ptr->m_MajorOrder = dmRender::RENDER_ORDER_WORLD;
        ++write_ptr;
    }

    dmRender::RenderListSubmit(render_context, render_list, write_ptr);
    return dmGameObject::UPDATE_RESULT_OK;
}

// Component callback: returns the native component pointer for Lua component lookup.
void* component_get(const dmGameObject::ComponentGetParams& params)
{
    return component_from_userdata(params.m_UserData);
}

// Component type registration callback.
// ctx provides Defold engine contexts and game.project config; type receives all callback hooks.
dmGameObject::Result component_type_create(const dmGameObject::ComponentTypeCreateCtx* ctx,
                                           dmGameObject::HComponentType type)
{
    SpriteLoopContext* context = new SpriteLoopContext;
    context->factory = ctx->m_Factory;
    context->graphics_context = *reinterpret_cast<dmGraphics::HContext const*>(
        ctx->m_Contexts.Get(dmHashString64("graphics")));
    context->render_context = *reinterpret_cast<dmRender::HRenderContext const*>(
        ctx->m_Contexts.Get(dmHashString64("render")));
    context->max_components_per_world =
        dmConfigFile::GetInt(ctx->m_Config, max_count_property, 1024);

    dmGameObject::ComponentTypeSetPrio(type, 1050);
    dmGameObject::ComponentTypeSetContext(type, context);
    dmGameObject::ComponentTypeSetHasUserData(type, true);
    dmGameObject::ComponentTypeSetReadsTransforms(type, true);
    dmGameObject::ComponentTypeSetNewWorldFn(type, component_new_world);
    dmGameObject::ComponentTypeSetDeleteWorldFn(type, component_delete_world);
    dmGameObject::ComponentTypeSetCreateFn(type, component_create);
    dmGameObject::ComponentTypeSetInitFn(type, component_init);
    dmGameObject::ComponentTypeSetDestroyFn(type, component_destroy);
    dmGameObject::ComponentTypeSetAddToUpdateFn(type, component_add_to_update);
    dmGameObject::ComponentTypeSetUpdateFn(type, component_update);
    dmGameObject::ComponentTypeSetRenderFn(type, component_render);
    dmGameObject::ComponentTypeSetGetFn(type, component_get);
    dmGameObject::ComponentTypeSetSetPropertiesFn(type, component_set_properties);
    return dmGameObject::RESULT_OK;
}

// Component type teardown callback.
// Frees the shared SpriteLoopContext allocated in component_type_create.
dmGameObject::Result component_type_destroy(const dmGameObject::ComponentTypeCreateCtx* ctx,
                                            dmGameObject::HComponentType type)
{
    (void)ctx;
    SpriteLoopContext* context =
        static_cast<SpriteLoopContext*>(dmGameObject::ComponentTypeGetContext(type));
    delete context;
    return dmGameObject::RESULT_OK;
}

} // namespace

} // namespace spla_defold

DM_DECLARE_COMPONENT_TYPE(ComponentTypeSpriteLoopExt, SPLA_DEFOLD_COMPONENT_TYPE,
                          spla_defold::component_type_create,
                          spla_defold::component_type_destroy);
