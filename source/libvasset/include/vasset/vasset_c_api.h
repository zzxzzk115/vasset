// vasset C ABI — a flat, stable C interface over the C++ asset library, for consumers that bind
// across an FFI boundary (engines/editors loading vasset by name, e.g. VultraEngine resolving these
// exports from the main program handle). Mirrors the "VRI-style" complete C ABI: runtime asset
// loading (VPK + UUID resolver), the editor asset registry (CRUD + dependency graph), and
// import/cook/pack. The CLI entry point stays in tool_c_api.h.
//
// Conventions:
//   - All strings are UTF-8, null-terminated.
//   - UUIDs cross the boundary as 32-char lowercase hex strings (vbase::to_string form).
//   - Functions returning int32_t use 0 for success and a negative code (-VAssetStatus) on failure;
//     vasset_last_error() then returns a human-readable message for the calling thread.
//   - Getter functions returning `const char*` return a thread-local buffer valid only until the
//     next call to the SAME function on the same thread; copy it out before calling again.
//   - Handle-returning constructors return NULL on failure (with vasset_last_error set).
//   - Object handles are not thread-safe; do not share one handle across threads concurrently.
#ifndef VASSET_C_API_H
#define VASSET_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // ABI revision of this header/runtime. Bump when the surface changes incompatibly.
    uint32_t vasset_c_api_version(void);

    // Last error message for the calling thread (empty string if none). Valid until the next failing
    // call on this thread.
    const char* vasset_last_error(void);

    // Status codes mirror vasset::AssetError. Failing int32_t functions return the negation.
    typedef enum VAssetStatus
    {
        VASSET_OK                  = 0,
        VASSET_ERR_NOT_FOUND       = 1,
        VASSET_ERR_INVALID_FORMAT  = 2,
        VASSET_ERR_INVALID_IMPORT  = 3,
        VASSET_ERR_UNKNOWN_IMPORTER= 4,
        VASSET_ERR_IMPORT_FAILED   = 5,
        VASSET_ERR_IO              = 6,
        VASSET_ERR_NOT_SUPPORTED   = 7,
        VASSET_ERR_OUT_OF_MEMORY   = 8,
        VASSET_ERR_INVALID_ARG     = 100, // C-ABI layer: null/!valid handle or argument
    } VAssetStatus;

    // VAssetType values match vasset::VAssetType (0 == unknown). Use the helpers to map names.
    const char* vasset_type_to_string(int32_t type);
    int32_t     vasset_type_from_string(const char* name); // returns VAssetType (0 unknown)

    // An owned byte buffer returned by read functions. Free every successfully-filled blob with
    // vasset_blob_free exactly once.
    typedef struct VAssetBlob
    {
        uint8_t* data;
        size_t   size;
    } VAssetBlob;

    void vasset_blob_free(VAssetBlob* blob);

    // ---- VPK (runtime asset package) --------------------------------------------------------------
    typedef struct VAssetVpk_t* VAssetVpkHandle;

    VAssetVpkHandle vasset_vpk_open(const char* vpkPath);
    VAssetVpkHandle vasset_vpk_open_memory(const uint8_t* blob, size_t size); // blob copied + owned
    void            vasset_vpk_close(VAssetVpkHandle vpk);

    // Read one cooked payload by logical path (e.g. "res://sprites/a.png"). On success fills outBlob
    // (free with vasset_blob_free) and returns VASSET_OK; negative on failure.
    int32_t vasset_vpk_read(VAssetVpkHandle vpk, const char* logicalPath, VAssetBlob* outBlob);

    uint32_t vasset_vpk_file_count(VAssetVpkHandle vpk);

    // Registry (uuid <-> logical path + type) embedded in the VPK.
    uint32_t    vasset_vpk_asset_count(VAssetVpkHandle vpk);
    const char* vasset_vpk_asset_uuid_at(VAssetVpkHandle vpk, uint32_t index); // 32-char hex, "" oob
    const char* vasset_vpk_asset_path_at(VAssetVpkHandle vpk, uint32_t index); // logical path, "" oob
    int32_t     vasset_vpk_asset_type_at(VAssetVpkHandle vpk, uint32_t index); // VAssetType, 0 oob

    // ---- UUID resolver (uuid <-> uri, with an optional scheme like "res") -------------------------
    typedef struct VAssetResolver_t* VAssetResolverHandle;
    typedef struct VAssetRegistry_t* VAssetRegistryHandle;

    VAssetResolverHandle vasset_resolver_create(void);
    void                 vasset_resolver_destroy(VAssetResolverHandle resolver);

    void vasset_resolver_set_scheme(VAssetResolverHandle resolver, const char* scheme); // e.g. "res"
    void vasset_resolver_clear(VAssetResolverHandle resolver);
    void vasset_resolver_register(VAssetResolverHandle resolver, const char* uuid, const char* logicalPath);

    int32_t vasset_resolver_load_from_registry(VAssetResolverHandle resolver, VAssetRegistryHandle registry);
    int32_t vasset_resolver_load_from_vpk(VAssetResolverHandle resolver, VAssetVpkHandle vpk);

    // resolve: uuid -> "scheme://path" (or bare path if no scheme). reverse: uri -> uuid hex.
    // Return NULL when the key is unknown.
    const char* vasset_resolver_resolve(VAssetResolverHandle resolver, const char* uuid);
    const char* vasset_resolver_reverse(VAssetResolverHandle resolver, const char* uri);

    // ---- Asset registry (editor-side uuid -> source/imported path + type, + dependency graph) -----
    VAssetRegistryHandle vasset_registry_create(void);
    void                 vasset_registry_destroy(VAssetRegistryHandle registry);

    void        vasset_registry_set_root(VAssetRegistryHandle registry, const char* rootPath);
    void        vasset_registry_set_imported_folder(VAssetRegistryHandle registry, const char* name);
    const char* vasset_registry_get_root(VAssetRegistryHandle registry);
    const char* vasset_registry_get_imported_folder(VAssetRegistryHandle registry);

    int32_t vasset_registry_load(VAssetRegistryHandle registry, const char* filename);
    int32_t vasset_registry_save(VAssetRegistryHandle registry, const char* filename);

    int32_t vasset_registry_register(VAssetRegistryHandle registry,
                                     const char*          uuid,
                                     const char*          sourcePath,
                                     const char*          importedPath,
                                     int32_t              type);
    int32_t vasset_registry_update(VAssetRegistryHandle registry, const char* uuid, const char* newImportedPath);
    int32_t vasset_registry_unregister(VAssetRegistryHandle registry, const char* uuid);
    void    vasset_registry_cleanup(VAssetRegistryHandle registry);

    // Lookup by uuid. Each getter returns its own thread-local buffer (see header conventions).
    int32_t     vasset_registry_contains(VAssetRegistryHandle registry, const char* uuid); // 1/0
    const char* vasset_registry_source_path(VAssetRegistryHandle registry, const char* uuid);
    const char* vasset_registry_imported_path(VAssetRegistryHandle registry, const char* uuid);
    int32_t     vasset_registry_type(VAssetRegistryHandle registry, const char* uuid); // VAssetType

    // Snapshot enumeration: count() freezes the current key order for the *_at getters that follow.
    uint32_t    vasset_registry_count(VAssetRegistryHandle registry);
    const char* vasset_registry_uuid_at(VAssetRegistryHandle registry, uint32_t index);

    // Path mapping helpers (relative or absolute).
    const char* vasset_registry_source_asset_path(VAssetRegistryHandle registry, const char* assetFullPath, int32_t relative);
    const char* vasset_registry_imported_asset_path(VAssetRegistryHandle registry, int32_t type, const char* assetName, int32_t relative);

    // Dependency graph. Kind values match vasset::VAssetDependencyKind.
    void vasset_registry_add_dependency(VAssetRegistryHandle registry,
                                        const char*          ownerUuid,
                                        int32_t              kind,
                                        const char*          targetUuid,
                                        const char*          targetPath,
                                        const char*          context,
                                        int32_t              required);
    void vasset_registry_clear_dependencies(VAssetRegistryHandle registry, const char* ownerUuid);

    // Outgoing edges of ownerUuid. count() snapshots; the *_at getters read that snapshot.
    uint32_t    vasset_registry_dependency_count(VAssetRegistryHandle registry, const char* ownerUuid);
    int32_t     vasset_registry_dependency_kind_at(VAssetRegistryHandle registry, uint32_t index);
    const char* vasset_registry_dependency_target_uuid_at(VAssetRegistryHandle registry, uint32_t index);
    const char* vasset_registry_dependency_target_path_at(VAssetRegistryHandle registry, uint32_t index);
    const char* vasset_registry_dependency_context_at(VAssetRegistryHandle registry, uint32_t index);
    int32_t     vasset_registry_dependency_required_at(VAssetRegistryHandle registry, uint32_t index);

    // Incoming edges (who depends on targetUuid). count() snapshots; *_at read that snapshot.
    uint32_t    vasset_registry_dependent_count(VAssetRegistryHandle registry, const char* targetUuid);
    const char* vasset_registry_dependent_owner_at(VAssetRegistryHandle registry, uint32_t index);
    int32_t     vasset_registry_dependent_kind_at(VAssetRegistryHandle registry, uint32_t index);

    // Validate the dependency graph. Returns 1 when ok (no issues), 0 otherwise; *outIssueCount (may
    // be NULL) receives the issue count, snapshotted for the issue getters below.
    int32_t     vasset_registry_validate(VAssetRegistryHandle registry, uint32_t* outIssueCount);
    int32_t     vasset_registry_issue_kind_at(VAssetRegistryHandle registry, uint32_t index); // 0 missing,1 cycle
    const char* vasset_registry_issue_owner_at(VAssetRegistryHandle registry, uint32_t index);

    // ---- Import / cook / pack (only available when linked against vasset-import) ------------------
    // Import every source asset under assetRoot, writing .vmeta + imported/ + asset_registry.json.
    int32_t vasset_import_folder(const char* assetRoot, int32_t reimport);

    // Pack options. Pass NULL to vasset_pack_* for defaults (zstd level 6, no filters).
    typedef struct VAssetPackOptions_t* VAssetPackOptionsHandle;

    VAssetPackOptionsHandle vasset_pack_options_create(void);
    void                    vasset_pack_options_destroy(VAssetPackOptionsHandle options);
    void                    vasset_pack_options_set_zstd_level(VAssetPackOptionsHandle options, int32_t level);
    void                    vasset_pack_options_add_include_path(VAssetPackOptionsHandle options, const char* path);
    void                    vasset_pack_options_add_root_path(VAssetPackOptionsHandle options, const char* path);
    // Pack a physical dir verbatim under logicalPrefix; subsequent add_extra_exclude calls attach to
    // the most recently added extra dir.
    void vasset_pack_options_add_extra_dir(VAssetPackOptionsHandle options, const char* dir, const char* logicalPrefix);
    void vasset_pack_options_add_extra_exclude(VAssetPackOptionsHandle options, const char* glob);

    // Pack an already-imported assetRoot into outVpk. *outBytes (may be NULL) receives the file size.
    int32_t vasset_pack_folder_to_vpk(const char* assetRoot, const char* outVpk, VAssetPackOptionsHandle options, size_t* outBytes);

    // Cook = import then pack, in one call (mirrors the CLI `cook` verb).
    int32_t vasset_cook_folder_to_vpk(const char* assetRoot, const char* outVpk, int32_t reimport, VAssetPackOptionsHandle options, size_t* outBytes);

#ifdef __cplusplus
}
#endif

#endif // VASSET_C_API_H
