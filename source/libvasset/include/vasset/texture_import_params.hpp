#pragma once

#include "vasset/vasset_importers.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace vasset
{
    constexpr std::string_view kTextureParamSubtype {"texture.subtype"};
    constexpr std::string_view kTextureParamGenerateMipmaps {"texture.generate_mipmaps"};
    constexpr std::string_view kTextureParamFlipY {"texture.flip_y"};
    constexpr std::string_view kTextureParamTargetFormat {"texture.target_format"};
    constexpr std::string_view kTextureParamUastc {"texture.uastc"};
    constexpr std::string_view kTextureParamQualityLevel {"texture.quality_level"};
    constexpr std::string_view kTextureParamCompressionLevel {"texture.compression_level"};
    constexpr std::string_view kTextureParamCompressOnlyLarge {"texture.compress_only_large"};
    constexpr std::string_view kTextureParamDownscaleLarge {"texture.downscale_large"};
    constexpr std::string_view kTextureParamDownscaleMinDimension {"texture.downscale_min_dimension"};
    constexpr std::string_view kTextureParamDownscaleTargetDimension {"texture.downscale_target_dimension"};
    constexpr std::string_view kTextureParamBakeNormalMap {"texture.bake_normal_map"};
    constexpr std::string_view kTextureParamDirectXNormalMap {"texture.directx_normal_map"};

    constexpr std::string_view kTextureSubtypeDefault {"default"};
    constexpr std::string_view kTextureSubtypeUiSprite {"ui_sprite"};
    constexpr std::string_view kTextureSubtypeNormalMap {"normal_map"};
    constexpr std::string_view kTextureSubtypeCursor {"cursor"};

    struct TextureImportParams
    {
        std::string                      subtype {std::string(kTextureSubtypeDefault)};
        VTextureImporter::ImportOptions options;
    };

    [[nodiscard]] std::string textureFileFormatToParam(VTextureFileFormat format);
    [[nodiscard]] VTextureFileFormat textureFileFormatFromParam(std::string_view value,
                                                                VTextureFileFormat fallback = VTextureFileFormat::eKTX2);

    [[nodiscard]] TextureImportParams
    resolveTextureImportParams(const std::unordered_map<std::string, std::string>& params,
                               const VTextureImporter::ImportOptions&             defaults = {});

    [[nodiscard]] VTextureImporter::ImportOptions
    textureImportOptionsForSubtype(std::string_view subtype, const VTextureImporter::ImportOptions& defaults = {});

    [[nodiscard]] std::unordered_map<std::string, std::string>
    normalizedTextureImportParams(std::unordered_map<std::string, std::string> existing,
                                  std::string_view                            subtype,
                                  const VTextureImporter::ImportOptions&      options);

    [[nodiscard]] std::string textureSubtypeFromParams(const std::unordered_map<std::string, std::string>& params);
} // namespace vasset
