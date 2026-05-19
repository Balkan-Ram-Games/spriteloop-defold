#pragma once

#include "spriteloop/spla_result.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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

namespace detail {

inline int next_power_of_two(int value)
{
    if (value <= 1) {
        return 1;
    }

    int result = 1;
    while (result < value && result <= (std::numeric_limits<int>::max() / 2)) {
        result *= 2;
    }
    return result;
}

inline bool checked_image_size(int width, int height, std::size_t& size)
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    const std::uint64_t pixel_count =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    const std::uint64_t byte_count = pixel_count * 4u;
    if (byte_count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }

    size = static_cast<std::size_t>(byte_count);
    return true;
}

inline void copy_pixel_clamped(const SplaRgbaImage& source,
                               SplaAtlas& atlas,
                               int source_x,
                               int source_y,
                               int atlas_x,
                               int atlas_y)
{
    source_x = std::clamp(source_x, 0, source.width - 1);
    source_y = std::clamp(source_y, 0, source.height - 1);

    const std::size_t source_offset =
        (static_cast<std::size_t>(source_y) * static_cast<std::size_t>(source.width) +
         static_cast<std::size_t>(source_x)) *
        4u;
    const std::size_t atlas_offset =
        (static_cast<std::size_t>(atlas_y) * static_cast<std::size_t>(atlas.width) +
         static_cast<std::size_t>(atlas_x)) *
        4u;

    atlas.pixels[atlas_offset + 0] = source.pixels[source_offset + 0];
    atlas.pixels[atlas_offset + 1] = source.pixels[source_offset + 1];
    atlas.pixels[atlas_offset + 2] = source.pixels[source_offset + 2];
    atlas.pixels[atlas_offset + 3] = source.pixels[source_offset + 3];
}

} // namespace detail

inline SplaResult<SplaAtlas> pack_rgba_atlas(const std::vector<SplaRgbaImage>& images,
                                             const SplaAtlasOptions& options = SplaAtlasOptions())
{
    if (images.empty()) {
        return SplaError{SplaErrorCode::validation_error, "atlas input has no images"};
    }

    const int padding = std::max(0, options.padding);
    std::uint64_t padded_area = 0;
    int widest = 1;
    for (const SplaRgbaImage& image : images) {
        std::size_t expected_size = 0;
        if (!detail::checked_image_size(image.width, image.height, expected_size) ||
            image.pixels.size() != expected_size) {
            return SplaError{SplaErrorCode::validation_error,
                             "atlas image has invalid RGBA data: " + image.id};
        }

        const int padded_width = image.width + padding * 2;
        const int padded_height = image.height + padding * 2;
        widest = std::max(widest, padded_width);
        padded_area += static_cast<std::uint64_t>(padded_width) *
                       static_cast<std::uint64_t>(padded_height);
    }

    const int target_side = detail::next_power_of_two(
        static_cast<int>(std::ceil(std::sqrt(static_cast<double>(padded_area)))));
    const int atlas_width = detail::next_power_of_two(std::max(widest, target_side));

    SplaAtlas atlas;
    atlas.width = atlas_width;
    atlas.padding = padding;
    atlas.source_image_count = static_cast<std::uint32_t>(images.size());
    atlas.regions.resize(images.size());

    int cursor_x = 0;
    int cursor_y = 0;
    int row_height = 0;
    int used_height = 0;
    for (std::size_t i = 0; i < images.size(); ++i) {
        const SplaRgbaImage& image = images[i];
        const int padded_width = image.width + padding * 2;
        const int padded_height = image.height + padding * 2;
        if (cursor_x > 0 && cursor_x + padded_width > atlas_width) {
            cursor_x = 0;
            cursor_y += row_height;
            row_height = 0;
        }

        SplaAtlasRegion region;
        region.id = image.id;
        region.padded_rect = {cursor_x, cursor_y, padded_width, padded_height};
        region.content_rect = {cursor_x + padding, cursor_y + padding, image.width, image.height};
        atlas.regions[i] = region;

        cursor_x += padded_width;
        row_height = std::max(row_height, padded_height);
        used_height = std::max(used_height, cursor_y + padded_height);
        atlas.used_area +=
            static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height);
    }

    atlas.height = detail::next_power_of_two(std::max(1, used_height));
    std::size_t atlas_byte_count = 0;
    if (!detail::checked_image_size(atlas.width, atlas.height, atlas_byte_count)) {
        return SplaError{SplaErrorCode::validation_error, "atlas dimensions are invalid"};
    }

    atlas.pixels.assign(atlas_byte_count, 0);
    for (std::size_t i = 0; i < images.size(); ++i) {
        const SplaRgbaImage& image = images[i];
        SplaAtlasRegion& region = atlas.regions[i];

        for (int y = 0; y < region.padded_rect.height; ++y) {
            for (int x = 0; x < region.padded_rect.width; ++x) {
                detail::copy_pixel_clamped(image, atlas, x - padding, y - padding,
                                           region.padded_rect.x + x, region.padded_rect.y + y);
            }
        }

        const float inv_width = 1.0f / static_cast<float>(atlas.width);
        const float inv_height = 1.0f / static_cast<float>(atlas.height);
        region.uv.u0 = static_cast<float>(region.content_rect.x) * inv_width;
        region.uv.v0 = static_cast<float>(region.content_rect.y) * inv_height;
        region.uv.u1 =
            static_cast<float>(region.content_rect.x + region.content_rect.width) * inv_width;
        region.uv.v1 =
            static_cast<float>(region.content_rect.y + region.content_rect.height) * inv_height;
    }

    const std::uint64_t atlas_area =
        static_cast<std::uint64_t>(atlas.width) * static_cast<std::uint64_t>(atlas.height);
    atlas.occupancy =
        atlas_area > 0 ? static_cast<float>(static_cast<double>(atlas.used_area) /
                                            static_cast<double>(atlas_area))
                       : 0.0f;
    return atlas;
}

} // namespace spriteloop
