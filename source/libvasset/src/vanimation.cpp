#include "vasset/vanimation.hpp"

#include <filesystem>
#include <fstream>
#include <cstring>

namespace vasset
{
    namespace
    {
        struct VAnimFileHeader
        {
            char     magic[16];
            uint32_t version {1};
            uint32_t flags {0};
            uint64_t rawSize {0};
        };

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

        vbase::Result<void, AssetError> writeContainer(vbase::StringView filePath,
                                                       const char*       magic,
                                                       const std::vector<uint8_t>& raw)
        {
            std::filesystem::path path(filePath);
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path());

            std::ofstream file(path, std::ios::binary);
            if (!file)
                return vbase::Result<void, AssetError>::err(AssetError::eIOError);

            VAnimFileHeader header {};
            std::memcpy(header.magic, magic, std::strlen(magic) + 1);
            header.rawSize = raw.size();
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            if (!raw.empty())
                file.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
            return file ? vbase::Result<void, AssetError>::ok()
                        : vbase::Result<void, AssetError>::err(AssetError::eIOError);
        }

        vbase::Result<std::vector<uint8_t>, AssetError> readContainer(const std::vector<std::byte>& data,
                                                                      const char* magic)
        {
            if (data.size() < sizeof(VAnimFileHeader))
                return vbase::Result<std::vector<uint8_t>, AssetError>::err(AssetError::eIOError);

            VAnimFileHeader header {};
            std::memcpy(&header, data.data(), sizeof(header));
            if (std::string(header.magic) != magic || header.version != 1u)
                return vbase::Result<std::vector<uint8_t>, AssetError>::err(AssetError::eInvalidFormat);

            const size_t payloadOffset = sizeof(VAnimFileHeader);
            if (payloadOffset + header.rawSize > data.size())
                return vbase::Result<std::vector<uint8_t>, AssetError>::err(AssetError::eIOError);

            std::vector<uint8_t> raw(static_cast<size_t>(header.rawSize));
            if (header.rawSize != 0u)
                std::memcpy(raw.data(), data.data() + payloadOffset, static_cast<size_t>(header.rawSize));
            return vbase::Result<std::vector<uint8_t>, AssetError>::ok(std::move(raw));
        }

        vbase::Result<std::vector<std::byte>, AssetError> readFile(vbase::StringView filePath)
        {
            std::ifstream file(std::filesystem::path(filePath), std::ios::binary);
            if (!file)
                return vbase::Result<std::vector<std::byte>, AssetError>::err(AssetError::eNotFound);

            file.seekg(0, std::ios::end);
            const auto size = static_cast<size_t>(file.tellg());
            file.seekg(0, std::ios::beg);
            std::vector<std::byte> data(size);
            if (size != 0u)
                file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
            return file ? vbase::Result<std::vector<std::byte>, AssetError>::ok(std::move(data))
                        : vbase::Result<std::vector<std::byte>, AssetError>::err(AssetError::eIOError);
        }
    } // namespace

    vbase::Result<void, AssetError> saveSkeleton(const VSkeleton& skeleton, vbase::StringView filePath)
    {
        std::vector<uint8_t> raw;
        writeRaw(raw, &skeleton.uuid, sizeof(skeleton.uuid));
        writeString(raw, skeleton.name);
        const uint32_t jointCount = static_cast<uint32_t>(skeleton.jointNames.size());
        writeRaw(raw, &jointCount, sizeof(jointCount));
        for (uint32_t i = 0; i < jointCount; ++i)
        {
            writeString(raw, skeleton.jointNames[i]);
            const int16_t parent = i < skeleton.jointParents.size() ? skeleton.jointParents[i] : -1;
            writeRaw(raw, &parent, sizeof(parent));
        }
        writeBytes(raw, skeleton.ozzData);
        return writeContainer(filePath, "VSKEL", raw);
    }

    vbase::Result<void, AssetError> loadSkeleton(vbase::StringView filePath, VSkeleton& outSkeleton)
    {
        auto data = readFile(filePath);
        if (!data)
            return vbase::Result<void, AssetError>::err(data.error());
        return loadSkeletonFromMemory(data.value(), outSkeleton);
    }

    vbase::Result<void, AssetError> loadSkeletonFromMemory(const std::vector<std::byte>& data, VSkeleton& outSkeleton)
    {
        auto rawResult = readContainer(data, "VSKEL");
        if (!rawResult)
            return vbase::Result<void, AssetError>::err(rawResult.error());

        const auto& raw = rawResult.value();
        size_t      offset = 0;
        outSkeleton = {};
        if (!readRaw(raw, offset, &outSkeleton.uuid, sizeof(outSkeleton.uuid)) ||
            !readString(raw, offset, outSkeleton.name))
        {
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);
        }

        uint32_t jointCount = 0;
        if (!readRaw(raw, offset, &jointCount, sizeof(jointCount)))
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);
        outSkeleton.jointNames.resize(jointCount);
        outSkeleton.jointParents.resize(jointCount);
        for (uint32_t i = 0; i < jointCount; ++i)
        {
            if (!readString(raw, offset, outSkeleton.jointNames[i]) ||
                !readRaw(raw, offset, &outSkeleton.jointParents[i], sizeof(outSkeleton.jointParents[i])))
            {
                return vbase::Result<void, AssetError>::err(AssetError::eIOError);
            }
        }
        if (!readBytes(raw, offset, outSkeleton.ozzData))
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);
        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> saveAnimation(const VAnimation& animation, vbase::StringView filePath)
    {
        std::vector<uint8_t> raw;
        writeRaw(raw, &animation.uuid, sizeof(animation.uuid));
        writeString(raw, animation.name);
        writeRaw(raw, &animation.duration, sizeof(animation.duration));
        writeBytes(raw, animation.ozzData);
        return writeContainer(filePath, "VANIM", raw);
    }

    vbase::Result<void, AssetError> loadAnimation(vbase::StringView filePath, VAnimation& outAnimation)
    {
        auto data = readFile(filePath);
        if (!data)
            return vbase::Result<void, AssetError>::err(data.error());
        return loadAnimationFromMemory(data.value(), outAnimation);
    }

    vbase::Result<void, AssetError> loadAnimationFromMemory(const std::vector<std::byte>& data, VAnimation& outAnimation)
    {
        auto rawResult = readContainer(data, "VANIM");
        if (!rawResult)
            return vbase::Result<void, AssetError>::err(rawResult.error());

        const auto& raw = rawResult.value();
        size_t      offset = 0;
        outAnimation = {};
        if (!readRaw(raw, offset, &outAnimation.uuid, sizeof(outAnimation.uuid)) ||
            !readString(raw, offset, outAnimation.name) ||
            !readRaw(raw, offset, &outAnimation.duration, sizeof(outAnimation.duration)) ||
            !readBytes(raw, offset, outAnimation.ozzData))
        {
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);
        }
        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset
