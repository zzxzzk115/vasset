#include "vasset/vgaussiansplat.hpp"

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace vasset
{
    namespace
    {
        struct VGaussianSplatContainerHeader
        {
            char     magic[16]; // "VGAUSSIANSPLAT"
            uint32_t version;   // 2
            uint32_t flags;     // bit0 = compressed
            uint64_t rawSize;   // uncompressed payload size
        };

        struct VGaussianSplatPayloadHeader
        {
            char     magic[16]; // "VGAUSSIANSPLAT"
            uint32_t version;   // 2 = base splat, 3 = flat LOD metadata streams
        };

        constexpr uint32_t kGaussianSplatPayloadVersion            = 3;
        constexpr uint32_t kMinGaussianSplatPayloadVersion         = 2;
        constexpr uint32_t kMaxReadableGaussianSplatPayloadVersion = 4; // v4 hierarchy sidecars are ignored.

        void sanitizeGaussianSplatLod(VGaussianSplat& splat)
        {
            auto& lod = splat.lod;

            const size_t pointCount = splat.numPoints > 0 ? static_cast<size_t>(splat.numPoints) : 0u;
            // Per-point streams must stay aligned with the raw Gaussian payload.
            // Misaligned sidecars are safer to drop than to serialize, because the
            // renderer turns importance directly into local point indices.
            auto clearIfNotPerPoint = [pointCount](auto& values) {
                if (!values.empty() && values.size() != pointCount)
                    values.clear();
            };

            clearIfNotPerPoint(lod.importance);
            clearIfNotPerPoint(lod.lodLevel);
            clearIfNotPerPoint(lod.clusterId);

            if (!lod.hasAnyData())
            {
                lod.type = VGaussianSplatLodType::eNone;
                return;
            }

            switch (lod.type)
            {
                case VGaussianSplatLodType::eFlatImportance:
                    break;
                case VGaussianSplatLodType::eNone:
                default:
                    lod.type = VGaussianSplatLodType::eFlatImportance;
                    break;
            }
        }

    } // namespace

    vbase::Result<void, AssetError>
    saveGaussianSplat(const VGaussianSplat& splat, vbase::StringView filePath, int zstdLevel)
    {
        // ---- Prepare output directory ----
        std::filesystem::path path(filePath);
        if (path.has_parent_path() && !std::filesystem::exists(path.parent_path()))
        {
            std::filesystem::create_directories(path.parent_path());
        }

        // ---- Serialize into memory ----
        std::vector<uint8_t> raw;
        raw.reserve(64 * splat.numPoints + 256);

        auto writeRaw = [&](const void* data, size_t size) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
            raw.insert(raw.end(), p, p + size);
        };

        // payload header (version 2 = cloud format)
        VGaussianSplatPayloadHeader payloadHeader {};
        std::memcpy(payloadHeader.magic, "VGAUSSIANSPLAT", 15);
        payloadHeader.version = kGaussianSplatPayloadVersion;
        writeRaw(&payloadHeader, sizeof(payloadHeader));

        // UUID
        writeRaw(&splat.uuid, sizeof(splat.uuid));

        // name
        uint32_t nameLen = static_cast<uint32_t>(splat.name.size());
        writeRaw(&nameLen, sizeof(nameLen));
        if (nameLen > 0)
            writeRaw(splat.name.c_str(), nameLen);

        // metadata
        int32_t numPoints   = splat.numPoints;
        int32_t shDegree    = splat.shDegree;
        uint8_t antialiased = splat.antialiased ? 1u : 0u;
        writeRaw(&numPoints,   sizeof(numPoints));
        writeRaw(&shDegree,    sizeof(shDegree));
        writeRaw(&antialiased, sizeof(antialiased));

        // per-splat array
        if (numPoints > 0)
            writeRaw(splat.splats.data(), static_cast<size_t>(numPoints) * sizeof(VGaussianSplatPoint));

        // higher-order SH coefficients
        uint32_t shCount = static_cast<uint32_t>(splat.sh.size());
        writeRaw(&shCount, sizeof(shCount));
        if (shCount > 0)
            writeRaw(splat.sh.data(), shCount * sizeof(float));

        // Optional LOD sidecar. Kept after the base splat payload so older payload
        // v2 assets remain readable; the main engine can ignore the sidecar and
        // still load/render the raw splats.
        const auto& lod     = splat.lod;
        uint32_t    lodType = static_cast<uint32_t>(lod.hasAnyData() ? lod.type : VGaussianSplatLodType::eNone);
        writeRaw(&lodType, sizeof(lodType));

        auto writeVector = [&]<typename T>(const std::vector<T>& values) {
            uint32_t count = static_cast<uint32_t>(std::min<size_t>(values.size(), std::numeric_limits<uint32_t>::max()));
            writeRaw(&count, sizeof(count));
            if (count > 0)
                writeRaw(values.data(), static_cast<size_t>(count) * sizeof(T));
        };

        writeVector(lod.importance);
        writeVector(lod.lodLevel);
        writeVector(lod.clusterId);

        // ---- Write to file ----
        std::ofstream file((std::string(filePath)), std::ios::binary);
        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        VGaussianSplatContainerHeader containerHeader {};
        std::memcpy(containerHeader.magic, "VGAUSSIANSPLAT", 15);
        containerHeader.version = 2;
        containerHeader.flags   = (zstdLevel > 0) ? 1u : 0u;
        containerHeader.rawSize = raw.size();

        file.write(reinterpret_cast<const char*>(&containerHeader), sizeof(containerHeader));

        if (zstdLevel <= 0)
        {
            file.write(reinterpret_cast<const char*>(raw.data()), raw.size());
        }
        else
        {
            size_t const         bound = ZSTD_compressBound(raw.size());
            std::vector<uint8_t> comp(bound);

            size_t const cSize = ZSTD_compress(comp.data(), bound, raw.data(), raw.size(), zstdLevel);
            if (ZSTD_isError(cSize))
                return vbase::Result<void, AssetError>::err(AssetError::eIOError);

            file.write(reinterpret_cast<const char*>(comp.data()), cSize);
        }

        file.close();

        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> loadGaussianSplat(vbase::StringView filePath, VGaussianSplat& outSplat)
    {
        std::filesystem::path path(filePath);

        if (!std::filesystem::exists(path))
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        const auto size = std::filesystem::file_size(path);
        if (size == 0)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        std::vector<std::byte> buffer(size);

        std::ifstream file(path, std::ios::binary);
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));

        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        return loadGaussianSplatFromMemory(buffer, outSplat);
    }

    vbase::Result<void, AssetError> loadGaussianSplatFromMemory(const std::vector<std::byte>& data,
                                                                VGaussianSplat&               outSplat)
    {
        if (data.size() < sizeof(VGaussianSplatPayloadHeader))
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        std::vector<uint8_t> raw;

        if (data.size() >= sizeof(VGaussianSplatContainerHeader))
        {
            size_t containerOffset = 0;
            auto   readContainer   = [&](void* dst, size_t size) -> bool {
                if (containerOffset + size > data.size())
                    return false;

                std::memcpy(dst, data.data() + containerOffset, size);
                containerOffset += size;
                return true;
            };

            VGaussianSplatContainerHeader containerHeader {};
            if (!readContainer(&containerHeader, sizeof(containerHeader)))
                return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

            if (std::string(containerHeader.magic) != "VGAUSSIANSPLAT")
                return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

            if (containerHeader.version >= 2)
            {
                bool compressed = (containerHeader.flags & 1u) != 0;
                raw.resize(static_cast<size_t>(containerHeader.rawSize));

                if (!compressed)
                {
                    if (containerOffset + raw.size() > data.size())
                        return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

                    std::memcpy(raw.data(), data.data() + containerOffset, raw.size());
                }
                else
                {
                    size_t      compSize = data.size() - containerOffset;
                    const void* compData = data.data() + containerOffset;

                    size_t dSize = ZSTD_decompress(raw.data(), raw.size(), compData, compSize);
                    if (ZSTD_isError(dSize) || dSize != raw.size())
                        return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
                }
            }
            else
            {
                raw.resize(data.size());
                std::memcpy(raw.data(), data.data(), data.size());
            }
        }
        else
        {
            raw.resize(data.size());
            std::memcpy(raw.data(), data.data(), data.size());
        }

        size_t payloadOffset = 0;
        auto   readPayload   = [&](void* dst, size_t size) -> bool {
            if (payloadOffset + size > raw.size())
                return false;

            std::memcpy(dst, raw.data() + payloadOffset, size);
            payloadOffset += size;
            return true;
        };

        VGaussianSplatPayloadHeader payloadHeader {};
        if (!readPayload(&payloadHeader, sizeof(payloadHeader)))
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        if (std::string(payloadHeader.magic) != "VGAUSSIANSPLAT")
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        if (payloadHeader.version < kMinGaussianSplatPayloadVersion ||
            payloadHeader.version > kMaxReadableGaussianSplatPayloadVersion)
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        // UUID
        if (!readPayload(&outSplat.uuid, sizeof(outSplat.uuid)))
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        // name
        uint32_t nameLen = 0;
        if (!readPayload(&nameLen, sizeof(nameLen)))
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
        outSplat.name.resize(nameLen);
        if (nameLen > 0 && !readPayload(outSplat.name.data(), nameLen))
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        // metadata
        int32_t numPoints   = 0;
        int32_t shDegree    = 0;
        uint8_t antialiased = 0;
        if (!readPayload(&numPoints,   sizeof(numPoints)))   return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
        if (!readPayload(&shDegree,    sizeof(shDegree)))    return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
        if (!readPayload(&antialiased, sizeof(antialiased))) return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        outSplat.numPoints   = numPoints;
        outSplat.shDegree    = shDegree;
        outSplat.antialiased = antialiased != 0;

        // per-splat array
        outSplat.splats.resize(numPoints);
        if (numPoints > 0 && !readPayload(outSplat.splats.data(), static_cast<size_t>(numPoints) * sizeof(VGaussianSplatPoint)))
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        // higher-order SH coefficients
        uint32_t shCount = 0;
        if (!readPayload(&shCount, sizeof(shCount)))
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
        outSplat.sh.resize(shCount);
        if (shCount > 0 && !readPayload(outSplat.sh.data(), shCount * sizeof(float)))
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        outSplat.lod = {};
        if (payloadHeader.version >= 3)
        {
            uint32_t lodType = 0;
            if (!readPayload(&lodType, sizeof(lodType)))
                return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
            outSplat.lod.type = static_cast<VGaussianSplatLodType>(lodType);

            auto readVector = [&]<typename T>(std::vector<T>& values) -> bool {
                uint32_t count = 0;
                if (!readPayload(&count, sizeof(count)))
                    return false;
                values.resize(count);
                return count == 0 || readPayload(values.data(), static_cast<size_t>(count) * sizeof(T));
            };

            if (!readVector(outSplat.lod.importance))
                return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
            if (!readVector(outSplat.lod.lodLevel))
                return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
            if (!readVector(outSplat.lod.clusterId))
                return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);
        }

        sanitizeGaussianSplatLod(outSplat);

        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset
