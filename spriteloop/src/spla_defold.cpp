#include "spla_defold.h"
#include "spla_defold_lua.h"

#include <dmsdk/sdk.h>

// Native extension entry point for SpriteLoop.
//
// Defold loads this file through DM_DECLARE_EXTENSION. The callbacks below
// connect the Lua modules to the engine and clean up any live SpriteLoop
// instances when the extension is finalized.
namespace spla_defold {

} // namespace spla_defold

namespace {

// Called once during app-level extension startup.
// params is unused because SpriteLoop has no app-global state to initialize.
dmExtension::Result app_initialize_spla(dmExtension::AppParams* params)
{
    (void)params;
    return dmExtension::RESULT_OK;
}

// Called when the extension is initialized for a running game.
// params supplies Defold's Lua state, where both SpriteLoop Lua modules are registered.
dmExtension::Result initialize_spla(dmExtension::Params* params)
{
    spla_defold::register_lua_module(params->m_L);
    dmLogInfo("Registered %s extension module '%s'", SPLA_DEFOLD_EXTENSION_NAME,
              SPLA_DEFOLD_MODULE_NAME);
    return dmExtension::RESULT_OK;
}

// Called once during app-level extension shutdown.
// params is unused because all runtime-owned resources are released in finalize_spla.
dmExtension::Result app_finalize_spla(dmExtension::AppParams* params)
{
    (void)params;
    return dmExtension::RESULT_OK;
}

// Called when the extension shuts down.
// params is unused; the installed graphics context is used to release any textures still owned
// by Lua-created or component-created SpriteLoop instances.
dmExtension::Result finalize_spla(dmExtension::Params* params)
{
    (void)params;
    spla_defold::destroy_all_instances(dmGraphics::GetInstalledContext());
    spla_defold::destroy_all_shared_package_resources(dmGraphics::GetInstalledContext());
    dmLogInfo("Finalized %s extension", SPLA_DEFOLD_EXTENSION_NAME);
    return dmExtension::RESULT_OK;
}

} // namespace

DM_DECLARE_EXTENSION(Spla, SPLA_DEFOLD_EXTENSION_NAME, app_initialize_spla, app_finalize_spla,
                     initialize_spla, 0, 0, finalize_spla)
