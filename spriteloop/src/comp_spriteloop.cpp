#include "spla_defold.h"
#include "res_spriteloop.h"

#include <dmsdk/dlib/configfile_gen.hpp>
#include <dmsdk/dlib/hash.h>
#include <dmsdk/dlib/intersection.h>
#include <dmsdk/dlib/log.h>
#include <dmsdk/gameobject/component.h>
#include <dmsdk/render/render.h>
#include <dmsdk/resource/resource.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <unordered_map>
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

// Shared component-type state created once when the component type is registered.
struct SpriteLoopContext {
    dmResource::HFactory factory = 0;
    dmGraphics::HContext graphics_context = 0;
    dmRender::HRenderContext render_context = 0;
    uint32_t max_components_per_world = 1024;
};

struct RenderSignatureEntry {
    const SplaDefoldComponent* component = nullptr;
    dmRender::HMaterial material = 0;
    dmGraphics::HTexture texture = 0;
    const spriteloop::SplaAnimation* animation = nullptr;
    int frame_index = -1;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
};

struct RenderBatchRange {
    dmRender::HMaterial material = 0;
    dmGraphics::HTexture texture = 0;
    uint32_t index_start = 0;
    uint32_t index_count = 0;
};

struct RenderSlot {
    RenderSignatureEntry signature;
    uint32_t vertex_start = 0;
    uint32_t vertex_count = 0;
    uint32_t index_start = 0;
    uint32_t index_count = 0;
};

// Per-collection storage for this component type.
// Defold creates one such world for each runtime collection context that can contain SpriteLoop
// components; it stores the live components and transient render buffers for that context.
struct SpriteLoopWorld {
    SpriteLoopContext* context = nullptr;
    std::vector<SplaDefoldComponent*> components;
    std::vector<dmRender::RenderObject> render_objects;
    std::vector<SpriteLoopVertex> vertex_data;
    std::vector<uint32_t> quad_index_pattern;
    std::vector<RenderSignatureEntry> previous_signature;
    std::vector<RenderSignatureEntry> current_signature;
    std::vector<RenderBatchRange> previous_batch_ranges;
    std::vector<RenderBatchRange> current_batch_ranges;
    std::vector<RenderSlot> render_slots;
    std::unordered_map<const SplaDefoldComponent*, uint32_t> slot_lookup;
    std::vector<uint32_t> current_slot_indices;
    SplaDefoldRenderStats frame_stats;
    SplaDefoldRenderStats update_stats;
    dmGraphics::HVertexDeclaration vertex_declaration = 0;
    dmGraphics::HVertexBuffer vertex_buffer = 0;
    dmGraphics::HIndexBuffer index_buffer = 0;
    uint32_t current_quad_count = 0;
    uint32_t current_index_count = 0;
    bool previous_buffers_valid = false;
    bool reuse_frame_candidate = false;
    bool reuse_frame_used = false;
    bool reuse_reject_recorded = false;
    bool current_dispatch_had_batch = false;
};

struct ComponentGeometryCache {
    bool valid = false;
    const spriteloop::SplaAnimation* animation = nullptr;
    int frame_index = -1;
    dmVMath::Matrix4 world_matrix;
    dmVMath::Point3 local_position;
    dmVMath::Quat local_rotation;
    dmVMath::Vector3 local_scale;
    std::vector<SpriteLoopVertex> vertices;
};

struct PreparedComponentGeometry {
    const SplaDefoldComponent* component = nullptr;
    dmRender::HMaterial material = 0;
    dmGraphics::HTexture texture = 0;
    const spriteloop::SplaAnimation* animation = nullptr;
    int frame_index = -1;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    bool valid = false;
    bool cache_hit = false;
};

uint32_t render_batch_key(dmRender::HMaterial material,
                          dmGraphics::HTexture texture)
{
    HashState32 state;
    dmHashInit32(&state, false);
    dmHashUpdateBuffer32(&state, &material, sizeof(material));
    dmHashUpdateBuffer32(&state, &texture, sizeof(texture));
    return dmHashFinal32(&state);
}

// Returns value when it is a non-empty string, otherwise fallback.
const char* non_empty_or_default(const char* value, const char* fallback)
{
    return value != nullptr && value[0] != '\0' ? value : fallback;
}

