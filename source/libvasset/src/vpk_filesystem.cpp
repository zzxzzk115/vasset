#include "vasset/vpk.hpp"

#include <cstring>
#include <vector>

namespace vasset
{
    namespace
    {
        class VpkMemoryFile final : public vfilesystem::IFile
        {
        public:
            explicit VpkMemoryFile(std::vector<std::byte> data) : m_Data(std::move(data)) {}

            uint64_t size() const override { return static_cast<uint64_t>(m_Data.size()); }

            uint64_t tell() const override { return m_Pos; }

            bool seek(uint64_t pos) override
            {
                if (pos > m_Data.size())
                    return false;
                m_Pos = pos;
                return true;
            }

            size_t read(void* dst, size_t bytes) override
            {
                const size_t avail = (m_Pos < m_Data.size()) ? (m_Data.size() - static_cast<size_t>(m_Pos)) : 0;
                const size_t n     = (bytes < avail) ? bytes : avail;
                if (n)
                    std::memcpy(dst, m_Data.data() + static_cast<size_t>(m_Pos), n);
                m_Pos += static_cast<uint64_t>(n);
                return n;
            }

            size_t write(const void*, size_t) override { return 0; } // read-only

            std::vector<std::byte> readAllBytes() override
            {
                if (m_Pos >= m_Data.size())
                    return {};
                std::vector<std::byte> out;
                out.resize(m_Data.size() - static_cast<size_t>(m_Pos));
                std::memcpy(out.data(), m_Data.data() + static_cast<size_t>(m_Pos), out.size());
                m_Pos += static_cast<uint64_t>(out.size());
                return out;
            }

        private:
            std::vector<std::byte> m_Data;
            uint64_t               m_Pos {0};
        };
    } // namespace

    VpkFileSystem::VpkFileSystem(std::string vpkPath) : m_Path(std::move(vpkPath)) {}

    vbase::Result<void, AssetError> VpkFileSystem::openPackage()
    {
        auto r = openVpk(m_Path);
        if (!r)
            return vbase::Result<void, AssetError>::err(r.error());
        m_Pkg   = std::move(r.value());
        m_Ready = true;
        return vbase::Result<void, AssetError>::ok();
    }

    bool VpkFileSystem::exists(vbase::StringView p) const
    {
        if (!m_Ready)
            return false;
        auto r = readVpkFile(m_Pkg, m_Path, p);
        return static_cast<bool>(r);
    }

    bool VpkFileSystem::isFile(vbase::StringView p) const { return exists(p); }

    bool VpkFileSystem::isDirectory(vbase::StringView) const { return false; }

    vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>
    VpkFileSystem::open(vbase::StringView p, vfilesystem::FileMode mode)
    {
        if (mode != vfilesystem::FileMode::eRead)
            return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::err(
                vfilesystem::FsError::eNotSupported);

        if (!m_Ready)
            return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::err(
                vfilesystem::FsError::eIOError);

        auto r = readVpkFile(m_Pkg, m_Path, p);
        if (!r)
        {
            if (r.error() == AssetError::eNotFound)
                return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::err(
                    vfilesystem::FsError::eNotFound);
            return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::err(
                vfilesystem::FsError::eIOError);
        }

        return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::ok(
            std::make_unique<VpkMemoryFile>(std::move(r.value())));
    }

} // namespace vasset
