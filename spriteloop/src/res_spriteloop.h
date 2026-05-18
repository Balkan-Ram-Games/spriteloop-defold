#pragma once

#include "spriteloop_ddf.h"

#include <dmsdk/gamesys/resources/res_material.h>

#include <string>

// Resource payload for compiled .spriteloop files.
//
// Bob compiles editor-authored .spriteloop files into .spriteloopc DDF resources. The Defold
// resource type loads that DDF, preloads the configured material, and hands this structure to
// component creation.
namespace spla_defold {

// Loaded component resource data shared by all component instances that reference the same
// .spriteloopc asset.
struct SpriteLoopResource {
    dmGameSystemDDF::SpriteLoopDesc* ddf = nullptr;
    dmGameSystem::MaterialResource* material = nullptr;
    std::string path;
};

} // namespace spla_defold
