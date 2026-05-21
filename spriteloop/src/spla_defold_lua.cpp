#include "spla_defold_lua.h"

#include "spla_defold.h"

#include <dmsdk/sdk.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Lua bindings for SpriteLoop.
//
// This file exposes two APIs:
// - spla_native: a low-level handle API for manually loaded .spla packages.
// - spriteloop_native: a component URL API for normal Defold component scripts.
namespace {

constexpr const char* instance_metatable = "spla.instance";

// Reads the raw userdata storage that holds a SplaDefoldInstance pointer.
// lua_state is the active Lua state and index is the stack index to validate.
spla_defold::SplaDefoldInstance** check_instance_storage(lua_State* lua_state, int index)
{
    return static_cast<spla_defold::SplaDefoldInstance**>(
        luaL_checkudata(lua_state, index, instance_metatable));
}

// Returns a live instance pointer from a Lua handle or raises a Lua error.
// index is the stack position of the userdata handle.
spla_defold::SplaDefoldInstance* check_instance(lua_State* lua_state, int index)
{
    spla_defold::SplaDefoldInstance** storage = check_instance_storage(lua_state, index);
    if (storage == nullptr || *storage == nullptr) {
        luaL_error(lua_state, "SpriteLoop instance has been destroyed");
        return nullptr;
    }

    return *storage;
}

// Pushes a native instance as Lua userdata and attaches the instance metatable.
// Ownership transfers to Lua until destroy or __gc runs.
void push_instance(lua_State* lua_state, spla_defold::SplaDefoldInstance* instance)
{
    void* userdata = lua_newuserdata(lua_state, sizeof(spla_defold::SplaDefoldInstance*));
    auto** storage = static_cast<spla_defold::SplaDefoldInstance**>(userdata);
    *storage = instance;

    luaL_getmetatable(lua_state, instance_metatable);
    lua_setmetatable(lua_state, -2);
}

// Destroys the native instance stored at a Lua stack index and clears the userdata pointer.
// It is used by both explicit destroy and Lua garbage collection.
void destroy_instance_storage(lua_State* lua_state, int index)
{
    spla_defold::SplaDefoldInstance** storage = check_instance_storage(lua_state, index);
    if (storage != nullptr && *storage != nullptr) {
        spla_defold::destroy_instance(*storage, dmGraphics::GetInstalledContext());
        *storage = nullptr;
    }
}

// Lua: spla_native.version() -> string.
int version(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 1);
    lua_pushstring(lua_state, SPLA_DEFOLD_VERSION);
    return 1;
}

// Lua: spla_native.load_bytes(path, bytes) -> userdata.
// path is used for diagnostics and bytes is the raw .spla package payload from sys.load_resource.
int load_bytes(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 1);

    const char* path = luaL_checkstring(lua_state, 1);
    size_t byte_count = 0;
    const char* bytes = luaL_checklstring(lua_state, 2, &byte_count);

    dmLogInfo("spla.load_bytes('%s', %u bytes)", path,
              static_cast<uint32_t>(byte_count));

    std::string error;
    spla_defold::SplaDefoldInstance* instance = spla_defold::create_instance_from_memory(
        path, reinterpret_cast<const std::uint8_t*>(bytes), byte_count, error);
    if (instance == nullptr) {
        return luaL_error(lua_state, "failed to load SpriteLoop package '%s': %s", path,
                          error.c_str());
    }

    const std::size_t part_count = instance->package.parts.size();
    const std::size_t animation_count = instance->package.animations.size();
    const std::size_t asset_count = instance->package.assets.size();
    const std::size_t image_resource_count = instance->image_resources.size();
    const std::string package_name = instance->package.name;

    dmLogInfo("loaded SpriteLoop package '%s': %u parts, %u animations, %u assets, %u image resources",
              package_name.c_str(), static_cast<uint32_t>(part_count),
              static_cast<uint32_t>(animation_count), static_cast<uint32_t>(asset_count),
              static_cast<uint32_t>(image_resource_count));

    push_instance(lua_state, instance);

    return 1;
}

// Lua: spla_native.destroy(handle).
// Releases the package instance and any textures owned by the handle.
int destroy(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    destroy_instance_storage(lua_state, 1);
    return 0;
}

