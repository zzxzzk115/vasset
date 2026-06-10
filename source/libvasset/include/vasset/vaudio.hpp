#pragma once

#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vasset
{
    // How the cooked payload in audioData is stored. PCM variants hold raw interleaved
    // frames; passthrough variants keep the original encoded source bytes and rely on
    // the runtime decoder (miniaudio) for wav/mp3/flac. Ogg/Vorbis sources are always
    // decoded to PCM at import because the runtime has no vorbis decoder.
    enum class VAudioStorage : uint32_t
    {
        ePCM16 = 0,
        ePCMF32,
        ePassthroughWav,
        ePassthroughMp3,
        ePassthroughFlac,
    };

    inline std::string toString(VAudioStorage storage)
    {
        switch (storage)
        {
            case VAudioStorage::ePCM16:
                return "pcm16";
            case VAudioStorage::ePCMF32:
                return "pcmf32";
            case VAudioStorage::ePassthroughWav:
                return "passthrough_wav";
            case VAudioStorage::ePassthroughMp3:
                return "passthrough_mp3";
            case VAudioStorage::ePassthroughFlac:
                return "passthrough_flac";
            default:
                return "pcm16";
        }
    }

    inline bool isPassthrough(VAudioStorage storage)
    {
        return storage == VAudioStorage::ePassthroughWav || storage == VAudioStorage::ePassthroughMp3 ||
               storage == VAudioStorage::ePassthroughFlac;
    }

    struct VAudio
    {
        vbase::UUID uuid;
        std::string name;

        VAudioStorage storage {VAudioStorage::ePCM16};
        uint32_t      sampleRate {48000};
        uint32_t      channels {2};
        uint64_t      frameCount {0};
        float         duration {0.0f};

        std::vector<std::byte> audioData;

        std::string sourceFileName;
    };

    vbase::Result<void, AssetError> saveAudio(const VAudio& audio, vbase::StringView filePath);
    vbase::Result<void, AssetError> loadAudio(vbase::StringView filePath, VAudio& outAudio);
    vbase::Result<void, AssetError> loadAudioFromMemory(const std::vector<std::byte>& data, VAudio& outAudio);
} // namespace vasset
