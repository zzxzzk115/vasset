#pragma once

#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace vasset
{
    // Editor-only import descriptor (INI-like file: <source>.vimport).
    // Runtime should NOT depend on this file.
    //
    // Design note:
    // - Derived/cache fields (source hash, params hash, timestamps...) are stored in the editor import database,
    //   not in the .vimport file, to keep the file stable and git-friendly.
    struct VImport
    {
        uint32_t    version {1};
        std::string importer; // e.g. "texture", "mesh"
        vbase::UUID uid;      // stable id for this asset (editor)

        std::string source; // models/xxx/yyy.gltf
        std::string output; // imported/mesh/yyy

        // Flat params. Keep keys stable for merge friendliness.
        std::unordered_map<std::string, std::string> params;
    };

    // Read/write .vimport file. The format is a strict INI-like schema:
    // [vimport]
    // version=1
    // importer="..."
    // uid="..."
    //
    // [source]
    // file="..."
    //
    // [output]
    // file="..."
    //
    // [params]
    // key=value (flat)
    vbase::Result<VImport, AssetError> loadVImport(vbase::StringView filePath);
    vbase::Result<void, AssetError>    saveVImport(const VImport& import, vbase::StringView filePath);
} // namespace vasset
