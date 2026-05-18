#include "spla_defold.h"

#include "stb/stb_image.h"

#include <dmsdk/sdk.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Texture resource helpers for SpriteLoop .spla packages.
//
// The core loader gives us embedded PNG asset bytes. This file validates and decodes those
// images, uploads them through Defold graphics APIs, and releases the temporary CPU buffers.
namespace spla_defold {

namespace {

constexpr std::uint8_t png_signature[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

// Reads a big-endian 32-bit integer from bytes at offset.
// The caller must ensure offset + 4 is in range.
std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

// Validates that an embedded asset is a PNG and extracts its IHDR dimensions.
// width and height receive positive dimensions on success; error receives a readable failure.
bool read_png_size(const spriteloop::SplaAsset& asset, int& width, int& height, std::string& error)
{
    const std::vector<std::uint8_t>& bytes = asset.bytes;
    if (bytes.size() < 33) {
        error = "asset '" + asset.path + "' is too small to be a PNG";
        return false;
    }

    for (std::size_t i = 0; i < sizeof(png_signature); ++i) {
        if (bytes[i] != png_signature[i]) {
            error = "asset '" + asset.path + "' does not have a PNG signature";
            return false;
        }
    }

    const std::uint32_t ihdr_size = read_be32(bytes, 8);
    const bool has_ihdr = bytes[12] == 'I' && bytes[13] == 'H' && bytes[14] == 'D' &&
                          bytes[15] == 'R';
    if (ihdr_size != 13 || !has_ihdr) {
        error = "asset '" + asset.path + "' does not start with a valid IHDR chunk";
        return false;
    }

    width = static_cast<int>(read_be32(bytes, 16));
    height = static_cast<int>(read_be32(bytes, 20));
    if (width <= 0 || height <= 0) {
        error = "asset '" + asset.path + "' has invalid PNG dimensions";
        return false;
    }

    return true;
}

// Finds an embedded asset by package-relative path.
// Returns nullptr if the manifest references an asset that is not present in the package.
const spriteloop::SplaAsset* find_asset(const spriteloop::SplaPackage& package,
                                        const std::string& path)
{
    for (const spriteloop::SplaAsset& asset : package.assets) {
        if (asset.path == path) {
            return &asset;
        }
    }

    return nullptr;
}

} // namespace

// Builds CPU-side image resources for every part in a parsed package.
// package supplies manifest data and embedded assets; resources is replaced with decoded RGBA
// data; error describes the first missing or invalid asset.
bool build_image_resources(const spriteloop::SplaPackage& package,
                           std::vector<SplaDefoldImageResource>& resources,
                           std::string& error)
{
    resources.clear();
    resources.reserve(package.parts.size());

    for (const spriteloop::SplaPart& part : package.parts) {
        const spriteloop::SplaAsset* asset = find_asset(package, part.asset_path);
        if (asset == nullptr) {
            error = "missing embedded asset for part '" + part.id + "': " + part.asset_path;
            resources.clear();
            return false;
        }

        SplaDefoldImageResource resource;
        resource.asset_path = asset->path;
        resource.byte_count = asset->bytes.size();

        if (!read_png_size(*asset, resource.width, resource.height, error)) {
            resources.clear();
            return false;
        }

        int decoded_width = 0;
        int decoded_height = 0;
        stbi_uc* pixels = stbi_load_from_memory(asset->bytes.data(),
                                                static_cast<int>(asset->bytes.size()),
                                                &decoded_width, &decoded_height, nullptr, 4);
        if (pixels == nullptr) {
            error = "failed to decode PNG asset '" + asset->path + "'";
            resources.clear();
            return false;
        }

        if (decoded_width != resource.width || decoded_height != resource.height) {
            stbi_image_free(pixels);
            error = "decoded PNG dimensions differ from IHDR for asset '" + asset->path + "'";
            resources.clear();
            return false;
        }

        const std::size_t pixel_count =
            static_cast<std::size_t>(decoded_width) * static_cast<std::size_t>(decoded_height);
        resource.rgba_pixels.assign(pixels, pixels + pixel_count * 4);
        stbi_image_free(pixels);

        resources.push_back(resource);
    }

    return true;
}

// Uploads decoded RGBA buffers to Defold textures.
// graphics_context is the active Defold graphics context, resources are updated with texture
// handles, and error describes the first upload failure.
bool upload_image_resources(dmGraphics::HContext graphics_context,
                            std::vector<SplaDefoldImageResource>& resources,
                            std::string& error)
{
    if (graphics_context == 0) {
        error = "Defold graphics context is not available";
        return false;
    }

    for (SplaDefoldImageResource& resource : resources) {
        if (resource.width <= 0 || resource.height <= 0 || resource.rgba_pixels.empty()) {
            error = "image resource has no decoded pixels: " + resource.asset_path;
            destroy_image_resources(graphics_context, resources);
            return false;
        }

        if (resource.width > std::numeric_limits<std::uint16_t>::max() ||
            resource.height > std::numeric_limits<std::uint16_t>::max()) {
            error = "image resource is too large for Defold texture dimensions: " +
                    resource.asset_path;
            destroy_image_resources(graphics_context, resources);
            return false;
        }

        dmGraphics::TextureCreationParams creation_params;
        creation_params.m_Type = dmGraphics::TEXTURE_TYPE_2D;
        creation_params.m_Width = static_cast<std::uint16_t>(resource.width);
        creation_params.m_Height = static_cast<std::uint16_t>(resource.height);
        creation_params.m_OriginalWidth = creation_params.m_Width;
        creation_params.m_OriginalHeight = creation_params.m_Height;
        creation_params.m_Depth = 1;
        creation_params.m_OriginalDepth = 1;
        creation_params.m_LayerCount = 1;
        creation_params.m_MipMapCount = 1;
        creation_params.m_UsageHintBits = dmGraphics::TEXTURE_USAGE_FLAG_SAMPLE;

        resource.texture = dmGraphics::NewTexture(graphics_context, creation_params);
        if (resource.texture == 0) {
            error = "failed to create Defold texture for asset '" + resource.asset_path + "'";
            destroy_image_resources(graphics_context, resources);
            return false;
        }

        dmGraphics::TextureParams texture_params;
        texture_params.m_Data = resource.rgba_pixels.data();
        texture_params.m_DataSize = static_cast<std::uint32_t>(resource.rgba_pixels.size());
        texture_params.m_Format = dmGraphics::TEXTURE_FORMAT_RGBA;
        texture_params.m_MinFilter = dmGraphics::TEXTURE_FILTER_LINEAR;
        texture_params.m_MagFilter = dmGraphics::TEXTURE_FILTER_LINEAR;
        texture_params.m_UWrap = dmGraphics::TEXTURE_WRAP_CLAMP_TO_EDGE;
        texture_params.m_VWrap = dmGraphics::TEXTURE_WRAP_CLAMP_TO_EDGE;
        texture_params.m_Width = creation_params.m_Width;
        texture_params.m_Height = creation_params.m_Height;
        texture_params.m_Depth = 1;
        texture_params.m_LayerCount = 1;
        texture_params.m_MipMap = 0;
        texture_params.m_SubUpdate = 0;

        dmGraphics::SetTexture(graphics_context, resource.texture, texture_params);
        dmGraphics::SetTextureParams(graphics_context, resource.texture,
                                     dmGraphics::TEXTURE_FILTER_LINEAR,
                                     dmGraphics::TEXTURE_FILTER_LINEAR,
                                     dmGraphics::TEXTURE_WRAP_CLAMP_TO_EDGE,
                                     dmGraphics::TEXTURE_WRAP_CLAMP_TO_EDGE, 1.0f);

        resource.rgba_pixels.clear();
        resource.rgba_pixels.shrink_to_fit();
    }

    return true;
}

// Deletes any Defold textures stored in resources.
// Safe to call with already-cleared resources; CPU-side pixels are not modified here.
void destroy_image_resources(dmGraphics::HContext graphics_context,
                             std::vector<SplaDefoldImageResource>& resources)
{
    if (graphics_context == 0) {
        return;
    }

    for (SplaDefoldImageResource& resource : resources) {
        if (resource.texture != 0) {
            dmGraphics::DeleteTexture(graphics_context, resource.texture);
            resource.texture = 0;
        }
    }
}

} // namespace spla_defold
