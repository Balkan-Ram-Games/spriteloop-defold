#pragma once

#include "spriteloop/spla_result.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace spriteloop {

struct SplaRgbaImage {
    std::string id;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

struct SplaAtlasRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct SplaAtlasUvRect {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

struct SplaAtlasRegion {
    std::string id;
    SplaAtlasRect content_rect;
    SplaAtlasRect padded_rect;
    SplaAtlasUvRect uv;
};

struct SplaAtlas {
    int width = 0;
    int height = 0;
    int padding = 0;
    std::vector<std::uint8_t> pixels;
    std::vector<SplaAtlasRegion> regions;
    std::uint32_t source_image_count = 0;
    std::uint64_t used_area = 0;
    float occupancy = 0.0f;
};

struct SplaAtlasOptions {
    int padding = 1;
};

SplaResult<SplaAtlas> pack_rgba_atlas(const std::vector<SplaRgbaImage>& images,
                                      const SplaAtlasOptions& options = SplaAtlasOptions());

} // namespace spriteloop
