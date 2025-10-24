#pragma once

#include "vasset/vuuid.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vasset
{
    struct VTextureRef
    {
        VUUID uuid;
    };

    // Directly mapped to Vulkan VkFormat values
    // https://registry.khronos.org/vulkan/specs/latest/man/html/VkFormat.html
    enum class VTextureFormat : uint32_t
    {
        eUnknown = 0,
        eRGBA8   = 37,  // VK_FORMAT_R8G8B8A8_UNORM
        eRGB8    = 23,  // VK_FORMAT_R8G8B8_UNORM
        eBGRA8   = 44,  // VK_FORMAT_B8G8R8A8_UNORM
        eBGR8    = 30,  // VK_FORMAT_B8G8R8_UNORM
        eRGBA16F = 97,  // VK_FORMAT_R16G16B16A16_SFLOAT
        eRGBA32F = 109, // VK_FORMAT_R32G32B32A32_SFLOAT
        eBC1     = 133, // VK_FORMAT_BC1_RGBA_UNORM_BLOCK
        eBC3     = 137, // VK_FORMAT_BC3_UNORM_BLOCK
        eBC4     = 139, // VK_FORMAT_BC4_UNORM_BLOCK
        eBC5     = 141, // VK_FORMAT_BC5_UNORM_BLOCK
        eBC7     = 145, // VK_FORMAT_BC7_UNORM_BLOCK
    };

    enum class VTextureFileFormat : uint32_t
    {
        eUnknown = 0,
        eKTX2,
        ePNG,
        eJPG,
        eHDR
    };

    enum class VTextureDimension : uint32_t
    {
        e1D = 1,
        e2D,
        e3D,
    };

    struct VTexture
    {
        VUUID                uuid;
        uint32_t             width {0};
        uint32_t             height {0};
        uint32_t             depth {1};
        uint32_t             mipLevels {1};
        uint32_t             arrayLayers {1};
        bool                 isCubemap {false};
        bool                 generateMipmaps {false};
        VTextureDimension    type {VTextureDimension::e2D};
        VTextureFormat       format {VTextureFormat::eRGBA8};
        VTextureFileFormat   fileFormat {VTextureFileFormat::ePNG};
        std::vector<uint8_t> data; // image data, could be compressed (KTX2) or raw (PNG, JPG, HDR)

        std::string toString() const
        {
            return "VTexture { uuid: " + uuid.toString() + ", width: " + std::to_string(width) +
                   ", height: " + std::to_string(height) + ", depth: " + std::to_string(depth) +
                   ", mipLevels: " + std::to_string(mipLevels) + ", arrayLayers: " + std::to_string(arrayLayers) +
                   ", isCubemap: " + std::to_string(isCubemap) +
                   ", generateMipmaps: " + std::to_string(generateMipmaps) +
                   ", type: " + std::to_string(static_cast<uint32_t>(type)) +
                   ", format: " + std::to_string(static_cast<uint32_t>(format)) +
                   ", fileFormat: " + std::to_string(static_cast<uint32_t>(fileFormat)) +
                   ", dataSize: " + std::to_string(data.size()) + " }";
        }
    };

    struct VTextureMeta
    {
        VUUID uuid;
    };

    bool saveTexture(const VTexture& texture, const std::string& filePath, const std::string& metaFilePath);
    bool loadTexture(const std::string& filePath, VTexture& outTexture);
} // namespace vasset
