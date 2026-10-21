#include "spla_defold.h"
#include "spla_defold_lua.h"
#include "spla_defold_masks.h"

#include <dmsdk/sdk.h>
#include <dmsdk/dlib/configfile_gen.hpp>

// Native extension entry point for SpriteLoop.
//
// Defold loads this file through DM_DECLARE_EXTENSION. The callbacks below
// connect the Lua modules to the engine and clean up any live SpriteLoop
// instances when the extension is finalized.
namespace spla_defold {

} // namespace spla_defold

namespace {

ExtensionResult pre_render_spla(ExtensionParams*)
{
    spla_defold::begin_mask_frame();
    return EXTENSION_RESULT_OK;
}

// Register the frame boundary used by custom render-script mask preparation.
// PRE_RENDER only resets bookkeeping: GPU work must wait until BeginFrame.
dmExtension::Result app_initialize_spla(dmExtension::AppParams* params)
{
    (void)params;
    return dmExtension::RegisterCallback(dmExtension::CALLBACK_PRE_RENDER, pre_render_spla)
        ? dmExtension::RESULT_OK : dmExtension::RESULT_INIT_ERROR;
}

// Called when the extension is initialized for a running game.
// params supplies Defold's Lua state, where both SpriteLoop Lua modules are registered.
dmExtension::Result initialize_spla(dmExtension::Params* params)
{
    const int mask_cache_budget_mb = dmConfigFile::GetInt(
        params->m_ConfigFile, "spriteloop.mask_cache_budget_mb", 32);
    if (mask_cache_budget_mb < 0 || !spla_defold::set_mask_composition_cache_budget(
            static_cast<uint64_t>(mask_cache_budget_mb) * 1024 * 1024)) {
        dmLogWarning("spriteloop.mask_cache_budget_mb must be between 0 and 128 MiB (got %d); using 32 MiB",
                   mask_cache_budget_mb);
        spla_defold::set_mask_composition_cache_budget(32ULL * 1024 * 1024);
    }
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
    spla_defold::destroy_all_component_masks();
    spla_defold::destroy_all_instances(dmGraphics::GetInstalledContext());
    spla_defold::destroy_all_shared_package_resources(dmGraphics::GetInstalledContext());
    dmLogInfo("Finalized %s extension", SPLA_DEFOLD_EXTENSION_NAME);
    return dmExtension::RESULT_OK;
}

} // namespace

DM_DECLARE_EXTENSION(Spla, SPLA_DEFOLD_EXTENSION_NAME, app_initialize_spla, app_finalize_spla,
                     initialize_spla, 0, 0, finalize_spla)