// Lua __gc metamethod for low-level package handles.
int gc_instance(lua_State* lua_state)
{
    destroy_instance_storage(lua_state, 1);
    return 0;
}

// Lua: spla_native.play(handle, animation_id) -> boolean.
int play(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 1);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    const char* animation_id = luaL_checkstring(lua_state, 2);
    const bool played = instance->player->play(animation_id);
    lua_pushboolean(lua_state, played ? 1 : 0);
    return 1;
}

// Lua: spla_native.stop(handle).
int stop(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    instance->player->stop();
    return 0;
}

// Lua: spla_native.update(handle, delta_seconds).
int update(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    const float delta_seconds = static_cast<float>(luaL_checknumber(lua_state, 2));
    instance->player->update(delta_seconds);
    return 0;
}

// Lua: spla_native.set_time(handle, seconds).
int set_time(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    const float seconds = static_cast<float>(luaL_checknumber(lua_state, 2));
    instance->player->set_time(seconds);
    return 0;
}

// Lua: spla_native.set_frame(handle, frame_index).
int set_frame(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    const int frame_index = static_cast<int>(luaL_checknumber(lua_state, 2));
    instance->player->set_frame(frame_index);
    return 0;
}

// Lua: spla_native.set_position(handle, x, y).
// This affects the standalone handle renderer path, not component transforms.
int set_position(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    instance->x = static_cast<float>(luaL_checknumber(lua_state, 2));
    instance->y = static_cast<float>(luaL_checknumber(lua_state, 3));
    return 0;
}

// Lua: spla_native.set_scale(handle, scale_x, scale_y?).
// scale_y defaults to scale_x when omitted.
int set_scale(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    instance->scale_x = static_cast<float>(luaL_checknumber(lua_state, 2));
    instance->scale_y = lua_isnumber(lua_state, 3)
                            ? static_cast<float>(lua_tonumber(lua_state, 3))
                            : instance->scale_x;
    return 0;
}

// Lua: spla_native.set_visible(handle, visible).
int set_visible(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    instance->visible = lua_toboolean(lua_state, 2) != 0;
    return 0;
}

