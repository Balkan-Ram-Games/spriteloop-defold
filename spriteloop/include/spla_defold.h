#pragma once

#include "spriteloop/spla_package.hpp"
#include "spriteloop/spla_player.hpp"

#include <dmsdk/gameobject/gameobject.h>
#include <dmsdk/graphics/graphics.h>
#include <dmsdk/resource/resource.h>
#include <dmsdk/dlib/vmath.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#define SPLA_DEFOLD_EXTENSION_NAME "Spla"
#define SPLA_DEFOLD_MODULE_NAME "spla_native"
#define SPLA_DEFOLD_COMPONENT_MODULE_NAME "spriteloop_native"
#define SPLA_DEFOLD_COMPONENT_TYPE "spriteloopc"
#define SPLA_DEFOLD_VERSION "0.1.0"

// Shared runtime declarations for the Defold adapter.
//
// The C++ source files use these types to pass parsed .spla packages, Defold textures,
// component state, and Lua-owned handles between the resource, component, rendering,
// and scripting layers.
namespace spla_defold {

struct SplaDefoldComponent;
struct SplaDefoldSharedPackageResource;
struct SplaPackageBytesResource;
struct SpriteLoopResource;

// One decoded PNG part from a .spla package plus its Defold texture handle.
// rgba_pixels is temporary CPU-side upload data and is cleared after upload_image_resources.
struct SplaDefoldImageResource {
    std::string asset_path;
    int width = 0;
    int height = 0;
    std::size_t byte_count = 0;
    std::vector<std::uint8_t> rgba_pixels;
    dmGraphics::HTexture texture = 0;
};

// Runtime playback state for one SpriteLoop package instance.
// Lua handles and Defold components both own instances. Component-owned instances also mirror
// their game object and component-local transform so the renderer can emit world-space geometry.
struct SplaDefoldInstance {
    std::string path;
    std::size_t byte_count = 0;
    spriteloop::SplaPackage package;
    SplaDefoldSharedPackageResource* shared_resource = nullptr;
    std::unique_ptr<spriteloop::SplaPlayer> player;
    std::vector<SplaDefoldImageResource> image_resources;
    float x = 0.0f;
    float y = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    bool visible = true;
    dmGameObject::HInstance game_object = 0;
    dmVMath::Point3 local_position = dmVMath::Point3(0.0f, 0.0f, 0.0f);
    dmVMath::Quat local_rotation = dmVMath::Quat(0.0f, 0.0f, 0.0f, 1.0f);
    dmVMath::Vector3 local_scale = dmVMath::Vector3(1.0f, 1.0f, 1.0f);
};

// Component-side state for one .spriteloop component instance in a collection.
// The DDF resource supplies the configured package, material, default animation, and flags.
struct SplaDefoldComponent {
    SplaDefoldInstance* instance = nullptr;
    SpriteLoopResource* resource = nullptr;
    dmGameObject::HInstance game_object = 0;
    std::string package_path;
    std::string default_animation;
    float playback_rate = 1.0f;
    bool loop = true;
    bool visible = true;
    bool autoplay = true;
};

// Shared parsed package and uploaded texture set for component-owned SpriteLoop instances.
// Each component using the same package path increments ref_count; the textures are released when
// the final component releases the resource.
struct SplaDefoldSharedPackageResource {
    std::string path;
    std::size_t byte_count = 0;
    spriteloop::SplaPackage package;
    std::vector<SplaDefoldImageResource> image_resources;
    std::uint32_t ref_count = 0;
};

// Raw bytes for a built .spla package resource.
//
// Bob copies .spla packages referenced by SpriteLoop components into the build. The runtime
// registers a .spla resource type so components can acquire those packages through the normal
// Defold resource system instead of relying on game.project custom_resources.
struct SplaPackageBytesResource {
    std::string path;
    std::vector<std::uint8_t> bytes;
};

// Creates a runtime SpriteLoop instance from in-memory .spla bytes.
// path is used for logs/errors, bytes and byte_count identify the package payload, and error
// receives a human-readable failure message when nullptr is returned.
SplaDefoldInstance* create_instance_from_memory(const char* path,
                                                const std::uint8_t* bytes,
                                                std::size_t byte_count,
                                                std::string& error);

// Acquires a component-shared package resource from the process-local cache. The first acquire for
// a path parses the package and uploads textures; later acquires only increment ref_count.
SplaDefoldSharedPackageResource* acquire_shared_package_resource(const char* path,
                                                                 const std::uint8_t* bytes,
                                                                 std::size_t byte_count,
                                                                 std::string& error);

// Retains an existing shared package resource if it is already cached.
SplaDefoldSharedPackageResource* retain_shared_package_resource(const char* path);

// Creates per-component playback/transform state backed by a shared package resource.
SplaDefoldInstance* create_instance_from_shared_resource(
    SplaDefoldSharedPackageResource* shared_resource);

// Destroys an instance and its textures.
// graphics_context is the Defold graphics context used to release GPU image resources.
void destroy_instance(SplaDefoldInstance* instance,
                      dmGraphics::HContext graphics_context);

// Fallback cleanup for extension shutdown if any shared component resources remain live.
void destroy_all_shared_package_resources(dmGraphics::HContext graphics_context);

// Accessors that hide whether an instance owns its package/textures directly or uses D6 sharing.
const spriteloop::SplaPackage& instance_package(const SplaDefoldInstance& instance);
const std::vector<SplaDefoldImageResource>& instance_image_resources(
    const SplaDefoldInstance& instance);
std::vector<SplaDefoldImageResource>& instance_image_resources(SplaDefoldInstance& instance);

// Extracts and decodes PNG part images from an already parsed SpriteLoop package.
// resources is replaced with decoded image entries; error explains the first unsupported asset.
bool build_image_resources(const spriteloop::SplaPackage& package,
                           std::vector<SplaDefoldImageResource>& resources,
                           std::string& error);

// Uploads decoded image resources to Defold textures.
// On success CPU pixel buffers are released; on failure any partially uploaded textures are freed.
bool upload_image_resources(dmGraphics::HContext graphics_context,
                            std::vector<SplaDefoldImageResource>& resources,
                            std::string& error);

// Releases all Defold textures stored in resources and clears their native handles.
void destroy_image_resources(dmGraphics::HContext graphics_context,
                             std::vector<SplaDefoldImageResource>& resources);

// Tracks Lua-created and component-created instances so extension shutdown can clean them up.
void register_instance(SplaDefoldInstance* instance);
void unregister_instance(SplaDefoldInstance* instance);
void destroy_all_instances(dmGraphics::HContext graphics_context);
const std::vector<SplaDefoldInstance*>& registered_instances();

std::size_t shared_package_resource_count();
const SplaDefoldSharedPackageResource* shared_package_resource_at(std::size_t index);
std::size_t image_resource_texture_bytes(const std::vector<SplaDefoldImageResource>& resources);

} // namespace spla_defold
