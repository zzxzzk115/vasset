#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vasset_importers.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>

#include <cstddef>
#include <string>
#include <utility>
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
        std::vector<std::string> rootPaths;
        // Physical directories packed verbatim under a logical prefix (first = dir, second =
        // prefix). For content that lives outside the asset root, e.g. managed plugins under
        // <project>/.vultra/plugins/<id>/<version>. Listing a dir is the opt-in: no
        // reachability or runtime-raw-extension gating applies to its files.
        std::vector<std::pair<std::string, std::string>> extraDirs;
    };

    vbase::Result<void, AssetError> importAssetFolder(vbase::StringView                  assetRoot,
                                                      const AssetFolderImportOptions& options = {});

    vbase::Result<size_t, AssetError> packAssetFolderToVpk(vbase::StringView assetRoot,
                                                           vbase::StringView outVpk,
                                                           const VpkPackOptions& options = {});
} // namespace vasset