// Lua: spla_native.get_info(handle) -> table.
// Returns package, playback, transform, and image-resource details for diagnostics.
int get_info(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 1);
    spla_defold::SplaDefoldInstance* instance = check_instance(lua_state, 1);
    const spriteloop::SplaAnimation* animation = instance->player->current_animation();
    const spriteloop::SplaFrame* frame = instance->player->current_frame();

    lua_newtable(lua_state);
    lua_pushstring(lua_state, instance->path.c_str());
    lua_setfield(lua_state, -2, "path");
    lua_pushstring(lua_state, instance->package.name.c_str());
    lua_setfield(lua_state, -2, "name");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(instance->byte_count));
    lua_setfield(lua_state, -2, "byte_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(instance->package.parts.size()));
    lua_setfield(lua_state, -2, "part_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(instance->package.animations.size()));
    lua_setfield(lua_state, -2, "animation_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(instance->package.assets.size()));
    lua_setfield(lua_state, -2, "asset_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(instance->image_resources.size()));
    lua_setfield(lua_state, -2, "image_resource_count");
    lua_pushinteger(lua_state, instance->package.canvas_width);
    lua_setfield(lua_state, -2, "canvas_width");
    lua_pushinteger(lua_state, instance->package.canvas_height);
    lua_setfield(lua_state, -2, "canvas_height");
    lua_pushboolean(lua_state, instance->player->is_playing() ? 1 : 0);
    lua_setfield(lua_state, -2, "playing");
    lua_pushnumber(lua_state, instance->player->time());
    lua_setfield(lua_state, -2, "time");
    lua_pushinteger(lua_state, instance->player->current_frame_index());
    lua_setfield(lua_state, -2, "frame_index");
    lua_pushnumber(lua_state, instance->x);
    lua_setfield(lua_state, -2, "x");
    lua_pushnumber(lua_state, instance->y);
    lua_setfield(lua_state, -2, "y");
    lua_pushnumber(lua_state, instance->scale_x);
    lua_setfield(lua_state, -2, "scale_x");
    lua_pushnumber(lua_state, instance->scale_y);
    lua_setfield(lua_state, -2, "scale_y");
    lua_pushboolean(lua_state, instance->visible ? 1 : 0);
    lua_setfield(lua_state, -2, "visible");

    if (animation != nullptr) {
        lua_pushstring(lua_state, animation->id.c_str());
        lua_setfield(lua_state, -2, "animation_id");
        lua_pushstring(lua_state, animation->name.c_str());
        lua_setfield(lua_state, -2, "animation_name");
        lua_pushnumber(lua_state, animation->fps);
        lua_setfield(lua_state, -2, "fps");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(animation->frames.size()));
        lua_setfield(lua_state, -2, "frame_count");
        lua_pushboolean(lua_state, instance->player->effective_loop() ? 1 : 0);
        lua_setfield(lua_state, -2, "loop");
    }

    if (frame != nullptr) {
        lua_pushinteger(lua_state, frame->index);
        lua_setfield(lua_state, -2, "frame_manifest_index");
        lua_pushinteger(lua_state, frame->source_frame);
        lua_setfield(lua_state, -2, "source_frame");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(frame->parts.size()));
        lua_setfield(lua_state, -2, "frame_part_count");
    }

    return 1;
}

// Low-level Lua module table for manually loaded package handles.
const luaL_reg module_methods[] = {
    {"version", version},
    {"load_bytes", load_bytes},
    {"destroy", destroy},
    {"play", play},
    {"stop", stop},
    {"update", update},
    {"set_time", set_time},
    {"set_frame", set_frame},
    {"set_position", set_position},
    {"set_scale", set_scale},
    {"set_visible", set_visible},
    {"get_info", get_info},
    {0, 0},
};

// Resolves a Defold SpriteLoop component from a Lua URL argument.
// index is the stack index of the URL/hash/string accepted by Defold's component lookup helper.
spla_defold::SplaDefoldComponent* check_component(lua_State* lua_state, int index)
{
    spla_defold::SplaDefoldComponent* component = nullptr;
    dmScript::GetComponentFromLua(lua_state, index, SPLA_DEFOLD_COMPONENT_TYPE, 0,
                                  reinterpret_cast<void**>(&component), 0);
    if (component == nullptr || component->instance == nullptr) {
        luaL_error(lua_state, "SpriteLoop component has not been created");
        return nullptr;
    }

    return component;
}

// Reads the optional play options table. Currently supported:
// { loop = boolean }.
bool read_play_options(lua_State* lua_state, int index, bool current_loop, bool& loop)
{
    loop = current_loop;
    if (lua_gettop(lua_state) < index || lua_isnil(lua_state, index)) {
        return true;
    }

    if (!lua_istable(lua_state, index)) {
        luaL_error(lua_state, "SpriteLoop play_anim options must be a table");
        return false;
    }

    lua_getfield(lua_state, index, "loop");
    if (!lua_isnil(lua_state, -1)) {
        if (!lua_isboolean(lua_state, -1)) {
            lua_pop(lua_state, 1);
            luaL_error(lua_state, "SpriteLoop play_anim options.loop must be a boolean");
            return false;
        }
        loop = lua_toboolean(lua_state, -1) != 0;
    }
    lua_pop(lua_state, 1);
    return true;
}

// Lua: spriteloop_native.play_anim(url, animation_id, options?) -> boolean.
// options is an optional table, for example { loop = false }.
int component_play_anim(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 1);
    spla_defold::SplaDefoldComponent* component = check_component(lua_state, 1);
    const char* animation_id = luaL_checkstring(lua_state, 2);
    bool loop = component->loop;
    read_play_options(lua_state, 3, component->loop, loop);
    component->instance->player->set_loop_override(loop);
    const bool played = component->instance->player->play(animation_id);
    if (played) {
        component->default_animation = animation_id;
        component->loop = loop;
    } else {
        component->instance->player->set_loop_override(component->loop);
    }
    lua_pushboolean(lua_state, played ? 1 : 0);
    return 1;
}

// Lua: spriteloop_native.stop_anim(url).
int component_stop_anim(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldComponent* component = check_component(lua_state, 1);
    component->instance->player->stop();
    return 0;
}

