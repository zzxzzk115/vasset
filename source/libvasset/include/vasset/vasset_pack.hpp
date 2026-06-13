#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vasset_importers.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vasset
{
    struct AssetFolderImportOptions
    {
        bool                          reimport {false};
        VAssetImporter::ImportOptions importOptions;
    };

    // A physical directory packed verbatim under a logical prefix. For content that lives outside
    // the asset root, e.g. managed plugins under <project>/.vultra/plugins/<id>/<version>. Listing
    // a dir is the opt-in: no reachability or runtime-raw-extension gating applies to its files.
    // `excludeGlobs` drops matching files (matched against the dir-relative path); used to keep
    // editor-only plugin code out of exported VPKs.
    struct VpkExtraDir
    {
        std::string              dir;
        std::string              logicalPrefix;
        std::vector<std::string> excludeGlobs;
    };

    struct VpkPackOptions
    {
        int                      zstdLevel {6};
        std::vector<std::string> includePaths;
        std::vector<std::string> rootPaths;
        std::vector<VpkExtraDir> extraDirs;
    };

    // Match a relative path (forward slashes) against a glob. `*` matches any run of non-`/`
    // characters, `?` matches one non-`/` character, `**` matches any characters including `/`.
    // A glob with no wildcard and no `/` (e.g. "editor") also matches anything beneath that dir.
    [[nodiscard]] bool matchPathGlob(std::string_view relPath, std::string_view glob);

    vbase::Result<void, AssetError> importAssetFolder(vbase::StringView                  assetRoot,
                                                      const AssetFolderImportOptions& options = {});

    vbase::Result<size_t, AssetError> packAssetFolderToVpk(vbase::StringView assetRoot,
                                                           vbase::StringView outVpk,
                                                           const VpkPackOptions& options = {});
} // namespace vasset
