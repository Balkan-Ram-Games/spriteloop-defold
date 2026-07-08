#include "spla_defold.h"

#include "spriteloop/spla.hpp"

#include <dmsdk/dlib/log.h>

#include <algorithm>
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

std::size_t& default_max_pending_events()
{
    static std::size_t value = spriteloop::SplaPlayer::default_max_pending_events;
    return value;
}

SplaDefoldRenderStats& global_render_stats()
{
    static SplaDefoldRenderStats stats;
    return stats;
}

std::vector<spriteloop::SplaBakedImage> baked_images_from_resources(
    const spriteloop::SplaPackage& package,
    const std::vector<SplaDefoldImageResource>& resources,
    const spriteloop::SplaPartImageMap& image_map,
    const spriteloop::SplaSkinState& skin_state)
{
    spriteloop::SplaAtlas atlas;
    for (const SplaDefoldImageResource& resource : resources) {
        atlas.regions.push_back(resource.atlas_region);
    }
    return spriteloop::build_baked_images_from_atlas(package, atlas, image_map, skin_state);
}

std::vector<spriteloop::SplaBakedAnimation> baked_animations_from_resources(
    const spriteloop::SplaPackage& package,
    const std::vector<SplaDefoldImageResource>& resources,
    const spriteloop::SplaPartImageMap& image_map,
    const spriteloop::SplaSkinState& skin_state)
{
    spriteloop::SplaAtlas atlas;
    for (const SplaDefoldImageResource& resource : resources) {
        atlas.regions.push_back(resource.atlas_region);
    }
    return spriteloop::build_baked_animations(package, atlas, image_map, skin_state);
}

std::uint64_t hash_package_bytes(const std::uint8_t* bytes, std::size_t byte_count)
{
    constexpr std::uint64_t fnv_offset = 14695981039346656037ull;
    constexpr std::uint64_t fnv_prime = 1099511628211ull;
    std::uint64_t hash = fnv_offset;
    for (std::size_t i = 0; i < byte_count; ++i) {
        hash ^= bytes[i];
        hash *= fnv_prime;
    }
    return hash;
}

float clamp_tint_channel(float value)
{
    return std::max(0.0f, std::min(value, 1.0f));
}

void rebuild_instance_baked_data(SplaDefoldInstance& instance)
{
    const spriteloop::SplaPackage& package = instance.shared_resource != nullptr
                                                 ? instance.shared_resource->package
                                                 : instance.package;
    const std::vector<SplaDefoldImageResource>& resources =
        instance.shared_resource != nullptr ? instance.shared_resource->image_resources
                                            : instance.image_resources;
    const spriteloop::SplaPartImageMap& image_map =
        instance.shared_resource != nullptr ? instance.shared_resource->image_map
                                            : instance.image_map;
    const std::vector<spriteloop::SplaBakedImage> baked_images =
        baked_images_from_resources(package, resources, image_map, instance.skin_state);
    instance.bounds = spriteloop::calculate_baked_bounds(package, baked_images);
    instance.baked_animations =
        baked_animations_from_resources(package, resources, image_map, instance.skin_state);
    ++instance.skin_revision;
}

