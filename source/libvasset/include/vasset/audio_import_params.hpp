#pragma once

#include "vasset/vasset_importers.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace vasset
{
    constexpr std::string_view kAudioParamSubtype {"audio.subtype"};
    constexpr std::string_view kAudioParamStorage {"audio.storage"};
    constexpr std::string_view kAudioParamTargetSampleRate {"audio.target_sample_rate"};
    constexpr std::string_view kAudioParamForceMono {"audio.force_mono"};
    constexpr std::string_view kAudioParamNormalize {"audio.normalize"};
    constexpr std::string_view kAudioParamBitrateKbps {"audio.bitrate_kbps"};
    constexpr std::string_view kAudioParamQuality {"audio.quality"};

    // Audio subtypes are keyed by source format; each format gets its own canonical
    // import defaults (e.g. wav decodes to PCM, mp3/flac keep their compressed bytes).
    constexpr std::string_view kAudioSubtypeWav {"wav"};
    constexpr std::string_view kAudioSubtypeMp3 {"mp3"};
    constexpr std::string_view kAudioSubtypeFlac {"flac"};
    constexpr std::string_view kAudioSubtypeOgg {"ogg"};

    struct AudioImportParams
    {
        std::string                   subtype {std::string(kAudioSubtypeWav)};
        VAudioImporter::ImportOptions options;
    };

    [[nodiscard]] std::string audioStorageToParam(VAudioStorage storage);
    [[nodiscard]] VAudioStorage audioStorageFromParam(std::string_view value,
                                                      VAudioStorage    fallback = VAudioStorage::ePCM16);

    [[nodiscard]] std::string audioSubtypeForSourceExtension(std::string_view extension);

    [[nodiscard]] AudioImportParams
    resolveAudioImportParams(const std::unordered_map<std::string, std::string>& params,
                             std::string_view                                    sourceExtension,
                             const VAudioImporter::ImportOptions&                defaults = {});

    [[nodiscard]] VAudioImporter::ImportOptions
    audioImportOptionsForSubtype(std::string_view subtype, const VAudioImporter::ImportOptions& defaults = {});

    [[nodiscard]] std::unordered_map<std::string, std::string>
    normalizedAudioImportParams(std::unordered_map<std::string, std::string> existing,
                                std::string_view                            subtype,
                                const VAudioImporter::ImportOptions&        options);

    [[nodiscard]] std::string audioSubtypeFromParams(const std::unordered_map<std::string, std::string>& params,
                                                     std::string_view sourceExtension);
} // namespace vasset
