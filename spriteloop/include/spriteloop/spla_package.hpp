#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spriteloop {

struct SplaVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct SplaTransform {
    float x = 0.0f;
    float y = 0.0f;
    float rotation_degrees = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float opacity = 1.0f;
};

struct SplaPart {
    std::string id;
    std::string name;
    std::string asset_path;
    int width = 0;
    int height = 0;
    SplaVec2 pivot;
    int draw_order = 0;
};

struct SplaFramePart {
    int part_index = -1;
    SplaTransform transform;
};

struct SplaFrame {
    int index = 0;
    int source_frame = 0;
    std::vector<SplaFramePart> parts;
};

struct SplaAnimation {
    std::string id;
    std::string name;
    float fps = 0.0f;
    bool loop = false;
    std::vector<SplaFrame> frames;
};

struct SplaAsset {
    std::string path;
    std::vector<std::uint8_t> bytes;
};

struct SplaPackage {
    std::string name;
    int canvas_width = 0;
    int canvas_height = 0;
    std::vector<SplaPart> parts;
    std::vector<SplaAnimation> animations;
    std::vector<SplaAsset> assets;
};

} // namespace spriteloop
