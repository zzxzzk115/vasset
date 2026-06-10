#include "vasset/vaudio.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace vasset
{
    namespace
    {
        struct VAudioFileHeader
        {
            char     magic[16];
            uint32_t version {1};
            uint32_t flags {0};
            uint64_t rawSize {0};
        };

        constexpr const char* kAudioMagic = "VAUDIO";

        void writeRaw(std::vector<uint8_t>& raw, const void* data, const size_t size)
        {
            const auto* p = static_cast<const uint8_t*>(data);
            raw.insert(raw.end(), p, p + size);
        }

        void writeString(std::vector<uint8_t>& raw, const std::string& value)
        {
            const uint32_t len = static_cast<uint32_t>(value.size());
            writeRaw(raw, &len, sizeof(len));
            if (len != 0u)
                writeRaw(raw, value.data(), len);
        }

        void writeBytes(std::vector<uint8_t>& raw, const std::vector<std::byte>& value)
        {
            const uint64_t size = static_cast<uint64_t>(value.size());
            writeRaw(raw, &size, sizeof(size));
            if (size != 0u)
                writeRaw(raw, value.data(), static_cast<size_t>(size));
        }

        bool readRaw(const std::vector<uint8_t>& raw, size_t& offset, void* dst, const size_t size)
        {
            if (offset + size > raw.size())
                return false;
            std::memcpy(dst, raw.data() + offset, size);
            offset += size;
            return true;
        }

        bool readString(const std::vector<uint8_t>& raw, size_t& offset, std::string& out)
        {
            uint32_t len = 0;
            if (!readRaw(raw, offset, &len, sizeof(len)))
                return false;
            if (offset + len > raw.size())
                return false;
            out.assign(reinterpret_cast<const char*>(raw.data() + offset), len);
            offset += len;
            return true;
        }

        bool readBytes(const std::vector<uint8_t>& raw, size_t& offset, std::vector<std::byte>& out)
        {
            uint64_t size = 0;
            if (!readRaw(raw, offset, &size, sizeof(size)))
                return false;
            if (offset + size > raw.size())
                return false;
            out.resize(static_cast<size_t>(size));
            if (size != 0u)
                std::memcpy(out.data(), raw.data() + offset, static_cast<size_t>(size));
            offset += static_cast<size_t>(size);
            return true;
        }
    } // namespace

    vbase::Result<void, AssetError> saveAudio(const VAudio& audio, vbase::StringView filePath)
    {
        std::vector<uint8_t> raw;
        writeRaw(raw, &audio.uuid, sizeof(audio.uuid));
        writeString(raw, audio.name);
        const uint32_t storage = static_cast<uint32_t>(audio.storage);
        writeRaw(raw, &storage, sizeof(storage));
        writeRaw(raw, &audio.sampleRate, sizeof(audio.sampleRate));
        writeRaw(raw, &audio.channels, sizeof(audio.channels));
        writeRaw(raw, &audio.frameCount, sizeof(audio.frameCount));
        writeRaw(raw, &audio.duration, sizeof(audio.duration));
        writeBytes(raw, audio.audioData);
        writeString(raw, audio.sourceFileName);

        std::filesystem::path path(filePath);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path, std::ios::binary);
        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        VAudioFileHeader header {};
        std::memcpy(header.magic, kAudioMagic, std::strlen(kAudioMagic) + 1);
        header.rawSize = raw.size();
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!raw.empty())
            file.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        return file ? vbase::Result<void, AssetError>::ok()
                    : vbase::Result<void, AssetError>::err(AssetError::eIOError);
    }

    vbase::Result<void, AssetError> loadAudio(vbase::StringView filePath, VAudio& outAudio)
    {
        std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        file.seekg(0, std::ios::end);
        const auto size = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);
        std::vector<std::byte> data(size);
        if (size != 0u)
            file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);
        return loadAudioFromMemory(data, outAudio);
    }

    vbase::Result<void, AssetError> loadAudioFromMemory(const std::vector<std::byte>& data, VAudio& outAudio)
    {
        if (data.size() < sizeof(VAudioFileHeader))
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        VAudioFileHeader header {};
        std::memcpy(&header, data.data(), sizeof(header));
        if (std::string(header.magic) != kAudioMagic || header.version != 1u)
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        const size_t payloadOffset = sizeof(VAudioFileHeader);
        if (payloadOffset + header.rawSize > data.size())
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        std::vector<uint8_t> raw(static_cast<size_t>(header.rawSize));
        if (header.rawSize != 0u)
            std::memcpy(raw.data(), data.data() + payloadOffset, static_cast<size_t>(header.rawSize));

        size_t offset = 0;
        outAudio      = {};
        uint32_t storage = 0;
        if (!readRaw(raw, offset, &outAudio.uuid, sizeof(outAudio.uuid)) ||
            !readString(raw, offset, outAudio.name) || !readRaw(raw, offset, &storage, sizeof(storage)) ||
            !readRaw(raw, offset, &outAudio.sampleRate, sizeof(outAudio.sampleRate)) ||
            !readRaw(raw, offset, &outAudio.channels, sizeof(outAudio.channels)) ||
            !readRaw(raw, offset, &outAudio.frameCount, sizeof(outAudio.frameCount)) ||
            !readRaw(raw, offset, &outAudio.duration, sizeof(outAudio.duration)) ||
            !readBytes(raw, offset, outAudio.audioData) || !readString(raw, offset, outAudio.sourceFileName))
        {
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);
        }
        outAudio.storage = static_cast<VAudioStorage>(storage);
        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset
