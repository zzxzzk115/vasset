// Import half of the vasset C ABI (vasset_c_api.h): folder import, pack-to-VPK, and cook
// (import+pack). Lives in the `vasset-import` target (gated by VASSET_HAS_IMPORTERS) because it pulls
// in the heavy importer toolchain (assimp/ozz/vshadersystem). The shared last-error store and the
// runtime half live in the `vasset` target this target links against.
#include "vasset/vasset_c_api.h"

#include "vasset_c_api_internal.hpp"

#include "vasset/vasset_import.hpp"
#include "vasset/vasset_pack.hpp"

#include <string>

// Wrapper for the opaque VAssetPackOptionsHandle.
struct VAssetPackOptions_t
{
    vasset::VpkPackOptions options;
};

namespace
{
    using vasset::capi::fail;
    using vasset::capi::failAsset;

    const vasset::VpkPackOptions g_defaultPackOptions {};

    const vasset::VpkPackOptions& packOptionsOf(VAssetPackOptionsHandle options)
    {
        auto* h = reinterpret_cast<VAssetPackOptions_t*>(options);
        return h ? h->options : g_defaultPackOptions;
    }
} // namespace

extern "C"
{
    int32_t vasset_import_folder(const char* assetRoot, int32_t reimport)
    {
        if (!assetRoot)
            return fail(VASSET_ERR_INVALID_ARG, "null asset root");
        vasset::AssetFolderImportOptions opts;
        opts.reimport = reimport != 0;
        auto res      = vasset::importAssetFolder(assetRoot, opts);
        return res ? VASSET_OK : failAsset(res.error(), "importAssetFolder failed");
    }

    VAssetPackOptionsHandle vasset_pack_options_create(void)
    {
        return reinterpret_cast<VAssetPackOptionsHandle>(new VAssetPackOptions_t());
    }
    void vasset_pack_options_destroy(VAssetPackOptionsHandle options)
    {
        delete reinterpret_cast<VAssetPackOptions_t*>(options);
    }
    void vasset_pack_options_set_zstd_level(VAssetPackOptionsHandle options, int32_t level)
    {
        if (auto* h = reinterpret_cast<VAssetPackOptions_t*>(options))
            h->options.zstdLevel = level;
    }
    void vasset_pack_options_add_include_path(VAssetPackOptionsHandle options, const char* path)
    {
        if (auto* h = reinterpret_cast<VAssetPackOptions_t*>(options); h && path)
            h->options.includePaths.emplace_back(path);
    }
    void vasset_pack_options_add_root_path(VAssetPackOptionsHandle options, const char* path)
    {
        if (auto* h = reinterpret_cast<VAssetPackOptions_t*>(options); h && path)
            h->options.rootPaths.emplace_back(path);
    }
    void vasset_pack_options_add_extra_dir(VAssetPackOptionsHandle options, const char* dir, const char* logicalPrefix)
    {
        if (auto* h = reinterpret_cast<VAssetPackOptions_t*>(options); h && dir)
            h->options.extraDirs.push_back(vasset::VpkExtraDir {.dir            = dir,
                                                                .logicalPrefix  = logicalPrefix ? logicalPrefix : "",
                                                                .excludeGlobs   = {}});
    }
    void vasset_pack_options_add_extra_exclude(VAssetPackOptionsHandle options, const char* glob)
    {
        auto* h = reinterpret_cast<VAssetPackOptions_t*>(options);
        if (h && glob && !h->options.extraDirs.empty())
            h->options.extraDirs.back().excludeGlobs.emplace_back(glob);
    }

    int32_t vasset_pack_folder_to_vpk(const char* assetRoot, const char* outVpk, VAssetPackOptionsHandle options, size_t* outBytes)
    {
        if (!assetRoot || !outVpk)
            return fail(VASSET_ERR_INVALID_ARG, "null asset root or out path");
        auto res = vasset::packAssetFolderToVpk(assetRoot, outVpk, packOptionsOf(options));
        if (!res)
            return failAsset(res.error(), "packAssetFolderToVpk failed");
        if (outBytes)
            *outBytes = res.value();
        return VASSET_OK;
    }

    int32_t vasset_cook_folder_to_vpk(const char* assetRoot, const char* outVpk, int32_t reimport, VAssetPackOptionsHandle options, size_t* outBytes)
    {
        if (int32_t rc = vasset_import_folder(assetRoot, reimport); rc != VASSET_OK)
            return rc;
        return vasset_pack_folder_to_vpk(assetRoot, outVpk, options, outBytes);
    }
} // extern "C"
