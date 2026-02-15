#pragma once

#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vasset
{
    struct VTextureRef
    {
        vbase::UUID uuid;
    };

    // Directly mapped to Vulkan VkFormat values
    // https://registry.khronos.org/vulkan/specs/latest/man/html/VkFormat.html
    enum class VTextureFormat : uint32_t
    {
        eUnknown = 0,

        eA8 = 1000470001, // VK_FORMAT_A8_UNORM
        eR8 = 9,          // VK_FORMAT_R8_UNORM

        eR16  = 70,  // VK_FORMAT_R16_UNORM
        eR32F = 100, // VK_FORMAT_R32_SFLOAT
        eR16F = 76,  // VK_FORMAT_R16_SFLOAT

        eRG8   = 16, // VK_FORMAT_R8G8_UNORM
        eRG8S  = 17, // VK_FORMAT_R8G8_SNORM
        eRG16  = 77, // VK_FORMAT_R16G16_UNORM
        eRG16F = 83, // VK_FORMAT_R16G16_SFLOAT
        eRG16S = 82, // VK_FORMAT_R16G16_SINT

        eRGBA8   = 37,  // VK_FORMAT_R8G8B8A8_UNORM
        eRGBA8S  = 38,  // VK_FORMAT_R8G8B8A8_SNORM
        eRGB8    = 23,  // VK_FORMAT_R8G8B8_UNORM
        eBGRA8   = 44,  // VK_FORMAT_B8G8R8A8_UNORM
        eBGR8    = 30,  // VK_FORMAT_B8G8R8_UNORM
        eRGBA16  = 70,  // VK_FORMAT_R16G16B16A16_UNORM
        eRGBA16F = 97,  // VK_FORMAT_R16G16B16A16_SFLOAT
        eRGBA32F = 109, // VK_FORMAT_R32G32B32A32_SFLOAT

        eA2RGB10 = 58,  // VK_FORMAT_A2R10G10B10_UNORM_PACK32
        eB10RG11 = 122, // VK_FORMAT_B10G11R11_UFLOAT_PACK32

        eBC1  = 133, // VK_FORMAT_BC1_RGBA_UNORM_BLOCK
        eBC2  = 135, // VK_FORMAT_BC2_UNORM_BLOCK
        eBC3  = 137, // VK_FORMAT_BC3_UNORM_BLOCK
        eBC4  = 139, // VK_FORMAT_BC4_UNORM_BLOCK
        eBC5  = 141, // VK_FORMAT_BC5_UNORM_BLOCK
        eBC6H = 144, // VK_FORMAT_BC6H_SFLOAT_BLOCK
        eBC7  = 145, // VK_FORMAT_BC7_UNORM_BLOCK

        eETC2   = 147, // VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK
        eETC2A  = 151, // VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK
        eETC2A1 = 149, // VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK

        ePTC12A = 1000054004, // VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG
        ePTC14A = 1000054005, // VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG
        ePTC12  = 1000054000, // VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG
        ePTC14  = 1000054001, // VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG
        ePTC22  = 1000054002, // VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG
        ePTC24  = 1000054003, // VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG

        eASTC_4x4  = 157, // VK_FORMAT_ASTC_4x4_UNORM_BLOCK
        eASTC_5x5  = 161, // VK_FORMAT_ASTC_5x5_UNORM_BLOCK
        eASTC_6x6  = 165, // VK_FORMAT_ASTC_6x6_UNORM_BLOCK
        eASTC_8x5  = 167, // VK_FORMAT_ASTC_8x5_UNORM_BLOCK
        eASTC_8x6  = 169, // VK_FORMAT_ASTC_8x6_UNORM_BLOCK
        eASTC_10x5 = 173, // VK_FORMAT_ASTC_10x5_UNORM_BLOCK
    };

    enum class VTextureFileFormat : uint32_t
    {
        eUnknown = 0,

        eJPG,
        eJPEG,
        ePNG,
        eTGA,
        eBMP,
        ePSD,
        eGIF,
        eHDR,
        ePIC,

        eEXR,

        eKTX,
        eDDS,

        eKTX2,
    };

    enum class VTextureDimension : uint32_t
    {
        e1D = 1,
        e2D,
        e3D,
    };

    struct VTexture
    {
        vbase::UUID          uuid;
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
            return "VTexture { uuid: " + vbase::to_string(uuid) + ", width: " + std::to_string(width) +
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

    vbase::Result<void, AssetError> saveTexture(const VTexture& texture, vbase::StringView filePath);
    vbase::Result<void, AssetError> loadTexture(vbase::StringView filePath, VTexture& outTexture);
    vbase::Result<void, AssetError> loadTextureFromMemory(const std::vector<std::byte>& data, VTexture& outTexture);
} // namespace vasset
