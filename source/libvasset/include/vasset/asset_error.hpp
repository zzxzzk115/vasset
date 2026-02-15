#pragma once

#include <cstdint>

namespace vasset
{
    // vasset-wide error codes for editor/import-time operations.
    enum class AssetError : uint8_t
    {
        eOk = 0,

        eNotFound,
        eInvalidFormat,
        eInvalidImportFile,
        eUnknownImporter,
        eImportFailed,

        eIOError,
        eNotSupported,
        eOutOfMemory,
    };

} // namespace vasset