// Lua: spriteloop_native.set_time(url, seconds).
int component_set_time(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldComponent* component = check_component(lua_state, 1);
    component->instance->player->set_time(static_cast<float>(luaL_checknumber(lua_state, 2)));
    return 0;
}

// Lua: spriteloop_native.set_frame(url, frame_index).
int component_set_frame(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldComponent* component = check_component(lua_state, 1);
    component->instance->player->set_frame(static_cast<int>(luaL_checknumber(lua_state, 2)));
    return 0;
}

// Lua: spriteloop_native.set_playback_rate(url, rate).
int component_set_playback_rate(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldComponent* component = check_component(lua_state, 1);
    component->playback_rate = static_cast<float>(luaL_checknumber(lua_state, 2));
    return 0;
}

// Lua: spriteloop_native.set_visible(url, visible).
int component_set_visible(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 0);
    spla_defold::SplaDefoldComponent* component = check_component(lua_state, 1);
    component->visible = lua_toboolean(lua_state, 2) != 0;
    component->instance->visible = component->visible;
    return 0;
}

// Lua: spriteloop_native.debug_destroy_component(url) -> boolean.
// Releases one component runtime instance while leaving the Defold component shell in place.
int component_debug_destroy_component(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 1);
    spla_defold::SplaDefoldComponent* component = nullptr;
    dmScript::GetComponentFromLua(lua_state, 1, SPLA_DEFOLD_COMPONENT_TYPE, 0,
                                  reinterpret_cast<void**>(&component), 0);
    if (component == nullptr) {
        return luaL_error(lua_state, "SpriteLoop component has not been created");
    }

    if (component->instance == nullptr) {
        lua_pushboolean(lua_state, 0);
        return 1;
    }

    spla_defold::destroy_instance(component->instance, dmGraphics::GetInstalledContext());
    component->instance = nullptr;
    component->visible = false;
    lua_pushboolean(lua_state, 1);
    return 1;
}

// Lua: spriteloop_native.get_info(url) -> table.
// Returns component package/playback details for debug scripts and sample output.
int component_get_info(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 1);
    spla_defold::SplaDefoldComponent* component = check_component(lua_state, 1);
    spla_defold::SplaDefoldInstance* instance = component->instance;
    const spriteloop::SplaPackage& package = spla_defold::instance_package(*instance);
    const std::vector<spla_defold::SplaDefoldImageResource>& image_resources =
        spla_defold::instance_image_resources(*instance);
    const spriteloop::SplaAnimation* animation = instance->player->current_animation();
    const spriteloop::SplaFrame* frame = instance->player->current_frame();

    lua_newtable(lua_state);
    lua_pushstring(lua_state, component->package_path.c_str());
    lua_setfield(lua_state, -2, "path");
    lua_pushstring(lua_state, package.name.c_str());
    lua_setfield(lua_state, -2, "name");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(package.parts.size()));
    lua_setfield(lua_state, -2, "part_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(package.animations.size()));
    lua_setfield(lua_state, -2, "animation_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(image_resources.size()));
    lua_setfield(lua_state, -2, "image_resource_count");
    lua_pushinteger(lua_state,
                    static_cast<lua_Integer>(spla_defold::instance_atlas_texture_bytes(*instance)));
    lua_setfield(lua_state, -2, "atlas_texture_bytes");
    lua_pushinteger(lua_state, package.canvas_width);
    lua_setfield(lua_state, -2, "canvas_width");
    lua_pushinteger(lua_state, package.canvas_height);
    lua_setfield(lua_state, -2, "canvas_height");
    lua_pushboolean(lua_state, instance->player->is_playing() ? 1 : 0);
    lua_setfield(lua_state, -2, "playing");
    lua_pushnumber(lua_state, instance->player->time());
    lua_setfield(lua_state, -2, "time");
    lua_pushinteger(lua_state, instance->player->current_frame_index());
    lua_setfield(lua_state, -2, "frame_index");
    lua_pushnumber(lua_state, component->playback_rate);
    lua_setfield(lua_state, -2, "playback_rate");
    lua_pushboolean(lua_state, component->loop ? 1 : 0);
    lua_setfield(lua_state, -2, "loop");
    lua_pushboolean(lua_state, component->visible ? 1 : 0);
    lua_setfield(lua_state, -2, "visible");
    lua_pushboolean(lua_state, component->autoplay ? 1 : 0);
    lua_setfield(lua_state, -2, "autoplay");

    if (animation != nullptr) {
        lua_pushstring(lua_state, animation->id.c_str());
        lua_setfield(lua_state, -2, "animation_id");
        lua_pushstring(lua_state, animation->name.c_str());
        lua_setfield(lua_state, -2, "animation_name");
        lua_pushnumber(lua_state, animation->fps);
        lua_setfield(lua_state, -2, "fps");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(animation->frames.size()));
        lua_setfield(lua_state, -2, "frame_count");
        lua_pushboolean(lua_state, instance->player->effective_loop() ? 1 : 0);
        lua_setfield(lua_state, -2, "loop");
    }

    if (frame != nullptr) {
        lua_pushinteger(lua_state, frame->index);
        lua_setfield(lua_state, -2, "frame_manifest_index");
        lua_pushinteger(lua_state, frame->source_frame);
        lua_setfield(lua_state, -2, "source_frame");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(frame->parts.size()));
        lua_setfield(lua_state, -2, "frame_part_count");
    }

    return 1;
}

