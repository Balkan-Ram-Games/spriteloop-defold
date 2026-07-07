#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace spriteloop {

struct SplaVec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct SplaColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct SplaTransform {
    float x = 0.0f;
    float y = 0.0f;
    float rotation_degrees = 0.0f;
    float skew_x = 0.0f;
    float skew_y = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float opacity = 1.0f;
    SplaColor tint;
};

struct SplaPart {
    std::string id;
    std::string key;
    std::string name;
    std::string asset_path;
    bool transform_only = false;
    int width = 0;
    int height = 0;
    SplaVec2 pivot;
    int draw_order = 0;
    bool visible = true;
};

struct SplaVariant {
    std::string id;
    std::string key;
    std::string name;
    int part_index = -1;
    std::string asset_path;
    int width = 0;
    int height = 0;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    int z_offset = 0;
};

struct SplaSkinPartOverride {
    int part_index = -1;
    int variant_index = -1;
    bool has_variant = false;
    bool visible = true;
    bool has_visible = false;
};

struct SplaSkin {
    std::string id;
    std::string name;
    std::vector<SplaSkinPartOverride> parts;
};

struct SplaFramePart {
    int part_index = -1;
    SplaTransform transform;
};

struct SplaEvent {
    std::string name;
    std::string data;
};

struct SplaFrame {
    int index = 0;
    int source_frame = 0;
    std::vector<SplaFramePart> parts;
    std::vector<SplaEvent> events;
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
    std::vector<SplaVariant> variants;
    std::vector<SplaSkin> skins;
    std::vector<SplaAnimation> animations;
    std::vector<SplaAsset> assets;
};

} // namespace spriteloop
