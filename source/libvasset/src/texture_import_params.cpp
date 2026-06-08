#include "vasset/texture_import_params.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <charconv>

namespace vasset
{
    namespace
    {
        std::string lowerAscii(std::string_view value)
        {
            std::string out(value);
            std::ranges::transform(out, out.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return out;
        }

        const std::string* findParam(const std::unordered_map<std::string, std::string>& params, std::string_view key)
        {
            auto it = params.find(std::string(key));
            return it == params.end() ? nullptr : &it->second;
        }

        bool parseBool(std::string_view value, const bool fallback)
        {
            const auto v = lowerAscii(value);
            if (v == "1" || v == "true" || v == "yes" || v == "on")
                return true;
            if (v == "0" || v == "false" || v == "no" || v == "off")
                return false;
            return fallback;
        }

        uint32_t parseU32(std::string_view value, const uint32_t fallback)
        {
            uint32_t out = fallback;
            const auto* begin = value.data();
            const auto* end   = begin + value.size();
            const auto  res   = std::from_chars(begin, end, out);
            return res.ec == std::errc {} ? out : fallback;
        }

        std::string boolParam(const bool value) { return value ? "true" : "false"; }

        void eraseKnownTextureParams(std::unordered_map<std::string, std::string>& params)
        {
            params.erase(std::string(kTextureParamSubtype));
            params.erase(std::string(kTextureParamGenerateMipmaps));
            params.erase(std::string(kTextureParamFlipY));
            params.erase(std::string(kTextureParamTargetFormat));
            params.erase(std::string(kTextureParamUastc));
            params.erase(std::string(kTextureParamQualityLevel));
            params.erase(std::string(kTextureParamCompressionLevel));
            params.erase(std::string(kTextureParamCompressOnlyLarge));
            params.erase(std::string(kTextureParamDownscaleLarge));
            params.erase(std::string(kTextureParamDownscaleMinDimension));
            params.erase(std::string(kTextureParamDownscaleTargetDimension));
            params.erase(std::string(kTextureParamBakeNormalMap));
            params.erase(std::string(kTextureParamDirectXNormalMap));
        }
    } // namespace

    std::string textureFileFormatToParam(const VTextureFileFormat format)
    {
        switch (format)
        {
            case VTextureFileFormat::eJPG:
                return "jpg";
            case VTextureFileFormat::eJPEG:
                return "jpeg";
            case VTextureFileFormat::ePNG:
                return "png";
            case VTextureFileFormat::eTGA:
                return "tga";
            case VTextureFileFormat::eBMP:
                return "bmp";
            case VTextureFileFormat::eHDR:
                return "hdr";
            case VTextureFileFormat::ePIC:
                return "pic";
            case VTextureFileFormat::eEXR:
                return "exr";
            case VTextureFileFormat::eKTX:
                return "ktx";
            case VTextureFileFormat::eDDS:
                return "dds";
            case VTextureFileFormat::eKTX2:
                return "ktx2";
            default:
                return "unknown";
        }
    }

    VTextureFileFormat textureFileFormatFromParam(std::string_view value, const VTextureFileFormat fallback)
    {
        auto v = lowerAscii(value);
        if (!v.empty() && v.front() == '.')
            v.erase(v.begin());
        if (v == "jpg")
            return VTextureFileFormat::eJPG;
        if (v == "jpeg")
            return VTextureFileFormat::eJPEG;
        if (v == "png")
            return VTextureFileFormat::ePNG;
        if (v == "tga")
            return VTextureFileFormat::eTGA;
        if (v == "bmp")
            return VTextureFileFormat::eBMP;
        if (v == "hdr")
            return VTextureFileFormat::eHDR;
        if (v == "pic")
            return VTextureFileFormat::ePIC;
        if (v == "exr")
            return VTextureFileFormat::eEXR;
        if (v == "ktx")
            return VTextureFileFormat::eKTX;
        if (v == "dds")
            return VTextureFileFormat::eDDS;
        if (v == "ktx2")
            return VTextureFileFormat::eKTX2;
        return fallback;
    }

    VTextureImporter::ImportOptions
    textureImportOptionsForSubtype(std::string_view subtype, const VTextureImporter::ImportOptions& defaults)
    {
        auto options = defaults;
        const auto id = lowerAscii(subtype);
        if (id == kTextureSubtypeUiSprite)
        {
            options.generateMipmaps = false;
            options.flipY           = false;
            options.targetTextureFileFormat = VTextureFileFormat::eKTX2;
            options.bakeNormalMap   = false;
            options.directXNormalMap = false;
        }
        else if (id == kTextureSubtypeNormalMap)
        {
            options.generateMipmaps = true;
            options.targetTextureFileFormat = VTextureFileFormat::eKTX2;
            options.bakeNormalMap   = true;
        }
        else if (id == kTextureSubtypeCursor)
        {
            options.generateMipmaps       = false;
            options.flipY                 = false;
            options.targetTextureFileFormat = VTextureFileFormat::eKTX2;
            options.bakeNormalMap         = false;
            options.directXNormalMap      = false;
            options.downscaleLargeTextures = false;
        }
        else
        {
            options.generateMipmaps = true;
            options.flipY           = false;
            options.targetTextureFileFormat = VTextureFileFormat::eKTX2;
            options.bakeNormalMap   = false;
            options.directXNormalMap = false;
        }
        return options;
    }

