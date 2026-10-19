#pragma once

#include "spriteloop/spla_result.hpp"

#include <cstdint>
#include <vector>

namespace spriteloop {

// Includes level zero. Each dimension halves with floor division, stopping at 1.
// Invalid (nonpositive) dimensions have no levels.
std::uint8_t rgba_mip_level_count(int width, int height) noexcept;

// Produce one RGBA8 mip level, sized max(1, width/2) x max(1, height/2).
// Input must already be premultiplied; no color-space conversion is performed.
// Average each 2x2 block with integer rounding (sum + 2) / 4. A dimension of 1
// repeats its sole sample; odd dimensions discard the final row/column.
// A 1x1 input is unchanged. Generate levels individually to bound scratch memory.
// This does not provide atlas isolation: padding and sampled LOD remain the
// renderer's responsibility.
SplaResult<std::vector<std::uint8_t>> downsample_premultiplied_rgba(
    const std::vector<std::uint8_t>& pixels, int width, int height);

} // namespace spriteloop
