#include "vasset/vpk.hpp"

#include <xxhash.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace vasset
{
    static constexpr uint32_t VPK_VERSION = 3;

    static inline uint64_t hash64(std::string_view s) { return XXH3_64bits(s.data(), s.size()); }

    static inline bool is_already_compressed_path(std::string_view path)
    {
        auto lower = std::string(path);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        auto has = [&](const char* ext) {
            if (lower.size() < std::strlen(ext))
                return false;
            return lower.compare(lower.size() - std::strlen(ext), std::strlen(ext), ext) == 0;
        };

        return has(".ktx2") || has(".dds") || has(".jpg") || has(".jpeg");
    }

    static inline bool is_vmesh_already_compressed(vbase::ConstByteSpan bytes)
    {
        if (bytes.size() < 16 + 4 + 4 + 8)
            return false;

        const char* m = reinterpret_cast<const char*>(bytes.data());
        if (std::memcmp(m, "VMESH", 5) != 0)
            return false;

        uint32_t flags = 0;
        std::memcpy(&flags, reinterpret_cast<const uint8_t*>(bytes.data()) + 16 + 4, sizeof(uint32_t));
        return (flags & 0x1u) != 0;
    }

    struct VpkHeaderV1
    {
        char     magic[4];
        uint32_t version;
        uint32_t flags;
        uint32_t fileCount;
        uint64_t indexOffset;
        uint64_t indexSize;
        uint64_t stringOffset;
        uint64_t stringSize;
        uint64_t dataOffset;
    };

    struct VpkAssetRegistryEntryV2
    {
        vbase::UUID uuid;
        uint32_t    pathOffset = 0;
        uint32_t    pathSize   = 0;
    };

    namespace
    {
        // Parse VPK metadata (header/index/string-table/registry) via a random-access byte reader.
        // readAt(offset, dst, n) must copy n bytes at file offset `offset` and return false on any
        // out-of-range / short read. Shared by the on-disk and in-memory open paths.
        template<typename ReadAt>
        vbase::Result<VpkReadOnly, AssetError> parseVpk(ReadAt&& readAt)
        {
            VpkReadOnly out {};

            char     magic[4] = {};
            uint32_t version  = 0;
            if (!readAt(0, magic, 4) || !readAt(4, &version, sizeof(uint32_t)))
                return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

            if (std::memcmp(magic, "VPK\0", 4) != 0)
                return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

            if (version == 1)
            {
                VpkHeaderV1 h1 {};
                if (!readAt(0, &h1, sizeof(VpkHeaderV1)))
                    return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

                std::memcpy(out.header.magic, h1.magic, 4);
                out.header.version      = h1.version;
                out.header.flags        = h1.flags;
                out.header.fileCount    = h1.fileCount;
                out.header.indexOffset  = h1.indexOffset;
                out.header.indexSize    = h1.indexSize;
                out.header.stringOffset = h1.stringOffset;
                out.header.stringSize   = h1.stringSize;
                out.header.dataOffset   = h1.dataOffset;

                out.header.registryOffset = 0;
                out.header.registrySize   = 0;
                out.header.registryCount  = 0;
            }
            else if (version == 2 || version == VPK_VERSION)
            {
                if (!readAt(0, &out.header, sizeof(VpkHeader)))
                    return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);
            }
            else
            {
                return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);
            }

            // Index
            out.entries.resize(out.header.fileCount);
            if (out.header.indexSize > 0 &&
                !readAt(out.header.indexOffset, out.entries.data(), static_cast<size_t>(out.header.indexSize)))
                return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

            // String table
            out.stringTable.resize(static_cast<size_t>(out.header.stringSize));
            if (out.header.stringSize > 0 &&
                !readAt(out.header.stringOffset, out.stringTable.data(), static_cast<size_t>(out.header.stringSize)))
                return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

            // Registry (v2+)
            if (out.header.registryCount > 0 && out.header.registrySize > 0)
            {
                if (version == 2)
                {
                    std::vector<VpkAssetRegistryEntryV2> registryV2;
                    registryV2.resize(static_cast<size_t>(out.header.registryCount));
                    if (!readAt(out.header.registryOffset,
                                registryV2.data(),
                                static_cast<size_t>(out.header.registrySize)))
                        return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

                    out.registry.reserve(registryV2.size());
                    for (const auto& r2 : registryV2)
                    {
                        VpkAssetRegistryEntry r {};
                        r.uuid       = r2.uuid;
                        r.pathOffset = r2.pathOffset;
                        r.pathSize   = r2.pathSize;
                        out.registry.push_back(r);
                    }
                }
                else
                {
                    out.registry.resize(static_cast<size_t>(out.header.registryCount));
                    if (!readAt(out.header.registryOffset,
                                out.registry.data(),
                                static_cast<size_t>(out.header.registrySize)))
                        return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);
                }
            }

            for (uint32_t i = 0; i < static_cast<uint32_t>(out.entries.size()); ++i)
                out.buckets[out.entries[i].pathHash64].push_back(i);

            return vbase::Result<VpkReadOnly, AssetError>::ok(std::move(out));
        }

        // Decompress (or copy) a packed entry payload into its raw bytes.
        vbase::Result<std::vector<std::byte>, AssetError> unpackEntry(const VpkEntry& e, std::vector<std::byte> packed)
        {
            if (e.compression == VpkCompression::eNone)
                return vbase::Result<std::vector<std::byte>, AssetError>::ok(std::move(packed));

            if (e.compression != VpkCompression::eZstd)
                return vbase::Result<std::vector<std::byte>, AssetError>::err(AssetError::eNotSupported);

            std::vector<std::byte> raw;
            raw.resize(static_cast<size_t>(e.rawSize));

            const size_t r = ZSTD_decompress(raw.data(), raw.size(), packed.data(), packed.size());
            if (ZSTD_isError(r) || r != raw.size())
                return vbase::Result<std::vector<std::byte>, AssetError>::err(AssetError::eInvalidFormat);

            return vbase::Result<std::vector<std::byte>, AssetError>::ok(std::move(raw));
        }
    } // namespace

    vbase::Result<VpkReadOnly, AssetError> openVpk(vbase::StringView vpkPath)
    {
        std::ifstream f(std::string(vpkPath), std::ios::binary);
        if (!f)
            return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eNotFound);

        return parseVpk([&f](uint64_t offset, void* dst, size_t n) -> bool {
            f.clear();
            f.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            f.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
            return static_cast<bool>(f);
        });
    }

    vbase::Result<VpkReadOnly, AssetError> openVpkFromMemory(vbase::ConstByteSpan blob)
    {
        return parseVpk([blob](uint64_t offset, void* dst, size_t n) -> bool {
            if (offset > blob.size() || n > blob.size() - offset)
                return false;
            std::memcpy(dst, blob.data() + offset, n);
            return true;
        });
    }

    static vbase::Result<const VpkEntry*, AssetError> find_entry(const VpkReadOnly& vpk, std::string_view logicalPath)
    {
        const uint64_t h  = hash64(logicalPath);
        auto           it = vpk.buckets.find(h);
        if (it == vpk.buckets.end())
            return vbase::Result<const VpkEntry*, AssetError>::err(AssetError::eNotFound);

        for (uint32_t idx : it->second)
        {
            const auto& e = vpk.entries[idx];
            if (e.pathOffset + e.pathSize > vpk.stringTable.size())
                continue;

            const char* s = vpk.stringTable.data() + e.pathOffset;
            if (std::string_view(s, e.pathSize) == logicalPath)
                return vbase::Result<const VpkEntry*, AssetError>::ok(&e);
        }

        return vbase::Result<const VpkEntry*, AssetError>::err(AssetError::eNotFound);
    }

    vbase::Result<std::vector<std::byte>, AssetError>
    readVpkFile(const VpkReadOnly& vpk, vbase::StringView vpkPath, vbase::StringView logicalPath)
    {
        if (!logicalPath.empty() && logicalPath.front() == '/')
            logicalPath.remove_prefix(1);

        auto fe = find_entry(vpk, std::string_view(logicalPath.data(), logicalPath.size()));
        if (!fe)
            return vbase::Result<std::vector<std::byte>, AssetError>::err(fe.error());

        const VpkEntry& e = *fe.value();

        std::ifstream f(std::string(vpkPath), std::ios::binary);
        if (!f)
            return vbase::Result<std::vector<std::byte>, AssetError>::err(AssetError::eNotFound);

        f.seekg(static_cast<std::streamoff>(e.dataOffset), std::ios::beg);

        std::vector<std::byte> packed;
        packed.resize(static_cast<size_t>(e.packedSize));
        if (e.packedSize > 0)
        {
            f.read(reinterpret_cast<char*>(packed.data()), static_cast<std::streamsize>(e.packedSize));
            if (!f)
                return vbase::Result<std::vector<std::byte>, AssetError>::err(AssetError::eIOError);
        }

        return unpackEntry(e, std::move(packed));
    }

    vbase::Result<std::vector<std::byte>, AssetError>
    readVpkFileFromMemory(const VpkReadOnly& vpk, vbase::ConstByteSpan blob, vbase::StringView logicalPath)
    {
        if (!logicalPath.empty() && logicalPath.front() == '/')
            logicalPath.remove_prefix(1);

        auto fe = find_entry(vpk, std::string_view(logicalPath.data(), logicalPath.size()));
        if (!fe)
            return vbase::Result<std::vector<std::byte>, AssetError>::err(fe.error());

        const VpkEntry& e = *fe.value();

        if (e.dataOffset > blob.size() || e.packedSize > blob.size() - e.dataOffset)
            return vbase::Result<std::vector<std::byte>, AssetError>::err(AssetError::eInvalidFormat);

        std::vector<std::byte> packed;
        packed.resize(static_cast<size_t>(e.packedSize));
        if (e.packedSize > 0)
            std::memcpy(packed.data(), blob.data() + e.dataOffset, static_cast<size_t>(e.packedSize));

        return unpackEntry(e, std::move(packed));
    }

    vbase::Result<void, AssetError>
    writeVpk(vbase::StringView outPath, const std::vector<VpkWriteItem>& items, int zstdLevel)
    {
        std::filesystem::path p(outPath);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());

        std::ofstream f(std::string(outPath), std::ios::binary);
        if (!f)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        // String table
        std::string strtab;
        strtab.reserve(1024);

        std::vector<VpkEntry> entries;
        entries.reserve(items.size());

        std::vector<VpkAssetRegistryEntry> registry;
        registry.reserve(items.size());

        // Header placeholder
        VpkHeader hdr {};
        std::memcpy(hdr.magic, "VPK\0", 4);
        hdr.version   = VPK_VERSION;
        hdr.flags     = 0;
        hdr.fileCount = static_cast<uint32_t>(items.size());

        f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

        hdr.dataOffset   = sizeof(hdr);
        uint64_t curData = hdr.dataOffset;

        // Data blob
        for (const auto& it : items)
        {
            VpkEntry e {};
            e.pathHash64 = hash64(it.logicalPath);

            e.pathOffset = static_cast<uint32_t>(strtab.size());
            e.pathSize   = static_cast<uint32_t>(it.logicalPath.size());
            strtab.append(it.logicalPath.data(), it.logicalPath.size());
            strtab.push_back('\0');

            // Registry entry: UUID -> (string table path)
            VpkAssetRegistryEntry r {};
            r.uuid       = it.uuid;
            r.pathOffset = e.pathOffset;
            r.pathSize   = e.pathSize;
            r.type       = it.type;
            registry.push_back(r);

            vbase::ConstByteSpan bytes {it.bytes.data(), it.bytes.size()};
            const bool already    = is_already_compressed_path(it.logicalPath) || is_vmesh_already_compressed(bytes);
            const bool doCompress = it.allowCompress && !already && !it.bytes.empty();

            std::vector<std::byte> packed;
            if (doCompress)
            {
                const size_t bound = ZSTD_compressBound(it.bytes.size());
                packed.resize(bound);
                const size_t sz =
                    ZSTD_compress(packed.data(), packed.size(), it.bytes.data(), it.bytes.size(), zstdLevel);
                if (ZSTD_isError(sz))
                    return vbase::Result<void, AssetError>::err(AssetError::eIOError);
                packed.resize(sz);

                e.compression = VpkCompression::eZstd;
                e.rawSize     = static_cast<uint64_t>(it.bytes.size());
                e.packedSize  = static_cast<uint64_t>(packed.size());
            }
            else
            {
                e.compression = VpkCompression::eNone;
                e.rawSize     = static_cast<uint64_t>(it.bytes.size());
                e.packedSize  = static_cast<uint64_t>(it.bytes.size());
            }

            e.dataOffset = curData;

            if (doCompress)
            {
                f.write(reinterpret_cast<const char*>(packed.data()), static_cast<std::streamsize>(packed.size()));
                curData += static_cast<uint64_t>(packed.size());
            }
            else
            {
                if (!it.bytes.empty())
                {
                    f.write(reinterpret_cast<const char*>(it.bytes.data()),
                            static_cast<std::streamsize>(it.bytes.size()));
                    curData += static_cast<uint64_t>(it.bytes.size());
                }
            }

            entries.push_back(e);
        }

        // String table
        hdr.stringOffset = curData;
        hdr.stringSize   = static_cast<uint64_t>(strtab.size());
        if (!strtab.empty())
        {
            f.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));
            curData += static_cast<uint64_t>(strtab.size());
        }

        // Index
        hdr.indexOffset = curData;
        hdr.indexSize   = static_cast<uint32_t>(entries.size() * sizeof(VpkEntry));
        if (!entries.empty())
        {
            f.write(reinterpret_cast<const char*>(entries.data()), static_cast<std::streamsize>(hdr.indexSize));
            curData += hdr.indexSize;
        }

        // Registry
        hdr.registryOffset = curData;
        hdr.registrySize   = static_cast<uint32_t>(registry.size() * sizeof(VpkAssetRegistryEntry));
        hdr.registryCount  = static_cast<uint32_t>(registry.size());
        if (!registry.empty())
        {
            f.write(reinterpret_cast<const char*>(registry.data()), static_cast<std::streamsize>(hdr.registrySize));
            curData += hdr.registrySize;
        }

        // Patch header
        f.seekp(0, std::ios::beg);
        f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset
