#include "vasset/audio_import_params.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>

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

        void eraseKnownAudioParams(std::unordered_map<std::string, std::string>& params)
        {
            params.erase(std::string(kAudioParamSubtype));
            params.erase(std::string(kAudioParamStorage));
            params.erase(std::string(kAudioParamTargetSampleRate));
            params.erase(std::string(kAudioParamForceMono));
            params.erase(std::string(kAudioParamNormalize));
            params.erase(std::string(kAudioParamBitrateKbps));
            params.erase(std::string(kAudioParamQuality));
        }
    } // namespace

    std::string audioStorageToParam(const VAudioStorage storage)
    {
        switch (storage)
        {
            case VAudioStorage::ePCM16:
                return "pcm16";
            case VAudioStorage::ePCMF32:
                return "pcmf32";
            case VAudioStorage::ePassthroughWav:
            case VAudioStorage::ePassthroughMp3:
            case VAudioStorage::ePassthroughFlac:
                return "passthrough";
            default:
                return "pcm16";
        }
    }

    VAudioStorage audioStorageFromParam(std::string_view value, const VAudioStorage fallback)
    {
        const auto v = lowerAscii(value);
        if (v == "pcm16")
            return VAudioStorage::ePCM16;
        if (v == "pcmf32" || v == "pcm32f" || v == "f32")
            return VAudioStorage::ePCMF32;
        // The concrete passthrough variant is fixed up from the source extension by
        // the importer; the generic marker resolves to wav here.
        if (v == "passthrough")
            return VAudioStorage::ePassthroughWav;
        return fallback;
    }

    std::string audioSubtypeForSourceExtension(std::string_view extension)
    {
        auto ext = lowerAscii(extension);
        if (!ext.empty() && ext.front() == '.')
            ext.erase(ext.begin());
        if (ext == "mp3")
            return std::string(kAudioSubtypeMp3);
        if (ext == "flac")
            return std::string(kAudioSubtypeFlac);
        if (ext == "ogg")
            return std::string(kAudioSubtypeOgg);
        return std::string(kAudioSubtypeWav);
    }

    VAudioImporter::ImportOptions
    audioImportOptionsForSubtype(std::string_view subtype, const VAudioImporter::ImportOptions& defaults)
    {
        auto options = defaults;
        const auto id = lowerAscii(subtype);
        if (id == kAudioSubtypeMp3)
        {
            options.storage = VAudioStorage::ePassthroughMp3;
        }
        else if (id == kAudioSubtypeFlac)
        {
            options.storage = VAudioStorage::ePassthroughFlac;
        }
        else if (id == kAudioSubtypeOgg)
        {
            // No runtime vorbis decoder: ogg always decodes to PCM at import.
            options.storage = VAudioStorage::ePCM16;
        }
        else
        {
            options.storage = VAudioStorage::ePCM16;
        }
        return options;
    }

    AudioImportParams resolveAudioImportParams(const std::unordered_map<std::string, std::string>& params,
                                               std::string_view                                    sourceExtension,
                                               const VAudioImporter::ImportOptions&                defaults)
    {
        AudioImportParams out;
        out.subtype = audioSubtypeFromParams(params, sourceExtension);
        out.options = audioImportOptionsForSubtype(out.subtype, defaults);

        if (const auto* v = findParam(params, kAudioParamStorage))
            out.options.storage = audioStorageFromParam(*v, out.options.storage);
        if (const auto* v = findParam(params, kAudioParamTargetSampleRate))
            out.options.targetSampleRate = parseU32(*v, out.options.targetSampleRate);
        if (const auto* v = findParam(params, kAudioParamForceMono))
            out.options.forceMono = parseBool(*v, out.options.forceMono);
        if (const auto* v = findParam(params, kAudioParamNormalize))
            out.options.normalize = parseBool(*v, out.options.normalize);
        if (const auto* v = findParam(params, kAudioParamBitrateKbps))
            out.options.bitrateKbps = parseU32(*v, out.options.bitrateKbps);
        if (const auto* v = findParam(params, kAudioParamQuality))
            out.options.quality = parseU32(*v, out.options.quality);

        return out;
    }

    std::unordered_map<std::string, std::string>
    normalizedAudioImportParams(std::unordered_map<std::string, std::string> existing,
                                std::string_view                            subtype,
                                const VAudioImporter::ImportOptions&        options)
    {
        eraseKnownAudioParams(existing);

        const std::string sub = subtype.empty() ? std::string(kAudioSubtypeWav) : std::string(subtype);
        // Only persist fields that differ from the subtype's canonical defaults so an
        // all-default asset writes a near-empty [params] (same policy as textures).
        const auto baseline = audioImportOptionsForSubtype(sub, {});

        existing[std::string(kAudioParamSubtype)] = sub;

        const auto setBool = [&](std::string_view key, bool v, bool def) {
            if (v != def)
                existing[std::string(key)] = boolParam(v);
        };
        const auto setU32 = [&](std::string_view key, uint32_t v, uint32_t def) {
            if (v != def)
                existing[std::string(key)] = std::to_string(v);
        };

        if (audioStorageToParam(options.storage) != audioStorageToParam(baseline.storage))
            existing[std::string(kAudioParamStorage)] = audioStorageToParam(options.storage);
        setU32(kAudioParamTargetSampleRate, options.targetSampleRate, baseline.targetSampleRate);
        setBool(kAudioParamForceMono, options.forceMono, baseline.forceMono);
        setBool(kAudioParamNormalize, options.normalize, baseline.normalize);
        setU32(kAudioParamBitrateKbps, options.bitrateKbps, baseline.bitrateKbps);
        setU32(kAudioParamQuality, options.quality, baseline.quality);
        return existing;
    }

    std::string audioSubtypeFromParams(const std::unordered_map<std::string, std::string>& params,
                                       std::string_view sourceExtension)
    {
        if (const auto* subtype = findParam(params, kAudioParamSubtype); subtype && !subtype->empty())
            return lowerAscii(*subtype);
        return audioSubtypeForSourceExtension(sourceExtension);
    }
} // namespace vasset
