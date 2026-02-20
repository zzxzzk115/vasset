#include "vasset/editor_filesystem.hpp"
#include "vasset/vimport.hpp"

#include <filesystem>

namespace vasset
{
    EditorRemapFileSystem::EditorRemapFileSystem(std::shared_ptr<vfilesystem::PhysicalFileSystem> physicalFS) :
        m_PhysicalFS(std::move(physicalFS))
    {}

    bool EditorRemapFileSystem::exists(vbase::StringView p) const
    {
        // Remap-aware exists: if .vimport exists, check imported output; otherwise fall back to base.
        std::string importPath = std::filesystem ::path(p).replace_extension(".vimport").string();
        if (m_PhysicalFS->exists(importPath))
        {
            auto vi = loadVImport(m_PhysicalFS->getFullPath(importPath));
            if (!vi)
                return false;
            return m_PhysicalFS->exists(vi.value().output);
        }
        return m_PhysicalFS->exists(p);
    }

    bool EditorRemapFileSystem::isFile(vbase::StringView p) const
    {
        std::string importPath = std::filesystem ::path(p).replace_extension(".vimport").string();
        if (m_PhysicalFS->exists(importPath))
        {
            auto vi = loadVImport(m_PhysicalFS->getFullPath(importPath));
            if (!vi)
                return false;
            return m_PhysicalFS->isFile(vi.value().output);
        }
        return m_PhysicalFS->isFile(p);
    }

    bool EditorRemapFileSystem::isDirectory(vbase::StringView p) const
    {
        // Directories are not remapped.
        return m_PhysicalFS->isDirectory(p);
    }

    vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>
    EditorRemapFileSystem::open(vbase::StringView p, vfilesystem::FileMode mode)
    {
        std::string importPath = std::filesystem ::path(p).replace_extension(".vimport").string();
        if (m_PhysicalFS->exists(importPath))
        {
            auto vi = loadVImport(m_PhysicalFS->getFullPath(importPath));
            if (!vi)
                return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::err(
                    vfilesystem::FsError::eInvalidPath);

            const auto& outPath = vi.value().output;
            if (!m_PhysicalFS->exists(outPath))
                return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::err(
                    vfilesystem::FsError::eNotFound);

            return m_PhysicalFS->open(outPath, mode);
        }

        return m_PhysicalFS->open(p, mode);
    }

} // namespace vasset
