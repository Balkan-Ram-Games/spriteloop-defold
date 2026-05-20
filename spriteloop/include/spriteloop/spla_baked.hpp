#pragma once

#include "spriteloop/spla_package.hpp"

#include <string>
#include <vector>

namespace spriteloop {

struct SplaBounds {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float radius_sq = 0.0f;
};

struct SplaBakedImage {
    int width = 0;
    int height = 0;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

struct SplaBakedVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float a = 1.0f;
};

struct SplaBakedFrame {
    std::vector<SplaBakedVertex> vertices;
};

struct SplaBakedAnimation {
    std::string id;
    std::vector<SplaBakedFrame> frames;
};

SplaBounds calculate_baked_bounds(const SplaPackage& package,
                                  const std::vector<SplaBakedImage>& images);

std::vector<SplaBakedAnimation> build_baked_animations(
    const SplaPackage& package,
    const std::vector<SplaBakedImage>& images);

} // namespace spriteloop
