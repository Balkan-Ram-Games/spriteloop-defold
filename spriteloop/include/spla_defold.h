#pragma once

#include "spriteloop/spla_baked.hpp"
#include "spriteloop/spla_package.hpp"
#include "spriteloop/spla_player.hpp"
#include "spriteloop/spla_atlas.hpp"

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

// One decoded PNG part from a .spla package plus its placement inside the shared atlas texture.
// rgba_pixels is temporary CPU-side upload data and is cleared after upload_image_resources.
struct SplaDefoldImageResource {
    std::string asset_path;
    int width = 0;
    int height = 0;
    std::size_t byte_count = 0;
    std::vector<std::uint8_t> rgba_pixels;
    spriteloop::SplaAtlasRegion atlas_region;
};

using SplaDefoldBounds = spriteloop::SplaBounds;
using SplaDefoldBakedVertex = spriteloop::SplaBakedVertex;
using SplaDefoldBakedFrame = spriteloop::SplaBakedFrame;
using SplaDefoldBakedAnimation = spriteloop::SplaBakedAnimation;

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
    dmGraphics::HTexture atlas_texture = 0;
    int atlas_width = 0;
    int atlas_height = 0;
    std::size_t atlas_texture_bytes = 0;
    SplaDefoldBounds bounds;
    std::vector<SplaDefoldBakedAnimation> baked_animations;
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
    dmGraphics::HTexture atlas_texture = 0;
    int atlas_width = 0;
    int atlas_height = 0;
    std::size_t atlas_texture_bytes = 0;
    SplaDefoldBounds bounds;
    std::vector<SplaDefoldBakedAnimation> baked_animations;
    std::uint32_t ref_count = 0;
};

struct SplaDefoldRenderStats {
    std::uint32_t component_count = 0;
    std::uint32_t render_candidates = 0;
    std::uint32_t frustum_visible = 0;
    std::uint32_t frustum_culled = 0;
    std::uint32_t vertices_generated = 0;
    std::uint32_t indices_generated = 0;
    std::uint32_t quads_generated = 0;
    std::uint32_t render_objects = 0;
    std::uint32_t batch_flushes = 0;
    std::uint32_t vertex_cache_hits = 0;
    std::uint32_t vertex_cache_misses = 0;
    std::uint32_t geometry_reused = 0;
    std::uint32_t vertex_bulk_copies = 0;
    std::uint32_t index_pattern_rebuilds = 0;
    std::uint32_t index_uploads = 0;
    std::uint32_t vertex_uploads = 0;
    std::uint32_t slot_count = 0;
    std::uint32_t dirty_slots = 0;
    std::uint32_t slot_vertex_copies = 0;
    std::uint32_t full_vertex_uploads = 0;
    std::uint32_t slot_layout_rebuilds = 0;
    std::uint32_t index_layout_uploads = 0;
    std::uint32_t reuse_rejected = 0;
    std::uint32_t reuse_reject_invalid_previous = 0;
    std::uint32_t reuse_reject_split_batch = 0;
    std::uint32_t reuse_reject_signature_size = 0;
    std::uint32_t reuse_reject_signature_entry = 0;
    std::uint32_t reuse_reject_cache_miss = 0;
    std::uint32_t playback_updates = 0;
    std::uint32_t render_entries_submitted = 0;
    std::uint32_t buffer_uploads = 0;
    std::uint64_t bytes_uploaded = 0;
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
dmGraphics::HTexture instance_atlas_texture(const SplaDefoldInstance& instance);
std::size_t instance_atlas_texture_bytes(const SplaDefoldInstance& instance);
const SplaDefoldBounds& instance_bounds(const SplaDefoldInstance& instance);
const std::vector<SplaDefoldBakedAnimation>& instance_baked_animations(
    const SplaDefoldInstance& instance);

// Extracts and decodes PNG part images from an already parsed SpriteLoop package.
// resources is replaced with decoded image entries; error explains the first unsupported asset.
bool build_image_resources(const spriteloop::SplaPackage& package,
                           std::vector<SplaDefoldImageResource>& resources,
                           std::string& error);

// Packs decoded image resources into one Defold atlas texture.
// On success CPU pixel buffers are released and per-part atlas regions are stored.
bool upload_image_resources(dmGraphics::HContext graphics_context,
                            std::vector<SplaDefoldImageResource>& resources,
                            dmGraphics::HTexture& atlas_texture,
                            int& atlas_width,
                            int& atlas_height,
                            std::size_t& atlas_texture_bytes,
                            std::string& error);

// Releases one Defold atlas texture and clears its native handle.
void destroy_atlas_texture(dmGraphics::HContext graphics_context,
                           dmGraphics::HTexture& atlas_texture);

// Tracks Lua-created and component-created instances so extension shutdown can clean them up.
void register_instance(SplaDefoldInstance* instance);
void unregister_instance(SplaDefoldInstance* instance);
void destroy_all_instances(dmGraphics::HContext graphics_context);
const std::vector<SplaDefoldInstance*>& registered_instances();

std::size_t shared_package_resource_count();
const SplaDefoldSharedPackageResource* shared_package_resource_at(std::size_t index);
std::size_t image_resource_texture_bytes(const SplaDefoldSharedPackageResource& resource);
const SplaDefoldRenderStats& render_stats();
void set_render_stats(const SplaDefoldRenderStats& stats);

} // namespace spla_defold
