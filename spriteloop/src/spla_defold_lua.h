#pragma once

struct lua_State;

// Lua binding declarations for the SpriteLoop Defold adapter.
//
// The implementation registers two global Lua tables:
// - spla_native: low-level handle API used by spriteloop/spla.lua.
// - spriteloop_native: component URL API used by spriteloop/spriteloop.lua.
namespace spla_defold {

// Registers the native Lua modules into lua_state.
// lua_state is supplied by Defold during extension initialization.
void register_lua_module(lua_State* lua_state);

} // namespace spla_defold
