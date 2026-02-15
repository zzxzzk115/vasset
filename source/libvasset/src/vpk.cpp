#include "vasset/vpk.hpp"

#include <xxhash.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace vasset
{
    static constexpr uint32_t VPK_VERSION = 1;

    static inline uint64_t hash64(std::string_view s) { return XXH3_64bits(s.data(), s.size()); }

    static inline bool is_already_compressed_path(std::string_view path)
    {
        // Common already-compressed formats.
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
        // VMESH header: magic[16], version, flags, rawSize
        // flags bit0 = compressed (as per your vmesh design)
        if (bytes.size() < 16 + 4 + 4 + 8)
            return false;

        const char* m = reinterpret_cast<const char*>(bytes.data());
        if (std::memcmp(m, "VMESH", 5) != 0)
            return false;

        uint32_t flags = 0;
        std::memcpy(&flags, reinterpret_cast<const uint8_t*>(bytes.data()) + 16 + 4, sizeof(uint32_t));
        return (flags & 0x1u) != 0;
    }

    vbase::Result<VpkReadOnly, AssetError> openVpk(vbase::StringView vpkPath)
    {
        std::ifstream f(std::string(vpkPath), std::ios::binary);
        if (!f)
            return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eNotFound);

        VpkReadOnly out {};

        f.read(reinterpret_cast<char*>(&out.header), sizeof(VpkHeader));
        if (!f)
            return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

        if (std::memcmp(out.header.magic, "VPK\0", 4) != 0 || out.header.version != VPK_VERSION)
            return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

        out.entries.resize(out.header.fileCount);

        f.seekg(static_cast<std::streamoff>(out.header.indexOffset), std::ios::beg);
        f.read(reinterpret_cast<char*>(out.entries.data()), static_cast<std::streamsize>(out.header.indexSize));
        if (!f)
            return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

        out.stringTable.resize(static_cast<size_t>(out.header.stringSize));
        f.seekg(static_cast<std::streamoff>(out.header.stringOffset), std::ios::beg);
        f.read(out.stringTable.data(), static_cast<std::streamsize>(out.header.stringSize));
        if (!f)
            return vbase::Result<VpkReadOnly, AssetError>::err(AssetError::eInvalidFormat);

        // Build buckets
        for (uint32_t i = 0; i < static_cast<uint32_t>(out.entries.size()); ++i)
            out.buckets[out.entries[i].pathHash64].push_back(i);

        return vbase::Result<VpkReadOnly, AssetError>::ok(std::move(out));
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
        // Remove the first '/' if present, since logical paths in VPK are stored without a leading slash.
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

    vbase::Result<void, AssetError>
    writeVpk(vbase::StringView outPath, const std::vector<VpkWriteItem>& items, int zstdLevel)
    {
        std::filesystem::path p(outPath);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());

        std::ofstream f(std::string(outPath), std::ios::binary);
        if (!f)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        // Build string table
        std::string strtab;
        strtab.reserve(1024);

        std::vector<VpkEntry> entries;
        entries.reserve(items.size());

        // We'll write data first after reserving header space.
        VpkHeader hdr {};
        std::memcpy(hdr.magic, "VPK\0", 4);
        hdr.version   = VPK_VERSION;
        hdr.flags     = 0;
        hdr.fileCount = static_cast<uint32_t>(items.size());

        f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr)); // placeholder

        // Data blob starts right after header.
        hdr.dataOffset   = sizeof(hdr);
        uint64_t curData = hdr.dataOffset;

        for (const auto& it : items)
        {
            VpkEntry e {};
            e.pathHash64 = hash64(it.logicalPath);

            e.pathOffset = static_cast<uint32_t>(strtab.size());
            e.pathSize   = static_cast<uint32_t>(it.logicalPath.size());
            strtab.append(it.logicalPath.data(), it.logicalPath.size());
            strtab.push_back('\0');

            vbase::ConstByteSpan bytes {it.bytes.data(), it.bytes.size()};
            const bool already = is_already_compressed_path(it.logicalPath) || is_vmesh_already_compressed(bytes);

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

        // Write string table
        hdr.stringOffset = curData;
        hdr.stringSize   = static_cast<uint64_t>(strtab.size());
        if (!strtab.empty())
        {
            f.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));
            curData += static_cast<uint64_t>(strtab.size());
        }

        // Write index
        hdr.indexOffset = curData;
        hdr.indexSize   = static_cast<uint32_t>(entries.size() * sizeof(VpkEntry));
        if (!entries.empty())
        {
            f.write(reinterpret_cast<const char*>(entries.data()), static_cast<std::streamsize>(hdr.indexSize));
            curData += hdr.indexSize;
        }

        // Patch header
        f.seekp(0, std::ios::beg);
        f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset
