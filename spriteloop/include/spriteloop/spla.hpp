#pragma once

#include "spriteloop/spla_atlas.hpp"
#include "spriteloop/spla_baked.hpp"
#include "spriteloop/spla_loader.hpp"
#include "spriteloop/spla_package.hpp"
#include "spriteloop/spla_player.hpp"
#include "spriteloop/spla_result.hpp"

#include <string_view>

namespace spriteloop {

std::string_view sdk_name() noexcept;
std::string_view sdk_version() noexcept;

} // namespace spriteloop
