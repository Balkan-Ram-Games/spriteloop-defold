#include "spla_defold.h"

#include "spriteloop/spla.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Instance lifetime registry for the Defold adapter.
//
// Lua-created handles and Defold component instances both share SplaDefoldInstance. The registry
// lets extension shutdown find live objects without owning them.
namespace spla_defold {

namespace {

// Returns the process-local list of live SpriteLoop instances.
std::vector<SplaDefoldInstance*>& live_instances()
{
    static std::vector<SplaDefoldInstance*> instances;
    return instances;
}

std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>>& shared_resources()
{
    static std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>> resources;
    return resources;
}

SplaDefoldRenderStats& global_render_stats()
{
    static SplaDefoldRenderStats stats;
    return stats;
}

std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>>::iterator find_shared_resource(
    const std::string& path)
{
    std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>>& resources = shared_resources();
    return std::find_if(resources.begin(), resources.end(),
                        [&path](const std::unique_ptr<SplaDefoldSharedPackageResource>& resource) {
                            return resource != nullptr && resource->path == path;
                        });
}

void release_shared_package_resource(SplaDefoldSharedPackageResource* resource,
                                     dmGraphics::HContext graphics_context)
{
    if (resource == nullptr) {
        return;
    }

    if (resource->ref_count > 1) {
        --resource->ref_count;
        return;
    }

    std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>>& resources = shared_resources();
    const auto it = std::find_if(resources.begin(), resources.end(),
                                 [resource](const std::unique_ptr<SplaDefoldSharedPackageResource>& entry) {
                                     return entry.get() == resource;
                                 });
    if (it != resources.end()) {
        destroy_atlas_texture(graphics_context, (*it)->atlas_texture);
        resources.erase(it);
    }
}

} // namespace

// Creates a SpriteLoop playback instance from .spla bytes and uploads its part textures.
// path is only used for diagnostics, bytes/byte_count identify the package payload, and error
// is filled when loading, decoding, or texture upload fails.
SplaDefoldInstance* create_instance_from_memory(const char* path,
                                                const std::uint8_t* bytes,
                                                std::size_t byte_count,
                                                std::string& error)
{
    auto package_result = spriteloop::load_package_from_memory(bytes, byte_count, path);
    if (!package_result) {
        error = package_result.error().message;
        return nullptr;
    }

    std::unique_ptr<SplaDefoldInstance> instance(new SplaDefoldInstance);
    instance->path = path;
    instance->byte_count = byte_count;
    instance->package = std::move(package_result).value();

    if (!build_image_resources(instance->package, instance->image_resources, error)) {
        return nullptr;
    }
    if (!upload_image_resources(dmGraphics::GetInstalledContext(), instance->image_resources,
                                instance->atlas_texture, instance->atlas_width,
                                instance->atlas_height, instance->atlas_texture_bytes, error)) {
        return nullptr;
    }
    instance->bounds = calculate_package_bounds(instance->package, instance->image_resources);
    instance->baked_animations =
        build_baked_animations(instance->package, instance->image_resources);

    instance->player.reset(new spriteloop::SplaPlayer(instance->package));
    register_instance(instance.get());
    return instance.release();
}

SplaDefoldSharedPackageResource* acquire_shared_package_resource(const char* path,
                                                                 const std::uint8_t* bytes,
                                                                 std::size_t byte_count,
                                                                 std::string& error)
{
    const std::string cache_path = path != nullptr ? path : "";
    if (SplaDefoldSharedPackageResource* existing = retain_shared_package_resource(path)) {
        return existing;
    }

    auto package_result = spriteloop::load_package_from_memory(bytes, byte_count, cache_path);
    if (!package_result) {
        error = package_result.error().message;
        return nullptr;
    }

    std::unique_ptr<SplaDefoldSharedPackageResource> resource(new SplaDefoldSharedPackageResource);
    resource->path = cache_path;
    resource->byte_count = byte_count;
    resource->package = std::move(package_result).value();

    if (!build_image_resources(resource->package, resource->image_resources, error)) {
        return nullptr;
    }
    if (!upload_image_resources(dmGraphics::GetInstalledContext(), resource->image_resources,
                                resource->atlas_texture, resource->atlas_width,
                                resource->atlas_height, resource->atlas_texture_bytes, error)) {
        return nullptr;
    }
    resource->bounds = calculate_package_bounds(resource->package, resource->image_resources);
    resource->baked_animations =
        build_baked_animations(resource->package, resource->image_resources);

    resource->ref_count = 1;
    SplaDefoldSharedPackageResource* raw_resource = resource.get();
    shared_resources().push_back(std::move(resource));
    return raw_resource;
}

SplaDefoldSharedPackageResource* retain_shared_package_resource(const char* path)
{
    const std::string cache_path = path != nullptr ? path : "";
    auto existing = find_shared_resource(cache_path);
    if (existing == shared_resources().end()) {
        return nullptr;
    }

    ++(*existing)->ref_count;
    return existing->get();
}

SplaDefoldInstance* create_instance_from_shared_resource(
    SplaDefoldSharedPackageResource* shared_resource)
{
    if (shared_resource == nullptr) {
        return nullptr;
    }

    std::unique_ptr<SplaDefoldInstance> instance(new SplaDefoldInstance);
    instance->path = shared_resource->path;
    instance->byte_count = shared_resource->byte_count;
    instance->shared_resource = shared_resource;
    instance->player.reset(new spriteloop::SplaPlayer(shared_resource->package));
    register_instance(instance.get());
    return instance.release();
}

// Destroys an instance previously returned by create_instance_from_memory.
// graphics_context is used to release any Defold textures owned by the instance.
void destroy_instance(SplaDefoldInstance* instance, dmGraphics::HContext graphics_context)
{
    if (instance == nullptr) {
        return;
    }

    unregister_instance(instance);
    if (instance->shared_resource != nullptr) {
        instance->player.reset();
        release_shared_package_resource(instance->shared_resource, graphics_context);
        instance->shared_resource = nullptr;
    } else {
        destroy_atlas_texture(graphics_context, instance->atlas_texture);
    }
    delete instance;
}

void destroy_all_shared_package_resources(dmGraphics::HContext graphics_context)
{
    for (const std::unique_ptr<SplaDefoldSharedPackageResource>& resource : shared_resources()) {
        if (resource != nullptr) {
            destroy_atlas_texture(graphics_context, resource->atlas_texture);
        }
    }
    shared_resources().clear();
}

const spriteloop::SplaPackage& instance_package(const SplaDefoldInstance& instance)
{
    return instance.shared_resource != nullptr ? instance.shared_resource->package
                                               : instance.package;
}

const std::vector<SplaDefoldImageResource>& instance_image_resources(
    const SplaDefoldInstance& instance)
{
    return instance.shared_resource != nullptr ? instance.shared_resource->image_resources
                                               : instance.image_resources;
}

std::vector<SplaDefoldImageResource>& instance_image_resources(SplaDefoldInstance& instance)
{
    return instance.shared_resource != nullptr ? instance.shared_resource->image_resources
                                               : instance.image_resources;
}

dmGraphics::HTexture instance_atlas_texture(const SplaDefoldInstance& instance)
{
    return instance.shared_resource != nullptr ? instance.shared_resource->atlas_texture
                                               : instance.atlas_texture;
}

std::size_t instance_atlas_texture_bytes(const SplaDefoldInstance& instance)
{
    return instance.shared_resource != nullptr ? instance.shared_resource->atlas_texture_bytes
                                               : instance.atlas_texture_bytes;
}

const SplaDefoldBounds& instance_bounds(const SplaDefoldInstance& instance)
{
    return instance.shared_resource != nullptr ? instance.shared_resource->bounds
                                               : instance.bounds;
}

const std::vector<SplaDefoldBakedAnimation>& instance_baked_animations(
    const SplaDefoldInstance& instance)
{
    return instance.shared_resource != nullptr ? instance.shared_resource->baked_animations
                                               : instance.baked_animations;
}

SplaDefoldBounds calculate_package_bounds(const spriteloop::SplaPackage& package,
                                          const std::vector<SplaDefoldImageResource>& resources)
{
    float max_image_extent = 0.0f;
    for (const SplaDefoldImageResource& image : resources) {
        max_image_extent =
            std::max(max_image_extent,
                     std::max(static_cast<float>(image.width),
                              static_cast<float>(image.height)));
    }

    SplaDefoldBounds bounds;
    const float half_width = static_cast<float>(package.canvas_width) * 0.5f;
    const float half_height = static_cast<float>(package.canvas_height) * 0.5f;
    bounds.min_x = -half_width - max_image_extent;
    bounds.min_y = -half_height - max_image_extent;
    bounds.max_x = half_width + max_image_extent;
    bounds.max_y = half_height + max_image_extent;
    bounds.center_x = (bounds.min_x + bounds.max_x) * 0.5f;
    bounds.center_y = (bounds.min_y + bounds.max_y) * 0.5f;

    const float dx = bounds.max_x - bounds.center_x;
    const float dy = bounds.max_y - bounds.center_y;
    bounds.radius_sq = dx * dx + dy * dy;
    return bounds;
}

std::vector<SplaDefoldBakedAnimation> build_baked_animations(
    const spriteloop::SplaPackage& package,
    const std::vector<SplaDefoldImageResource>& resources)
{
    std::vector<SplaDefoldBakedAnimation> baked_animations;
    baked_animations.reserve(package.animations.size());
    const float canvas_half_width = static_cast<float>(package.canvas_width) * 0.5f;
    const float canvas_half_height = static_cast<float>(package.canvas_height) * 0.5f;

    for (const spriteloop::SplaAnimation& animation : package.animations) {
        SplaDefoldBakedAnimation baked_animation;
        baked_animation.id = animation.id;
        baked_animation.frames.resize(animation.frames.size());

        for (std::size_t frame_index = 0; frame_index < animation.frames.size();
             ++frame_index) {
            const spriteloop::SplaFrame& frame = animation.frames[frame_index];
            SplaDefoldBakedFrame& baked = baked_animation.frames[frame_index];

            baked.vertices.reserve(frame.parts.size() * 4);
            for (const spriteloop::SplaFramePart& frame_part : frame.parts) {
                if (frame_part.part_index < 0 ||
                    frame_part.part_index >= static_cast<int>(package.parts.size())) {
                    continue;
                }

                const std::size_t part_index =
                    static_cast<std::size_t>(frame_part.part_index);
                if (part_index >= resources.size()) {
                    continue;
                }

                const spriteloop::SplaPart& part = package.parts[part_index];
                const SplaDefoldImageResource& image = resources[part_index];
                const spriteloop::SplaTransform& transform = frame_part.transform;
                const float width = static_cast<float>(image.width);
                const float height = static_cast<float>(image.height);
                const float scale_x = transform.scale_x;
                const float scale_y = transform.scale_y;
                const float radians = -transform.rotation_degrees * 3.14159265358979323846f / 180.0f;
                const float cos_r = std::cos(radians);
                const float sin_r = std::sin(radians);
                const float opacity = std::max(0.0f, std::min(transform.opacity, 1.0f));
                const spriteloop::SplaAtlasUvRect& uv = image.atlas_region.uv;

                const float source_x[4] = {0.0f, width, width, 0.0f};
                const float source_y[4] = {0.0f, 0.0f, height, height};
                for (int i = 0; i < 4; ++i) {
                    const float local_x = (source_x[i] - part.pivot.x) * scale_x;
                    const float local_y = (part.pivot.y - source_y[i]) * scale_y;
                    const float x = local_x * cos_r - local_y * sin_r;
                    const float y = local_x * sin_r + local_y * cos_r;
                    const float spla_x = transform.x + x;
                    const float spla_y =
                        (static_cast<float>(package.canvas_height) - transform.y) + y;

                    SplaDefoldBakedVertex vertex;
                    vertex.x = spla_x - canvas_half_width;
                    vertex.y = spla_y - canvas_half_height;
                    const float source_u = source_x[i] / width;
                    const float source_v = source_y[i] / height;
                    vertex.u = uv.u0 + (uv.u1 - uv.u0) * source_u;
                    vertex.v = uv.v0 + (uv.v1 - uv.v0) * source_v;
                    vertex.a = opacity;
                    baked.vertices.push_back(vertex);
                }
            }
        }

        baked_animations.push_back(std::move(baked_animation));
    }

    return baked_animations;
}

// Adds instance to the live registry if it is non-null and not already present.
void register_instance(SplaDefoldInstance* instance)
{
    if (instance == nullptr) {
        return;
    }

    std::vector<SplaDefoldInstance*>& instances = live_instances();
    if (std::find(instances.begin(), instances.end(), instance) == instances.end()) {
        instances.push_back(instance);
    }
}

// Removes instance from the live registry without destroying it.
void unregister_instance(SplaDefoldInstance* instance)
{
    std::vector<SplaDefoldInstance*>& instances = live_instances();
    instances.erase(std::remove(instances.begin(), instances.end(), instance), instances.end());
}

// Destroys all live instances during extension shutdown.
// graphics_context is used for texture cleanup before the process-local registry is cleared.
void destroy_all_instances(dmGraphics::HContext graphics_context)
{
    std::vector<SplaDefoldInstance*> instances = live_instances();
    live_instances().clear();

    for (SplaDefoldInstance* instance : instances) {
        if (instance != nullptr) {
            destroy_instance(instance, graphics_context);
        }
    }
    destroy_all_shared_package_resources(graphics_context);
}

// Returns the current live instance registry.
// The caller must not delete through this view; ownership remains with Lua/component code.
const std::vector<SplaDefoldInstance*>& registered_instances()
{
    return live_instances();
}

std::size_t shared_package_resource_count()
{
    return shared_resources().size();
}

const SplaDefoldSharedPackageResource* shared_package_resource_at(std::size_t index)
{
    const std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>>& resources =
        shared_resources();
    return index < resources.size() ? resources[index].get() : nullptr;
}

std::size_t image_resource_texture_bytes(const SplaDefoldSharedPackageResource& resource)
{
    return resource.atlas_texture != 0 ? resource.atlas_texture_bytes : 0;
}

const SplaDefoldRenderStats& render_stats()
{
    return global_render_stats();
}

void set_render_stats(const SplaDefoldRenderStats& stats)
{
    global_render_stats() = stats;
}

} // namespace spla_defold
