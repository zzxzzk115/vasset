#include "vasset/editor_filesystem.hpp"
#include "vasset/vimport.hpp"

namespace vasset
{
    EditorRemapFileSystem::EditorRemapFileSystem(std::shared_ptr<vfilesystem::IFileSystem> base) :
        m_Base(std::move(base))
    {}

    bool EditorRemapFileSystem::exists(vbase::StringView p) const
    {
        // Remap-aware exists: if .vimport exists, check imported output; otherwise fall back to base.
        std::string importPath = std::string(p) + ".vimport";
        if (m_Base->exists(importPath))
        {
            auto vi = loadVImport(importPath);
            if (!vi)
                return false;
            return m_Base->exists(vi.value().output);
        }
        return m_Base->exists(p);
    }

    bool EditorRemapFileSystem::isFile(vbase::StringView p) const
    {
        std::string importPath = std::string(p) + ".vimport";
        if (m_Base->exists(importPath))
        {
            auto vi = loadVImport(importPath);
            if (!vi)
                return false;
            return m_Base->isFile(vi.value().output);
        }
        return m_Base->isFile(p);
    }

    bool EditorRemapFileSystem::isDirectory(vbase::StringView p) const
    {
        // Directories are not remapped.
        return m_Base->isDirectory(p);
    }

    vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>
    EditorRemapFileSystem::open(vbase::StringView p, vfilesystem::FileMode mode)
    {
        std::string importPath = std::string(p) + ".vimport";
        if (m_Base->exists(importPath))
        {
            auto vi = loadVImport(importPath);
            if (!vi)
                return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::err(
                    vfilesystem::FsError::eInvalidPath);

            const auto& outPath = vi.value().output;
            if (!m_Base->exists(outPath))
                return vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>::err(
                    vfilesystem::FsError::eNotFound);

            return m_Base->open(outPath, mode);
        }

        return m_Base->open(p, mode);
    }

} // namespace vasset