// Lua: spriteloop_native.get_cache_info() -> table.
// Returns component shared-resource cache information for diagnostics.
int component_get_cache_info(lua_State* lua_state)
{
    DM_LUA_STACK_CHECK(lua_state, 1);

    std::size_t total_ref_count = 0;
    std::size_t total_texture_count = 0;
    std::size_t total_texture_bytes = 0;
    const std::size_t entry_count = spla_defold::shared_package_resource_count();

    lua_newtable(lua_state);
    lua_pushinteger(lua_state, static_cast<lua_Integer>(entry_count));
    lua_setfield(lua_state, -2, "entry_count");

    lua_newtable(lua_state);
    for (std::size_t i = 0; i < entry_count; ++i) {
        const spla_defold::SplaDefoldSharedPackageResource* resource =
            spla_defold::shared_package_resource_at(i);
        if (resource == nullptr) {
            continue;
        }

        const std::size_t texture_count = resource->atlas_texture != 0 ? 1 : 0;
        const std::size_t texture_bytes =
            spla_defold::image_resource_texture_bytes(*resource);
        total_ref_count += resource->ref_count;
        total_texture_count += texture_count;
        total_texture_bytes += texture_bytes;

        lua_newtable(lua_state);
        lua_pushstring(lua_state, resource->path.c_str());
        lua_setfield(lua_state, -2, "path");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(resource->ref_count));
        lua_setfield(lua_state, -2, "ref_count");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(resource->package.parts.size()));
        lua_setfield(lua_state, -2, "part_count");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(resource->package.animations.size()));
        lua_setfield(lua_state, -2, "animation_count");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(texture_count));
        lua_setfield(lua_state, -2, "texture_count");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(texture_bytes));
        lua_setfield(lua_state, -2, "texture_bytes");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(resource->atlas_width));
        lua_setfield(lua_state, -2, "atlas_width");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(resource->atlas_height));
        lua_setfield(lua_state, -2, "atlas_height");
        lua_rawseti(lua_state, -2, static_cast<int>(i + 1));
    }
    lua_setfield(lua_state, -2, "entries");

    lua_pushinteger(lua_state, static_cast<lua_Integer>(total_ref_count));
    lua_setfield(lua_state, -2, "total_ref_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(total_texture_count));
    lua_setfield(lua_state, -2, "total_texture_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(total_texture_bytes));
    lua_setfield(lua_state, -2, "total_texture_bytes");

    const spla_defold::SplaDefoldRenderStats& stats = spla_defold::render_stats();
    lua_newtable(lua_state);
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.component_count));
    lua_setfield(lua_state, -2, "component_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.render_candidates));
    lua_setfield(lua_state, -2, "render_candidates");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.frustum_visible));
    lua_setfield(lua_state, -2, "frustum_visible");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.frustum_culled));
    lua_setfield(lua_state, -2, "frustum_culled");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.vertices_generated));
    lua_setfield(lua_state, -2, "vertices_generated");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.indices_generated));
    lua_setfield(lua_state, -2, "indices_generated");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.quads_generated));
    lua_setfield(lua_state, -2, "quads_generated");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.render_objects));
    lua_setfield(lua_state, -2, "render_objects");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.batch_flushes));
    lua_setfield(lua_state, -2, "batch_flushes");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.vertex_cache_hits));
    lua_setfield(lua_state, -2, "vertex_cache_hits");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.vertex_cache_misses));
    lua_setfield(lua_state, -2, "vertex_cache_misses");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.geometry_reused));
    lua_setfield(lua_state, -2, "geometry_reused");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.vertex_bulk_copies));
    lua_setfield(lua_state, -2, "vertex_bulk_copies");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.index_pattern_rebuilds));
    lua_setfield(lua_state, -2, "index_pattern_rebuilds");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.index_uploads));
    lua_setfield(lua_state, -2, "index_uploads");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.vertex_uploads));
    lua_setfield(lua_state, -2, "vertex_uploads");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.slot_count));
    lua_setfield(lua_state, -2, "slot_count");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.dirty_slots));
    lua_setfield(lua_state, -2, "dirty_slots");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.slot_vertex_copies));
    lua_setfield(lua_state, -2, "slot_vertex_copies");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.full_vertex_uploads));
    lua_setfield(lua_state, -2, "full_vertex_uploads");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.slot_layout_rebuilds));
    lua_setfield(lua_state, -2, "slot_layout_rebuilds");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.index_layout_uploads));
    lua_setfield(lua_state, -2, "index_layout_uploads");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.reuse_rejected));
    lua_setfield(lua_state, -2, "reuse_rejected");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.reuse_reject_invalid_previous));
    lua_setfield(lua_state, -2, "reuse_reject_invalid_previous");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.reuse_reject_split_batch));
    lua_setfield(lua_state, -2, "reuse_reject_split_batch");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.reuse_reject_signature_size));
    lua_setfield(lua_state, -2, "reuse_reject_signature_size");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.reuse_reject_signature_entry));
    lua_setfield(lua_state, -2, "reuse_reject_signature_entry");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.reuse_reject_cache_miss));
    lua_setfield(lua_state, -2, "reuse_reject_cache_miss");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.playback_updates));
    lua_setfield(lua_state, -2, "playback_updates");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.render_entries_submitted));
    lua_setfield(lua_state, -2, "render_entries_submitted");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.buffer_uploads));
    lua_setfield(lua_state, -2, "buffer_uploads");
    lua_pushinteger(lua_state, static_cast<lua_Integer>(stats.bytes_uploaded));
    lua_setfield(lua_state, -2, "bytes_uploaded");
    lua_setfield(lua_state, -2, "render_stats");

    return 1;
}

