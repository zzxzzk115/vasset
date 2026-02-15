#pragma once

#include "vasset/asset_error.hpp"

#include <vfilesystem/interfaces/ifile.hpp>
#include <vfilesystem/interfaces/ifilesystem.hpp>

#include <vbase/core/result.hpp>
#include <vbase/core/span.hpp>
#include <vbase/core/string_view.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vasset
{
    // VPK: Vultra asset package (path -> cooked bytes).
    //
    // Notes:
    // - Key is the logical source path (e.g. "res://sprites/a.png").
    // - Data is typically the cooked asset bytes (e.g. .vtex / .vmesh).
    // - Per-entry compression is supported. Already-compressed assets should be stored uncompressed.
    //
    // File layout (v1):
    // [Header][Index][StringTable][DataBlob]
    struct VpkHeader
    {
        char     magic[4]; // "VPK\0"
        uint32_t version;  // 1
        uint32_t flags;    // reserved
        uint32_t fileCount;
        uint64_t indexOffset;
        uint64_t indexSize;
        uint64_t stringOffset;
        uint64_t stringSize;
        uint64_t dataOffset;
    };

    enum class VpkCompression : uint8_t
    {
        eNone = 0,
        eZstd = 1,
    };

    struct VpkEntry
    {
        uint64_t       pathHash64  = 0;
        uint32_t       pathOffset  = 0; // offset into string table
        uint32_t       pathSize    = 0; // bytes (not including null)
        uint64_t       dataOffset  = 0; // absolute file offset
        uint64_t       packedSize  = 0;
        uint64_t       rawSize     = 0;
        VpkCompression compression = VpkCompression::eNone;
    };

    struct VpkReadOnly
    {
        VpkHeader                                           header {};
        std::vector<VpkEntry>                               entries;
        std::string                                         stringTable;
        std::unordered_map<uint64_t, std::vector<uint32_t>> buckets; // hash -> entry indices
    };

    // Open and parse a VPK file.
    vbase::Result<VpkReadOnly, AssetError> openVpk(vbase::StringView vpkPath);

    // Read an entry payload by logical path.
    vbase::Result<std::vector<std::byte>, AssetError>
    readVpkFile(const VpkReadOnly& vpk, vbase::StringView vpkPath, vbase::StringView logicalPath);

    // Writer input: already-prepared cooked bytes for each logical path.
    struct VpkWriteItem
    {
        std::string            logicalPath;
        std::vector<std::byte> bytes;
        bool                   allowCompress = true;
    };

    // Write a VPK to disk (per-entry zstd).
    vbase::Result<void, AssetError>
    writeVpk(vbase::StringView outPath, const std::vector<VpkWriteItem>& items, int zstdLevel);

    // A filesystem view over a VPK file.
    class VpkFileSystem final : public vfilesystem::IFileSystem
    {
    public:
        explicit VpkFileSystem(std::string vpkPath);

        vbase::Result<void, AssetError> openPackage();

        bool exists(vbase::StringView p) const override;
        bool isFile(vbase::StringView p) const override;
        bool isDirectory(vbase::StringView p) const override;

        vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>
        open(vbase::StringView p, vfilesystem::FileMode mode) override;

    private:
        std::string m_Path;
        VpkReadOnly m_Pkg;
        bool        m_Ready {false};
    };

} // namespace vasset
