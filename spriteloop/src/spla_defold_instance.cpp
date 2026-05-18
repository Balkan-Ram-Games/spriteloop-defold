#include "spla_defold.h"

#include "spriteloop/spla.hpp"

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
        destroy_image_resources(graphics_context, (*it)->image_resources);
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
                                error)) {
        return nullptr;
    }

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
                                error)) {
        return nullptr;
    }

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
        destroy_image_resources(graphics_context, instance->image_resources);
    }
    delete instance;
}

void destroy_all_shared_package_resources(dmGraphics::HContext graphics_context)
{
    for (const std::unique_ptr<SplaDefoldSharedPackageResource>& resource : shared_resources()) {
        if (resource != nullptr) {
            destroy_image_resources(graphics_context, resource->image_resources);
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

std::size_t image_resource_texture_bytes(const std::vector<SplaDefoldImageResource>& resources)
{
    std::size_t bytes = 0;
    for (const SplaDefoldImageResource& resource : resources) {
        if (resource.texture != 0 && resource.width > 0 && resource.height > 0) {
            bytes += static_cast<std::size_t>(resource.width) *
                     static_cast<std::size_t>(resource.height) * 4;
        }
    }
    return bytes;
}

} // namespace spla_defold