std::unordered_map<const SplaDefoldComponent*, ComponentGeometryCache>& geometry_cache()
{
    static std::unordered_map<const SplaDefoldComponent*, ComponentGeometryCache> cache;
    return cache;
}

void invalidate_uploaded_geometry(SpriteLoopWorld* world)
{
    if (world == nullptr) {
        return;
    }

    world->previous_buffers_valid = false;
    world->previous_signature.clear();
    world->previous_batch_ranges.clear();
    world->render_slots.clear();
    world->current_slot_indices.clear();
    world->slot_lookup.clear();
}

// Removes a component pointer from a world without deleting the component itself.
void remove_component(SpriteLoopWorld* world, SplaDefoldComponent* component)
{
    if (world == nullptr || component == nullptr) {
        return;
    }

    geometry_cache().erase(component);
    invalidate_uploaded_geometry(world);
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

// Rotates v by quaternion q without relying on engine helper overloads.
dmVMath::Vector3 rotate_vector(const dmVMath::Quat& q, const dmVMath::Vector3& v)
{
    const dmVMath::Vector3 quat_vector(q.getX(), q.getY(), q.getZ());
    const dmVMath::Vector3 uv = dmVMath::Cross(quat_vector, v);
    const dmVMath::Vector3 uuv = dmVMath::Cross(quat_vector, uv);
    return v + ((uv * q.getW()) + uuv) * 2.0f;
}

const SplaDefoldBakedFrame* current_baked_frame(const SplaDefoldInstance& instance)
{
    const spriteloop::SplaAnimation* animation = instance.player->current_animation();
    if (animation == nullptr) {
        return nullptr;
    }

    const int frame_index = instance.player->current_frame_index();
    if (frame_index < 0) {
        return nullptr;
    }

    const std::vector<SplaDefoldBakedAnimation>& animations =
        instance_baked_animations(instance);
    for (const SplaDefoldBakedAnimation& baked_animation : animations) {
        if (baked_animation.id == animation->id &&
            frame_index < static_cast<int>(baked_animation.frames.size())) {
            return &baked_animation.frames[static_cast<std::size_t>(frame_index)];
        }
    }

    return nullptr;
}

bool near_equal(float a, float b)
{
    return std::abs(a - b) <= 0.00001f;
}

bool same_vector4(const dmVMath::Vector4& a, const dmVMath::Vector4& b)
{
    return near_equal(a.getX(), b.getX()) && near_equal(a.getY(), b.getY()) &&
           near_equal(a.getZ(), b.getZ()) && near_equal(a.getW(), b.getW());
}

bool same_point3(const dmVMath::Point3& a, const dmVMath::Point3& b)
{
    return near_equal(a.getX(), b.getX()) && near_equal(a.getY(), b.getY()) &&
           near_equal(a.getZ(), b.getZ());
}

bool same_vector3(const dmVMath::Vector3& a, const dmVMath::Vector3& b)
{
    return near_equal(a.getX(), b.getX()) && near_equal(a.getY(), b.getY()) &&
           near_equal(a.getZ(), b.getZ());
}

bool same_quat(const dmVMath::Quat& a, const dmVMath::Quat& b)
{
    return near_equal(a.getX(), b.getX()) && near_equal(a.getY(), b.getY()) &&
           near_equal(a.getZ(), b.getZ()) && near_equal(a.getW(), b.getW());
}

bool same_matrix4(const dmVMath::Matrix4& a, const dmVMath::Matrix4& b)
{
    return same_vector4(a * dmVMath::Vector4(1.0f, 0.0f, 0.0f, 0.0f),
                        b * dmVMath::Vector4(1.0f, 0.0f, 0.0f, 0.0f)) &&
           same_vector4(a * dmVMath::Vector4(0.0f, 1.0f, 0.0f, 0.0f),
                        b * dmVMath::Vector4(0.0f, 1.0f, 0.0f, 0.0f)) &&
           same_vector4(a * dmVMath::Vector4(0.0f, 0.0f, 1.0f, 0.0f),
                        b * dmVMath::Vector4(0.0f, 0.0f, 1.0f, 0.0f)) &&
           same_vector4(a * dmVMath::Vector4(0.0f, 0.0f, 0.0f, 1.0f),
                        b * dmVMath::Vector4(0.0f, 0.0f, 0.0f, 1.0f));
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

bool component_intersects_frustum(const SplaDefoldComponent* component,
                                  const dmIntersection::Frustum* frustum)
{
    if (component == nullptr || component->instance == nullptr || frustum == nullptr) {
        return true;
    }

    const SplaDefoldInstance& instance = *component->instance;
    const SplaDefoldBounds& bounds = instance_bounds(instance);

    const dmVMath::Matrix4& world = dmGameObject::GetWorldMatrix(instance.game_object);
    const dmVMath::Vector4 center_local =
        transform_component_local_point(instance, bounds.center_x, bounds.center_y);
    const dmVMath::Vector4 center_world = world * center_local;

    const dmVMath::Vector3 world_scale = dmGameObject::GetWorldScale(instance.game_object);
    const float max_component_scale =
        std::max(std::abs(instance.local_scale.getX()), std::abs(instance.local_scale.getY()));
    const float max_world_scale =
        std::max(std::abs(world_scale.getX()), std::abs(world_scale.getY()));
    const float radius_scale = max_component_scale * max_world_scale;
    const float radius_sq = bounds.radius_sq * radius_scale * radius_scale;

    return dmIntersection::TestFrustumSphereSq(*frustum, center_world, radius_sq);
}

SpriteLoopVertex transform_baked_vertex(const SplaDefoldInstance& instance,
                                        const dmVMath::Matrix4& world_matrix,
                                        const SplaDefoldBakedVertex& baked)
{
    const dmVMath::Vector4 local =
        transform_component_local_point(instance, baked.x, baked.y);
    const dmVMath::Vector4 transformed = world_matrix * local;

    SpriteLoopVertex vertex;
    vertex.x = transformed.getX();
    vertex.y = transformed.getY();
    vertex.z = transformed.getZ();
    vertex.u = baked.u;
    vertex.v = baked.v;
    vertex.a = baked.a;
    return vertex;
}

// Returns true when a component has enough state to emit geometry this frame.
bool component_should_render(const SplaDefoldComponent* component)
{
    return component != nullptr && component->visible && component->instance != nullptr &&
           component->instance->visible && component->instance->player != nullptr &&
           component->instance->game_object != 0 && component->instance->player->current_frame() != nullptr;
}

bool component_is_render_candidate(const SplaDefoldComponent* component)
{
    return component_should_render(component) &&
           component->resource != nullptr && component->resource->material != nullptr &&
           component->resource->material->m_Material != 0 &&
           instance_atlas_texture(*component->instance) != 0;
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

bool same_signature_entry(const RenderSignatureEntry& a,
                          const RenderSignatureEntry& b)
{
    return a.component == b.component && a.material == b.material && a.texture == b.texture &&
           a.animation == b.animation && a.frame_index == b.frame_index &&
           a.vertex_count == b.vertex_count && a.index_count == b.index_count;
}

bool same_layout_entry(const RenderSignatureEntry& a,
                       const RenderSignatureEntry& b)
{
    return a.component == b.component && a.material == b.material && a.texture == b.texture &&
           a.vertex_count == b.vertex_count && a.index_count == b.index_count;
}

void reject_reuse(SpriteLoopWorld* world, uint32_t SplaDefoldRenderStats::*reason)
{
    if (world == nullptr || world->reuse_reject_recorded) {
        return;
    }

    ++world->frame_stats.reuse_rejected;
    ++(world->frame_stats.*reason);
    world->reuse_reject_recorded = true;
}

void ensure_quad_index_pattern(SpriteLoopWorld* world, uint32_t quad_count)
{
    if (world == nullptr) {
        return;
    }

    const uint32_t old_quad_count =
        static_cast<uint32_t>(world->quad_index_pattern.size() / 6);
    if (old_quad_count >= quad_count) {
        return;
    }

    world->quad_index_pattern.resize(static_cast<std::size_t>(quad_count) * 6);
    for (uint32_t quad = old_quad_count; quad < quad_count; ++quad) {
        const uint32_t base_vertex = quad * 4;
        const uint32_t base_index = quad * 6;
        world->quad_index_pattern[base_index + 0] = base_vertex + 0;
        world->quad_index_pattern[base_index + 1] = base_vertex + 1;
        world->quad_index_pattern[base_index + 2] = base_vertex + 2;
        world->quad_index_pattern[base_index + 3] = base_vertex + 0;
        world->quad_index_pattern[base_index + 4] = base_vertex + 2;
        world->quad_index_pattern[base_index + 5] = base_vertex + 3;
    }
    ++world->frame_stats.index_pattern_rebuilds;
}

RenderSignatureEntry signature_entry_from_prepared(
    const PreparedComponentGeometry& prepared)
{
    RenderSignatureEntry entry;
    entry.component = prepared.component;
    entry.material = prepared.material;
    entry.texture = prepared.texture;
    entry.animation = prepared.animation;
    entry.frame_index = prepared.frame_index;
    entry.vertex_count = prepared.vertex_count;
    entry.index_count = prepared.index_count;
    return entry;
}

PreparedComponentGeometry prepare_component_geometry(SpriteLoopWorld* world,
                                                     const SplaDefoldComponent* component)
{
    PreparedComponentGeometry prepared;
    if (!component_should_render(component)) {
        return prepared;
    }

    const SplaDefoldInstance& instance = *component->instance;
    dmGraphics::HTexture atlas_texture = instance_atlas_texture(instance);
    if (atlas_texture == 0) {
        return prepared;
    }

    const dmVMath::Matrix4& world_matrix = dmGameObject::GetWorldMatrix(instance.game_object);
    const spriteloop::SplaAnimation* animation = instance.player->current_animation();
    const int frame_index = instance.player->current_frame_index();
    const SplaDefoldBakedFrame* baked_frame = current_baked_frame(instance);
    if (animation == nullptr || baked_frame == nullptr) {
        return prepared;
    }

    ComponentGeometryCache& cache = geometry_cache()[component];
    const bool cache_hit =
        cache.valid && cache.animation == animation &&
        cache.frame_index == frame_index &&
        same_matrix4(cache.world_matrix, world_matrix) &&
        same_point3(cache.local_position, instance.local_position) &&
        same_quat(cache.local_rotation, instance.local_rotation) &&
        same_vector3(cache.local_scale, instance.local_scale);

    if (cache_hit) {
        ++world->frame_stats.vertex_cache_hits;
        prepared.cache_hit = true;
    } else {
        ++world->frame_stats.vertex_cache_misses;
        cache.valid = false;
        cache.animation = animation;
        cache.frame_index = frame_index;
        cache.world_matrix = world_matrix;
        cache.local_position = instance.local_position;
        cache.local_rotation = instance.local_rotation;
        cache.local_scale = instance.local_scale;
        cache.vertices.clear();
        cache.vertices.reserve(baked_frame->vertices.size());

        for (const SplaDefoldBakedVertex& baked_vertex : baked_frame->vertices) {
            cache.vertices.push_back(
                transform_baked_vertex(instance, world_matrix, baked_vertex));
        }
        cache.valid = true;
    }

    const uint32_t vertex_count = static_cast<uint32_t>((cache.vertices.size() / 4) * 4);
    prepared.component = component;
    prepared.material = component->resource->material->m_Material;
    prepared.texture = atlas_texture;
    prepared.animation = animation;
    prepared.frame_index = frame_index;
    prepared.vertex_count = vertex_count;
    prepared.index_count = (vertex_count / 4) * 6;
    prepared.valid = prepared.vertex_count > 0;
    return prepared;
}

bool copy_prepared_vertices_to_slot(SpriteLoopWorld* world,
                                    const PreparedComponentGeometry& prepared,
                                    RenderSlot& slot)
{
    if (world == nullptr || !prepared.valid) {
        return false;
    }

    const auto found = geometry_cache().find(prepared.component);
    if (found == geometry_cache().end() || !found->second.valid) {
        return false;
    }

    const ComponentGeometryCache& cache = found->second;
    const std::size_t vertex_count =
        std::min<std::size_t>(prepared.vertex_count, cache.vertices.size());
    const std::size_t vertex_start = slot.vertex_start;
    if (vertex_count == 0 ||
        vertex_start + vertex_count > world->vertex_data.size()) {
        return false;
    }

    std::copy(cache.vertices.begin(), cache.vertices.begin() + vertex_count,
              world->vertex_data.begin() + vertex_start);
    ++world->frame_stats.slot_vertex_copies;
    ++world->frame_stats.vertex_bulk_copies;
    ++world->frame_stats.dirty_slots;
    return true;
}

void rebuild_slot_layout(SpriteLoopWorld* world,
                         const std::vector<PreparedComponentGeometry>& prepared_entries)
{
    world->render_slots.clear();
    world->slot_lookup.clear();
    world->vertex_data.clear();
    world->current_quad_count = 0;
    world->current_index_count = 0;
    ++world->frame_stats.slot_layout_rebuilds;
}

void append_slot_layout_batch(SpriteLoopWorld* world,
                              const std::vector<PreparedComponentGeometry>& prepared_entries)
{
    world->current_slot_indices.clear();
    world->current_slot_indices.reserve(prepared_entries.size());

    uint32_t vertex_cursor = static_cast<uint32_t>(world->vertex_data.size());
    uint32_t index_cursor = world->render_slots.empty()
                                ? 0
                                : world->render_slots.back().index_start +
                                      world->render_slots.back().index_count;
    for (const PreparedComponentGeometry& prepared : prepared_entries) {
        RenderSlot slot;
        slot.signature = signature_entry_from_prepared(prepared);
        slot.vertex_start = vertex_cursor;
        slot.vertex_count = prepared.vertex_count;
        slot.index_start = index_cursor;
        slot.index_count = prepared.index_count;
        const uint32_t slot_index = static_cast<uint32_t>(world->render_slots.size());
        world->render_slots.push_back(slot);
        world->slot_lookup[prepared.component] = slot_index;
        world->current_slot_indices.push_back(slot_index);
        vertex_cursor += prepared.vertex_count;
        index_cursor += prepared.index_count;
    }

    world->vertex_data.resize(vertex_cursor);
    world->current_quad_count = vertex_cursor / 4;
    world->current_index_count = index_cursor;
}

bool resolve_existing_slots(SpriteLoopWorld* world,
                            const std::vector<PreparedComponentGeometry>& prepared_entries)
{
    world->current_slot_indices.clear();
    world->current_slot_indices.reserve(prepared_entries.size());
    for (const PreparedComponentGeometry& prepared : prepared_entries) {
        const auto found = world->slot_lookup.find(prepared.component);
        if (found == world->slot_lookup.end() ||
            found->second >= world->render_slots.size()) {
            return false;
        }

        const RenderSlot& slot = world->render_slots[found->second];
        const RenderSignatureEntry current = signature_entry_from_prepared(prepared);
        if (!same_layout_entry(slot.signature, current)) {
            return false;
        }
        world->current_slot_indices.push_back(found->second);
    }
    return true;
}

void build_slot_render_objects(SpriteLoopWorld* world,
                               dmRender::HRenderContext render_context,
                               const std::vector<PreparedComponentGeometry>& prepared_entries)
{
    dmRender::HMaterial current_material = 0;
    dmGraphics::HTexture current_texture = 0;
    uint32_t batch_index_start = 0;
    uint32_t batch_index_count = 0;

    auto flush_batch = [&]() {
        if (batch_index_count == 0 || current_texture == 0 || current_material == 0) {
            return;
        }

        world->render_objects.emplace_back();
        fill_render_object(world, render_context, world->render_objects.back(),
                           current_texture, current_material, batch_index_start,
                           batch_index_count);
        RenderBatchRange range;
        range.material = current_material;
        range.texture = current_texture;
        range.index_start = batch_index_start;
        range.index_count = batch_index_count;
        world->current_batch_ranges.push_back(range);
        ++world->frame_stats.batch_flushes;
        ++world->frame_stats.render_objects;
        batch_index_count = 0;
    };

    for (std::size_t i = 0; i < prepared_entries.size(); ++i) {
        const PreparedComponentGeometry& prepared = prepared_entries[i];
        if (!prepared.valid || i >= world->current_slot_indices.size()) {
            continue;
        }

        const RenderSlot& slot = world->render_slots[world->current_slot_indices[i]];
        const bool can_extend =
            batch_index_count > 0 &&
            prepared.texture == current_texture &&
            prepared.material == current_material &&
            slot.index_start == batch_index_start + batch_index_count;
        if (batch_index_count > 0 && !can_extend) {
            flush_batch();
        }

        if (batch_index_count == 0) {
            current_texture = prepared.texture;
            current_material = prepared.material;
            batch_index_start = slot.index_start;
        }

        batch_index_count += slot.index_count;
    }
    flush_batch();
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
        world->current_signature.clear();
        world->current_batch_ranges.clear();
        world->current_slot_indices.clear();
        world->current_quad_count = 0;
        world->current_index_count = 0;
        world->reuse_frame_used = false;
        world->reuse_reject_recorded = false;
        world->current_dispatch_had_batch = false;
        world->reuse_frame_candidate = world->previous_buffers_valid;
        if (!world->previous_buffers_valid) {
            rebuild_slot_layout(world, {});
            world->frame_stats.slot_layout_rebuilds = 0;
        }
        break;
    case dmRender::RENDER_LIST_OPERATION_BATCH:
    {
        const std::size_t batch_entry_count =
            static_cast<std::size_t>(params.m_End - params.m_Begin);
        if (batch_entry_count > 0) {
            world->current_dispatch_had_batch = true;
        }
        std::vector<PreparedComponentGeometry> prepared_entries;
        prepared_entries.reserve(batch_entry_count);

        bool can_reuse_complete_batch = false;
        if (batch_entry_count > 0 && !world->reuse_frame_candidate) {
            reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_invalid_previous);
        }
        if (world->reuse_frame_candidate && !world->current_signature.empty()) {
            can_reuse_complete_batch = false;
            reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_split_batch);
        }
        if (world->reuse_frame_candidate &&
            batch_entry_count != world->previous_signature.size()) {
            can_reuse_complete_batch = false;
            reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_signature_size);
        }

        for (uint32_t* i = params.m_Begin; i != params.m_End; ++i) {
            const uint32_t component_index =
                static_cast<uint32_t>(params.m_Buf[*i].m_UserData);
            if (component_index >= world->components.size()) {
                can_reuse_complete_batch = false;
                reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_signature_entry);
                continue;
            }

            const SplaDefoldComponent* component = world->components[component_index];
            if (!component_should_render(component)) {
                can_reuse_complete_batch = false;
                reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_signature_entry);
                continue;
            }

            dmRender::HMaterial material = component->resource->material->m_Material;
            dmGraphics::HTexture texture = instance_atlas_texture(*component->instance);
            if (texture == 0 || material == 0) {
                can_reuse_complete_batch = false;
                reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_signature_entry);
                continue;
            }

            PreparedComponentGeometry prepared =
                prepare_component_geometry(world, component);
            prepared.material = material;
            prepared.texture = texture;
            if (!prepared.valid) {
                can_reuse_complete_batch = false;
                reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_cache_miss);
                continue;
            }

            prepared_entries.push_back(prepared);
            world->current_signature.push_back(signature_entry_from_prepared(prepared));
            const std::size_t signature_index = world->current_signature.size() - 1;
            if (signature_index >= world->previous_signature.size() ||
                !same_signature_entry(world->current_signature[signature_index],
                                      world->previous_signature[signature_index])) {
                can_reuse_complete_batch = false;
                if (!prepared.cache_hit) {
                    reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_cache_miss);
                } else {
                    reject_reuse(world, &SplaDefoldRenderStats::reuse_reject_signature_entry);
                }
            }
        }

        if (can_reuse_complete_batch) {
            world->current_batch_ranges = world->previous_batch_ranges;
            for (const RenderBatchRange& range : world->previous_batch_ranges) {
                world->render_objects.emplace_back();
                fill_render_object(world, params.m_Context, world->render_objects.back(),
                                   range.texture, range.material, range.index_start,
                                   range.index_count);
                ++world->frame_stats.batch_flushes;
                ++world->frame_stats.render_objects;
            }
            ++world->frame_stats.geometry_reused;
            world->reuse_frame_used = true;
            break;
        }

        world->reuse_frame_candidate = false;

        const bool layout_matches_previous =
            world->previous_buffers_valid &&
            resolve_existing_slots(world, prepared_entries);
        if (!layout_matches_previous) {
            if (world->current_signature.size() == prepared_entries.size()) {
                rebuild_slot_layout(world, {});
            } else if (world->frame_stats.slot_layout_rebuilds == 0) {
                ++world->frame_stats.slot_layout_rebuilds;
            }
            append_slot_layout_batch(world, prepared_entries);
        }

        for (std::size_t i = 0; i < prepared_entries.size(); ++i) {
            const PreparedComponentGeometry& prepared = prepared_entries[i];
            if (!prepared.valid || i >= world->current_slot_indices.size()) {
                continue;
            }

            RenderSlot& slot = world->render_slots[world->current_slot_indices[i]];
            const RenderSignatureEntry current = signature_entry_from_prepared(prepared);
            const bool needs_copy =
                !layout_matches_previous ||
                !same_signature_entry(current, slot.signature) ||
                !prepared.cache_hit;
            if (needs_copy && copy_prepared_vertices_to_slot(world, prepared, slot)) {
                slot.signature = current;
            }

            world->frame_stats.indices_generated += slot.index_count;
            world->frame_stats.quads_generated += slot.index_count / 6;
            world->frame_stats.vertices_generated += slot.vertex_count;
        }
        world->current_quad_count =
            static_cast<uint32_t>(world->vertex_data.size() / 4);
        world->current_index_count =
            world->render_slots.empty()
                ? 0
                : world->render_slots.back().index_start +
                      world->render_slots.back().index_count;
        build_slot_render_objects(world, params.m_Context, prepared_entries);
        break;
    }
    case dmRender::RENDER_LIST_OPERATION_END:
        if (world->reuse_frame_used) {
            world->previous_signature = world->current_signature;
            world->previous_batch_ranges = world->current_batch_ranges;
            world->frame_stats.slot_count =
                static_cast<uint32_t>(world->render_slots.size());
            set_render_stats(world->frame_stats);
            break;
        }

        if (!world->vertex_data.empty() && world->current_index_count > 0) {
            const bool has_dirty_slots = world->frame_stats.dirty_slots > 0;
            const bool layout_rebuilt = world->frame_stats.slot_layout_rebuilds > 0;
            if (!has_dirty_slots && world->previous_buffers_valid) {
                ++world->frame_stats.geometry_reused;
                world->previous_signature = world->current_signature;
                world->previous_batch_ranges = world->current_batch_ranges;
            } else {
                ensure_quad_index_pattern(world, world->current_quad_count);
                const uint32_t vertex_bytes =
                    static_cast<uint32_t>(world->vertex_data.size() * sizeof(SpriteLoopVertex));
                const uint32_t index_bytes =
                    static_cast<uint32_t>(world->current_index_count * sizeof(uint32_t));
                const bool upload_index_buffer =
                    layout_rebuilt || !world->previous_buffers_valid;
                dmGraphics::SetVertexBufferData(
                    world->vertex_buffer, vertex_bytes, world->vertex_data.data(),
                    dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
                ++world->frame_stats.vertex_uploads;
                ++world->frame_stats.full_vertex_uploads;
                world->frame_stats.bytes_uploaded += vertex_bytes;
                if (upload_index_buffer) {
                    dmGraphics::SetIndexBufferData(
                        world->index_buffer, index_bytes, world->quad_index_pattern.data(),
                        dmGraphics::BUFFER_USAGE_DYNAMIC_DRAW);
                    ++world->frame_stats.index_uploads;
                    if (layout_rebuilt) {
                        ++world->frame_stats.index_layout_uploads;
                    }
                    world->frame_stats.bytes_uploaded += index_bytes;
                }
                ++world->frame_stats.buffer_uploads;
                world->previous_buffers_valid = true;
                world->previous_signature = world->current_signature;
                world->previous_batch_ranges = world->current_batch_ranges;
            }
        } else if (world->current_dispatch_had_batch) {
            invalidate_uploaded_geometry(world);
        }
        world->frame_stats.slot_count =
            static_cast<uint32_t>(world->render_slots.size());
        set_render_stats(world->frame_stats);
        break;
    }
}

