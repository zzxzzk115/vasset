#pragma once

#include <vfilesystem/interfaces/ifilesystem.hpp>

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>

#include <memory>

namespace vasset
{
    // Editor-only file system wrapper.
    //
    // Behaviour:
    // - When opening a path, if "<path>.vimport" exists, it will be parsed and the request will be remapped to the
    //   imported output path recorded in the .vimport file.
    // - If the .vimport exists but the output is missing, open() returns FsError::eNotFound (caller may trigger
    // import).
    //
    // This keeps vfilesystem free of any asset/import knowledge.
    class EditorRemapFileSystem final : public vfilesystem::IFileSystem
    {
    public:
        explicit EditorRemapFileSystem(std::shared_ptr<vfilesystem::IFileSystem> base);

        bool exists(vbase::StringView p) const override;
        bool isFile(vbase::StringView p) const override;
        bool isDirectory(vbase::StringView p) const override;

        vbase::Result<std::unique_ptr<vfilesystem::IFile>, vfilesystem::FsError>
        open(vbase::StringView p, vfilesystem::FileMode mode) override;

    private:
        std::shared_ptr<vfilesystem::IFileSystem> m_Base;
    };

} // namespace vasset