// Component-oriented Lua module table.
const luaL_reg component_module_methods[] = {
    {"play_anim", component_play_anim},
    {"stop_anim", component_stop_anim},
    {"set_time", component_set_time},
    {"set_frame", component_set_frame},
    {"set_playback_rate", component_set_playback_rate},
    {"set_visible", component_set_visible},
    {"debug_destroy_component", component_debug_destroy_component},
    {"get_info", component_get_info},
    {"get_cache_info", component_get_cache_info},
    {0, 0},
};

} // namespace

namespace spla_defold {

// Registers the low-level and component-oriented Lua modules.
// lua_state is supplied by Defold during extension initialization; the stack top is restored
// before returning.
void register_lua_module(lua_State* lua_state)
{
    const int top = lua_gettop(lua_state);

    if (luaL_newmetatable(lua_state, instance_metatable)) {
        lua_pushcfunction(lua_state, gc_instance);
        lua_setfield(lua_state, -2, "__gc");
    }
    lua_pop(lua_state, 1);

    luaL_register(lua_state, SPLA_DEFOLD_MODULE_NAME, module_methods);
    lua_pop(lua_state, 1);
    luaL_register(lua_state, SPLA_DEFOLD_COMPONENT_MODULE_NAME, component_module_methods);
    lua_pop(lua_state, 1);
    assert(top == lua_gettop(lua_state));
}

} // namespace spla_defold