std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>>::iterator find_shared_resource(
    const std::string& path,
    std::uint64_t content_hash)
{
    std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>>& resources = shared_resources();
    return std::find_if(resources.begin(), resources.end(),
                        [&path, content_hash](const std::unique_ptr<SplaDefoldSharedPackageResource>& resource) {
                            return resource != nullptr && resource->path == path &&
                                   resource->content_hash == content_hash;
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
    instance->image_map = spriteloop::build_part_image_map_by_asset(instance->package);
    if (!upload_image_resources(dmGraphics::GetInstalledContext(), instance->image_resources,
                                instance->atlas_texture, instance->atlas_width,
                                instance->atlas_height, instance->atlas_texture_bytes, error)) {
        return nullptr;
    }
    rebuild_instance_baked_data(*instance);

    instance->player.reset(
        new spriteloop::SplaPlayer(instance->package, default_max_pending_events()));
    register_instance(instance.get());
    return instance.release();
}

SplaDefoldSharedPackageResource* acquire_shared_package_resource(const char* path,
                                                                 const std::uint8_t* bytes,
                                                                 std::size_t byte_count,
                                                                 std::string& error)
{
    const std::string cache_path = path != nullptr ? path : "";
    const std::uint64_t content_hash = hash_package_bytes(bytes, byte_count);
    auto existing = find_shared_resource(cache_path, content_hash);
    if (existing != shared_resources().end()) {
        ++(*existing)->ref_count;
        return existing->get();
    }

    auto package_result = spriteloop::load_package_from_memory(bytes, byte_count, cache_path);
    if (!package_result) {
        error = package_result.error().message;
        return nullptr;
    }

    std::unique_ptr<SplaDefoldSharedPackageResource> resource(new SplaDefoldSharedPackageResource);
    resource->path = cache_path;
    resource->byte_count = byte_count;
    resource->content_hash = content_hash;
    resource->package = std::move(package_result).value();

    if (!build_image_resources(resource->package, resource->image_resources, error)) {
        return nullptr;
    }
    resource->image_map = spriteloop::build_part_image_map_by_asset(resource->package);
    if (!upload_image_resources(dmGraphics::GetInstalledContext(), resource->image_resources,
                                resource->atlas_texture, resource->atlas_width,
                                resource->atlas_height, resource->atlas_texture_bytes, error)) {
        return nullptr;
    }
    const std::vector<spriteloop::SplaBakedImage> baked_images =
        baked_images_from_resources(resource->package, resource->image_resources,
                                    resource->image_map, {});
    resource->bounds = spriteloop::calculate_baked_bounds(resource->package, baked_images);
    resource->baked_animations =
        baked_animations_from_resources(resource->package, resource->image_resources,
                                        resource->image_map, {});

    resource->ref_count = 1;
    SplaDefoldSharedPackageResource* raw_resource = resource.get();
    shared_resources().push_back(std::move(resource));
    return raw_resource;
}

SplaDefoldSharedPackageResource* retain_shared_package_resource(const char* path)
{
    const std::string cache_path = path != nullptr ? path : "";
    std::vector<std::unique_ptr<SplaDefoldSharedPackageResource>>& resources = shared_resources();
    auto existing = std::find_if(resources.begin(), resources.end(),
                                 [&cache_path](const std::unique_ptr<SplaDefoldSharedPackageResource>& resource) {
                                     return resource != nullptr && resource->path == cache_path;
                                 });
    if (existing != resources.end()) {
        ++(*existing)->ref_count;
        return existing->get();
    }
    return nullptr;
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
    instance->player.reset(
        new spriteloop::SplaPlayer(shared_resource->package, default_max_pending_events()));
    rebuild_instance_baked_data(*instance);
    register_instance(instance.get());
    return instance.release();
}

void set_default_max_pending_events(int max_pending_events)
{
    default_max_pending_events() =
        max_pending_events > 0
            ? static_cast<std::size_t>(max_pending_events)
            : spriteloop::SplaPlayer::default_max_pending_events;
}

void log_event_queue_overflow_if_needed(SplaDefoldInstance& instance)
{
    if (instance.event_overflow_warning_logged || instance.player == nullptr ||
        instance.player->dropped_event_count() == 0) {
        return;
    }

    dmLogWarning(
        "SpriteLoop event queue overflow in '%s': oldest events are being dropped "
        "(max_pending_events=%u). Call consume_events() or dispatch_events() regularly, "
        "or increase spriteloop.max_pending_events",
        instance.path.empty() ? "<unknown>" : instance.path.c_str(),
        static_cast<uint32_t>(instance.player->max_pending_events()));
    instance.event_overflow_warning_logged = true;
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
    return instance.bounds;
}

const std::vector<SplaDefoldBakedAnimation>& instance_baked_animations(
    const SplaDefoldInstance& instance)
{
    return instance.baked_animations;
}

bool rebuild_instance_skin(SplaDefoldInstance& instance)
{
    rebuild_instance_baked_data(instance);
    return instance.baked_animations.size() == instance_package(instance).animations.size();
}

bool set_instance_skin(SplaDefoldInstance& instance, const std::string& skin_id)
{
    const spriteloop::SplaPackage& package = instance_package(instance);
    const int skin_index = spriteloop::find_skin_index_by_id_or_name(package, skin_id);
    if (skin_index < 0) {
        return false;
    }

    instance.skin_state.skin_index = skin_index;
    return rebuild_instance_skin(instance);
}

bool set_instance_variant(SplaDefoldInstance& instance,
                          const std::string& part_id,
                          const std::string& variant_id)
{
    const spriteloop::SplaPackage& package = instance_package(instance);
    const int part_index = spriteloop::find_part_index_by_id_key_or_name(package, part_id);
    const int variant_index = spriteloop::find_variant_index_by_id_key_or_name_for_part(
        package, part_index, variant_id);
    if (part_index < 0 || variant_index < 0) {
        return false;
    }

    if (instance.skin_state.variant_overrides_by_part.size() < package.parts.size()) {
        instance.skin_state.variant_overrides_by_part.assign(package.parts.size(), -1);
    }
    instance.skin_state.variant_overrides_by_part[static_cast<std::size_t>(part_index)] =
        variant_index;
    return rebuild_instance_skin(instance);
}

bool clear_instance_variant(SplaDefoldInstance& instance, const std::string& part_id)
{
    const spriteloop::SplaPackage& package = instance_package(instance);
    const int part_index = spriteloop::find_part_index_by_id_key_or_name(package, part_id);
    if (part_index < 0) {
        return false;
    }

    if (instance.skin_state.variant_overrides_by_part.size() < package.parts.size()) {
        instance.skin_state.variant_overrides_by_part.assign(package.parts.size(), -1);
    }
    instance.skin_state.variant_overrides_by_part[static_cast<std::size_t>(part_index)] = -1;
    return rebuild_instance_skin(instance);
}

void clear_instance_variants(SplaDefoldInstance& instance)
{
    instance.skin_state.variant_overrides_by_part.clear();
    rebuild_instance_skin(instance);
}

void set_instance_tint(SplaDefoldInstance& instance, float r, float g, float b)
{
    const float next_r = clamp_tint_channel(r);
    const float next_g = clamp_tint_channel(g);
    const float next_b = clamp_tint_channel(b);
    if (instance.tint_r == next_r && instance.tint_g == next_g && instance.tint_b == next_b) {
        return;
    }

    instance.tint_r = next_r;
    instance.tint_g = next_g;
    instance.tint_b = next_b;
    ++instance.tint_revision;
}

void clear_instance_tint(SplaDefoldInstance& instance)
{
    set_instance_tint(instance, 1.0f, 1.0f, 1.0f);
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
