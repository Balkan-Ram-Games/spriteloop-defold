#pragma once

#include "spriteloop/spla_atlas.hpp"
#include "spriteloop/spla_package.hpp"

#include <cstddef>
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
    bool rotated = false;
    float pivot_x = 0.0f;
    float pivot_y = 0.0f;
    bool has_pivot = false;
    float rotation_degrees = 0.0f;
    int z_offset = 0;
    SplaMeshFit mesh_fit = SplaMeshFit::fitted;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};

struct SplaBakedVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct SplaBakedDraw {
    int part_index = -1;
    std::size_t index_start = 0;
    std::size_t index_count = 0;
    int mask_source_part_index = -1;
    float mask_padding = 0.0f;
};

struct SplaBakedMaskSource {
    int part_index = -1;
    // Atlas UVs match artwork; sample an alpha atlas eroded before packing.
    // Colors are white, alpha is state weight, independent of artwork opacity/tint.
    std::vector<SplaBakedVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct SplaBakedFrame {
    std::vector<SplaBakedVertex> vertices;
    // Explicit triangle topology. Legacy quad baking emits six indices per four vertices.
    std::vector<std::uint32_t> indices;
    // Ordered ranges, including empty ranges that preserve group boundaries.
    // Consecutive equal (source, padding) ranges form a mask group.
    // An absent source in mask_sources means zero coverage, never unmasked fallback.
    std::vector<SplaBakedDraw> draws;
    std::vector<SplaBakedMaskSource> mask_sources;
};

struct SplaBakedAnimation {
    std::string id;
    std::vector<SplaBakedFrame> frames;
};

struct SplaPartImageMap {
    std::vector<std::size_t> source_part_indices;
    std::vector<std::string> source_asset_paths;
    std::vector<std::size_t> atlas_region_indices_by_part;
    std::vector<std::size_t> atlas_region_indices_by_variant;
};

struct SplaSkinState {
    int skin_index = -1;
    std::vector<int> variant_overrides_by_part;
};

SplaPartImageMap build_part_image_map_by_asset(const SplaPackage& package);

std::vector<SplaBakedImage> build_baked_images_from_atlas(
    const SplaAtlas& atlas,
    const std::vector<std::size_t>& atlas_region_indices_by_part);

std::vector<SplaBakedImage> build_baked_images_from_atlas(
    const SplaPackage& package,
    const SplaAtlas& atlas,
    const SplaPartImageMap& image_map,
    const SplaSkinState& skin_state = {});

int find_skin_index_by_id(const SplaPackage& package, const std::string& skin_id);
int find_variant_index_by_id(const SplaPackage& package, const std::string& variant_id);
int find_part_index_by_id(const SplaPackage& package, const std::string& part_id);
int find_skin_index_by_id_or_name(const SplaPackage& package,
                                  const std::string& skin_id_or_name);
int find_part_index_by_id_key_or_name(const SplaPackage& package,
                                      const std::string& part_id_key_or_name);
int find_variant_index_by_id_key_or_name_for_part(
    const SplaPackage& package,
    int part_index,
    const std::string& variant_id_key_or_name);

SplaBounds calculate_baked_bounds(const SplaPackage& package,
                                  const std::vector<SplaBakedImage>& images);

std::vector<SplaBakedAnimation> build_baked_animations(
    const SplaPackage& package,
    // Pre-resolved images: zero dimensions mean hidden/unavailable. Use the
    // package-aware build_baked_images_from_atlas overload to apply skin visibility.
    // Use the atlas/image-map overload below for sprite-state crossfades.
    const std::vector<SplaBakedImage>& images);

std::vector<SplaBakedAnimation> build_baked_animations(
    const SplaPackage& package,
    const SplaAtlas& atlas,
    const SplaPartImageMap& image_map,
    const SplaSkinState& skin_state = {});

} // namespace spriteloop