void render_list_visibility(const dmRender::RenderListVisibilityParams& params)
{
    SpriteLoopWorld* world = static_cast<SpriteLoopWorld*>(params.m_UserData);
    if (world == nullptr || world->context == nullptr || params.m_Frustum == nullptr) {
        return;
    }

    world->frame_stats.frustum_visible = 0;
    world->frame_stats.frustum_culled = 0;
    for (uint32_t i = 0; i < params.m_NumEntries; ++i) {
        dmRender::RenderListEntry& entry = params.m_Entries[i];
        const uint32_t component_index = static_cast<uint32_t>(entry.m_UserData);
        if (component_index >= world->components.size()) {
            entry.m_Visibility = dmRender::VISIBILITY_NONE;
            continue;
        }

        entry.m_Visibility =
            component_intersects_frustum(world->components[component_index], params.m_Frustum)
                ? dmRender::VISIBILITY_FULL
                : dmRender::VISIBILITY_NONE;
        if (entry.m_Visibility == dmRender::VISIBILITY_FULL) {
            ++world->frame_stats.frustum_visible;
        } else {
            ++world->frame_stats.frustum_culled;
        }
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
    world->update_stats = {};
    const float dt = params.m_UpdateContext != nullptr ? params.m_UpdateContext->m_DT : 0.0f;
    for (SplaDefoldComponent* component : world->components) {
        if (component != nullptr && component->instance != nullptr &&
            component->instance->player != nullptr) {
            component->instance->player->update(dt * component->playback_rate);
            ++world->update_stats.playback_updates;
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
        set_render_stats(world->update_stats);
        return dmGameObject::UPDATE_RESULT_OK;
    }

    world->frame_stats = world->update_stats;
    world->frame_stats.component_count = component_count;

    uint32_t visible_count = 0;
    for (uint32_t i = 0; i < component_count; ++i) {
        if (component_is_render_candidate(world->components[i])) {
            ++visible_count;
        }
    }
    world->frame_stats.render_candidates = visible_count;

    if (visible_count == 0) {
        set_render_stats(world->frame_stats);
        return dmGameObject::UPDATE_RESULT_OK;
    }

    struct RenderBatchOrder {
        dmRender::HMaterial material = 0;
        dmGraphics::HTexture texture = 0;
        uint32_t minor_order = 0;
    };
    std::vector<RenderBatchOrder> batch_orders;

    auto minor_order_for_batch = [&batch_orders](dmRender::HMaterial material,
                                                 dmGraphics::HTexture texture) {
        for (const RenderBatchOrder& order : batch_orders) {
            if (order.material == material && order.texture == texture) {
                return order.minor_order;
            }
        }

        RenderBatchOrder order;
        order.material = material;
        order.texture = texture;
        order.minor_order = static_cast<uint32_t>(batch_orders.size());
        batch_orders.push_back(order);
        return order.minor_order;
    };

    world->render_objects.reserve(visible_count);

    dmRender::RenderListEntry* render_list =
        dmRender::RenderListAlloc(render_context, visible_count);
    dmRender::HRenderListDispatch dispatch =
        dmRender::RenderListMakeDispatch(render_context, &render_list_dispatch,
                                         &render_list_visibility, world);
    dmRender::RenderListEntry* write_ptr = render_list;
    for (uint32_t i = 0; i < component_count; ++i) {
        SplaDefoldComponent* component = world->components[i];
        if (!component_is_render_candidate(component)) {
            continue;
        }

        dmRender::HMaterial material = component->resource->material->m_Material;
        dmGraphics::HTexture texture = instance_atlas_texture(*component->instance);
        write_ptr->m_WorldPosition = component_render_sort_position(component);
        write_ptr->m_UserData = i;
        write_ptr->m_BatchKey = render_batch_key(material, texture);
        write_ptr->m_TagListKey = dmRender::GetMaterialTagListKey(material);
        write_ptr->m_Dispatch = dispatch;
        write_ptr->m_MinorOrder = minor_order_for_batch(material, texture);
        write_ptr->m_MajorOrder = dmRender::RENDER_ORDER_WORLD;
        write_ptr->m_Visibility = dmRender::VISIBILITY_FULL;
        ++write_ptr;
        ++world->frame_stats.render_entries_submitted;
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