    TextureImportParams
    resolveTextureImportParams(const std::unordered_map<std::string, std::string>& params,
                               const VTextureImporter::ImportOptions&             defaults)
    {
        TextureImportParams out;
        out.subtype = textureSubtypeFromParams(params);
        out.options = textureImportOptionsForSubtype(out.subtype, defaults);

        if (const auto* v = findParam(params, kTextureParamGenerateMipmaps))
            out.options.generateMipmaps = parseBool(*v, out.options.generateMipmaps);
        if (const auto* v = findParam(params, kTextureParamFlipY))
            out.options.flipY = parseBool(*v, out.options.flipY);
        if (const auto* v = findParam(params, kTextureParamTargetFormat))
            out.options.targetTextureFileFormat =
                textureFileFormatFromParam(*v, out.options.targetTextureFileFormat);
        if (const auto* v = findParam(params, kTextureParamUastc))
            out.options.uastc = parseBool(*v, out.options.uastc);
        if (const auto* v = findParam(params, kTextureParamQualityLevel))
            out.options.qualityLevel = parseU32(*v, out.options.qualityLevel);
        if (const auto* v = findParam(params, kTextureParamCompressionLevel))
            out.options.compressionLevel = parseU32(*v, out.options.compressionLevel);
        if (const auto* v = findParam(params, kTextureParamCompressOnlyLarge))
            out.options.compressOnlyLargeTextures = parseBool(*v, out.options.compressOnlyLargeTextures);
        if (const auto* v = findParam(params, kTextureParamDownscaleLarge))
            out.options.downscaleLargeTextures = parseBool(*v, out.options.downscaleLargeTextures);
        if (const auto* v = findParam(params, kTextureParamDownscaleMinDimension))
            out.options.downscaleMinDimension = parseU32(*v, out.options.downscaleMinDimension);
        if (const auto* v = findParam(params, kTextureParamDownscaleTargetDimension))
            out.options.downscaleTargetDimension = parseU32(*v, out.options.downscaleTargetDimension);
        if (const auto* v = findParam(params, kTextureParamBakeNormalMap))
            out.options.bakeNormalMap = parseBool(*v, out.options.bakeNormalMap);
        if (const auto* v = findParam(params, kTextureParamDirectXNormalMap))
            out.options.directXNormalMap = parseBool(*v, out.options.directXNormalMap);

        return out;
    }

    std::unordered_map<std::string, std::string>
    normalizedTextureImportParams(std::unordered_map<std::string, std::string> existing,
                                  std::string_view                            subtype,
                                  const VTextureImporter::ImportOptions&      options)
    {
        eraseKnownTextureParams(existing);

        const std::string sub = subtype.empty() ? std::string(kTextureSubtypeDefault) : std::string(subtype);
        // Only persist fields that differ from the subtype's canonical defaults. resolve fills the
        // rest back in from the same baseline, so an all-default asset writes a near-empty [params].
        const auto baseline = textureImportOptionsForSubtype(sub, {});

        if (sub != std::string(kTextureSubtypeDefault))
            existing[std::string(kTextureParamSubtype)] = sub;

        const auto setBool = [&](std::string_view key, bool v, bool def) {
            if (v != def)
                existing[std::string(key)] = boolParam(v);
        };
        const auto setU32 = [&](std::string_view key, uint32_t v, uint32_t def) {
            if (v != def)
                existing[std::string(key)] = std::to_string(v);
        };

        setBool(kTextureParamGenerateMipmaps, options.generateMipmaps, baseline.generateMipmaps);
        setBool(kTextureParamFlipY, options.flipY, baseline.flipY);
        if (options.targetTextureFileFormat != baseline.targetTextureFileFormat)
            existing[std::string(kTextureParamTargetFormat)] = textureFileFormatToParam(options.targetTextureFileFormat);
        setBool(kTextureParamUastc, options.uastc, baseline.uastc);
        setU32(kTextureParamQualityLevel, options.qualityLevel, baseline.qualityLevel);
        setU32(kTextureParamCompressionLevel, options.compressionLevel, baseline.compressionLevel);
        setBool(kTextureParamCompressOnlyLarge, options.compressOnlyLargeTextures, baseline.compressOnlyLargeTextures);
        setBool(kTextureParamDownscaleLarge, options.downscaleLargeTextures, baseline.downscaleLargeTextures);
        setU32(kTextureParamDownscaleMinDimension, options.downscaleMinDimension, baseline.downscaleMinDimension);
        setU32(kTextureParamDownscaleTargetDimension, options.downscaleTargetDimension, baseline.downscaleTargetDimension);
        setBool(kTextureParamBakeNormalMap, options.bakeNormalMap, baseline.bakeNormalMap);
        setBool(kTextureParamDirectXNormalMap, options.directXNormalMap, baseline.directXNormalMap);
        return existing;
    }

    std::string textureSubtypeFromParams(const std::unordered_map<std::string, std::string>& params)
    {
        if (const auto* subtype = findParam(params, kTextureParamSubtype); subtype && !subtype->empty())
            return *subtype;
        return std::string(kTextureSubtypeDefault);
    }
} // namespace vasset
