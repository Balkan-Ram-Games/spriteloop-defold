#pragma once

#include "spriteloop/spla_atlas.hpp"

namespace spriteloop {

// White RGB with inset alpha. Operates on original image pixels before atlas
// packing, fitting or deformation. Sampling matches the app's circular erosion
// (integer disk plus 24 fractional perimeter samples); outside UVs are transparent.
SplaResult<SplaRgbaImage> inset_mask_alpha(const SplaRgbaImage& image,
                                          float padding,
                                          bool nearest = false);

} // namespace spriteloop
