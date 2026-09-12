#pragma once

#include <filesystem>

namespace vasset::detail
{
    inline std::filesystem::path filePath(std::filesystem::path path)
    {
#ifdef _WIN32
        // Consumers need not opt into long paths. Normalize relative components before
        // entering the extended namespace; 248 also covers directory creation's limit.
        if (path.native().starts_with(L"\\\\?\\") || path.native().starts_with(L"\\\\.\\"))
            return path;
        auto absolute = std::filesystem::absolute(path).lexically_normal().make_preferred();
        if (absolute.native().size() >= 248)
        {
            if (absolute.native().starts_with(L"\\\\"))
                return std::filesystem::path(L"\\\\?\\UNC\\" + absolute.native().substr(2));
            return std::filesystem::path(L"\\\\?\\" + absolute.native());
        }
#endif
        return path;
    }
} // namespace vasset::detail
