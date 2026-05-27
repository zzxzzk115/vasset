#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vasset_importers.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace vasset
{
    struct AssetFolderImportOptions
    {
        bool                          reimport {false};
        VAssetImporter::ImportOptions importOptions;
    };

    struct VpkPackOptions
    {
        int                      zstdLevel {6};
        std::vector<std::string> includePaths;
    };

    vbase::Result<void, AssetError> importAssetFolder(vbase::StringView                  assetRoot,
                                                      const AssetFolderImportOptions& options = {});

    vbase::Result<size_t, AssetError> packAssetFolderToVpk(vbase::StringView assetRoot,
                                                           vbase::StringView outVpk,
                                                           const VpkPackOptions& options = {});
} // namespace vasset
