// Runtime half of the vasset C ABI (vasset_c_api.h): VPK reading, the UUID resolver, and the editor
// asset registry (CRUD + dependency graph). Lives in the `vasset` target so it is available in every
// build, including runtime-only ones without the importers. The import/cook/pack half lives in
// vasset_c_api_import.cpp (vasset-import target).
#include "vasset/vasset_c_api.h"

#include "vasset_c_api_internal.hpp"

#include "vasset/uuid_resolver.hpp"
#include "vasset/vasset_registry.hpp"
#include "vasset/vasset_type.hpp"
#include "vasset/vpk.hpp"

#include <vbase/core/uuid.hpp>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Handle wrappers. The opaque typedefs in the header are `struct VAsset*_t*`; we define the structs.
struct VAssetVpk_t
{
    vasset::VpkReadOnly    vpk;
    bool                   memory {false};
    std::string            path;  // source path (disk-backed)
    std::vector<std::byte> blob;  // owned image (memory-backed)
};

struct VAssetResolver_t
{
    vasset::VUUIDResolver resolver;
};

struct VAssetRegistry_t
{
    vasset::VAssetRegistry                     registry;
    std::vector<std::string>                   uuidSnapshot;       // frozen by vasset_registry_count
    std::vector<vasset::VAssetDependency>      depSnapshot;        // frozen by *_dependency_count
    std::vector<vasset::VAssetDependent>       dependentSnapshot;  // frozen by *_dependent_count
    std::vector<vasset::VAssetDependencyIssue> issueSnapshot;      // frozen by *_validate
};

namespace vasset::capi
{
    // Single definition of the shared per-thread last-error store (declared in the internal header);
    // the import TU writes here too, so vasset_last_error() reports failures from both halves.
    thread_local std::string g_lastError;

    void setLastError(std::string msg) { g_lastError = std::move(msg); }

    int32_t fail(VAssetStatus code, const char* msg)
    {
        g_lastError = msg ? msg : "";
        return -static_cast<int32_t>(code);
    }

    // vasset::AssetError values map 1:1 onto VAssetStatus 0..8.
    int32_t failAsset(vasset::AssetError e, const char* ctx)
    {
        g_lastError = ctx ? ctx : "";
        return -static_cast<int32_t>(static_cast<uint8_t>(e));
    }
} // namespace vasset::capi

namespace
{
    using vasset::capi::fail;
    using vasset::capi::failAsset;

    // Each public getter passes its own thread_local buffer so concurrent getters don't clobber.
    const char* hold(std::string& buf, std::string value)
    {
        buf = std::move(value);
        return buf.c_str();
    }

    bool parseUuid(const char* hex, vbase::UUID& out)
    {
        return hex && vbase::try_parse_uuid(hex, out);
    }

    int32_t fillBlob(VAssetBlob* out, const std::vector<std::byte>& bytes)
    {
        if (!out)
            return fail(VASSET_ERR_INVALID_ARG, "null out blob");
        out->size = bytes.size();
        out->data = static_cast<uint8_t*>(std::malloc(bytes.empty() ? 1 : bytes.size()));
        if (!out->data)
        {
            out->size = 0;
            return fail(VASSET_ERR_OUT_OF_MEMORY, "blob alloc failed");
        }
        if (!bytes.empty())
            std::memcpy(out->data, bytes.data(), bytes.size());
        return VASSET_OK;
    }
} // namespace

extern "C"
{
    uint32_t    vasset_c_api_version(void) { return 1; }
    const char* vasset_last_error(void) { return vasset::capi::g_lastError.c_str(); }

    const char* vasset_type_to_string(int32_t type)
    {
        static thread_local std::string buf;
        return hold(buf, vasset::toString(static_cast<vasset::VAssetType>(type)));
    }
    int32_t vasset_type_from_string(const char* name)
    {
        return static_cast<int32_t>(vasset::fromString(name ? name : ""));
    }

    void vasset_blob_free(VAssetBlob* blob)
    {
        if (!blob)
            return;
        std::free(blob->data);
        blob->data = nullptr;
        blob->size = 0;
    }

    // ---- VPK -------------------------------------------------------------------------------------
    VAssetVpkHandle vasset_vpk_open(const char* vpkPath)
    {
        if (!vpkPath)
        {
            fail(VASSET_ERR_INVALID_ARG, "null vpk path");
            return nullptr;
        }
        auto opened = vasset::openVpk(vpkPath);
        if (!opened)
        {
            failAsset(opened.error(), "openVpk failed");
            return nullptr;
        }
        auto* h = new VAssetVpk_t();
        h->vpk    = std::move(opened).value();
        h->memory = false;
        h->path   = vpkPath;
        return reinterpret_cast<VAssetVpkHandle>(h);
    }

    VAssetVpkHandle vasset_vpk_open_memory(const uint8_t* blob, size_t size)
    {
        if (!blob && size != 0)
        {
            fail(VASSET_ERR_INVALID_ARG, "null vpk blob");
            return nullptr;
        }
        std::vector<std::byte> owned(size);
        if (size)
            std::memcpy(owned.data(), blob, size);
        auto opened = vasset::openVpkFromMemory(vbase::ConstByteSpan {owned.data(), owned.size()});
        if (!opened)
        {
            failAsset(opened.error(), "openVpkFromMemory failed");
            return nullptr;
        }
        auto* h = new VAssetVpk_t();
        h->vpk    = std::move(opened).value();
        h->memory = true;
        h->blob   = std::move(owned);
        return reinterpret_cast<VAssetVpkHandle>(h);
    }

    void vasset_vpk_close(VAssetVpkHandle vpk) { delete reinterpret_cast<VAssetVpk_t*>(vpk); }

    int32_t vasset_vpk_read(VAssetVpkHandle vpk, const char* logicalPath, VAssetBlob* outBlob)
    {
        auto* h = reinterpret_cast<VAssetVpk_t*>(vpk);
        if (!h || !logicalPath)
            return fail(VASSET_ERR_INVALID_ARG, "null vpk handle or path");

        auto read = h->memory ? vasset::readVpkFileFromMemory(
                                    h->vpk, vbase::ConstByteSpan {h->blob.data(), h->blob.size()}, logicalPath)
                              : vasset::readVpkFile(h->vpk, h->path, logicalPath);
        if (!read)
            return failAsset(read.error(), "readVpkFile failed");
        return fillBlob(outBlob, read.value());
    }

    uint32_t vasset_vpk_file_count(VAssetVpkHandle vpk)
    {
        auto* h = reinterpret_cast<VAssetVpk_t*>(vpk);
        return h ? static_cast<uint32_t>(h->vpk.entries.size()) : 0;
    }

    uint32_t vasset_vpk_asset_count(VAssetVpkHandle vpk)
    {
        auto* h = reinterpret_cast<VAssetVpk_t*>(vpk);
        return h ? static_cast<uint32_t>(h->vpk.registry.size()) : 0;
    }

    const char* vasset_vpk_asset_uuid_at(VAssetVpkHandle vpk, uint32_t index)
    {
        static thread_local std::string buf;
        auto* h = reinterpret_cast<VAssetVpk_t*>(vpk);
        if (!h || index >= h->vpk.registry.size())
            return hold(buf, "");
        return hold(buf, vbase::to_string(h->vpk.registry[index].uuid));
    }

    const char* vasset_vpk_asset_path_at(VAssetVpkHandle vpk, uint32_t index)
    {
        static thread_local std::string buf;
        auto* h = reinterpret_cast<VAssetVpk_t*>(vpk);
        if (!h || index >= h->vpk.registry.size())
            return hold(buf, "");
        const auto& e = h->vpk.registry[index];
        if (static_cast<size_t>(e.pathOffset) + e.pathSize > h->vpk.stringTable.size())
            return hold(buf, "");
        return hold(buf, std::string(h->vpk.stringTable.data() + e.pathOffset, e.pathSize));
    }

    int32_t vasset_vpk_asset_type_at(VAssetVpkHandle vpk, uint32_t index)
    {
        auto* h = reinterpret_cast<VAssetVpk_t*>(vpk);
        if (!h || index >= h->vpk.registry.size())
            return 0;
        return static_cast<int32_t>(h->vpk.registry[index].type);
    }

    // ---- Resolver --------------------------------------------------------------------------------
    VAssetResolverHandle vasset_resolver_create(void) { return reinterpret_cast<VAssetResolverHandle>(new VAssetResolver_t()); }
    void vasset_resolver_destroy(VAssetResolverHandle resolver) { delete reinterpret_cast<VAssetResolver_t*>(resolver); }

    void vasset_resolver_set_scheme(VAssetResolverHandle resolver, const char* scheme)
    {
        if (auto* h = reinterpret_cast<VAssetResolver_t*>(resolver))
            h->resolver.setScheme(scheme ? scheme : "");
    }
    void vasset_resolver_clear(VAssetResolverHandle resolver)
    {
        if (auto* h = reinterpret_cast<VAssetResolver_t*>(resolver))
            h->resolver.clear();
    }
    void vasset_resolver_register(VAssetResolverHandle resolver, const char* uuid, const char* logicalPath)
    {
        auto* h = reinterpret_cast<VAssetResolver_t*>(resolver);
        vbase::UUID id {};
        if (h && parseUuid(uuid, id) && logicalPath)
            h->resolver.registerAsset(id, logicalPath);
    }

    int32_t vasset_resolver_load_from_registry(VAssetResolverHandle resolver, VAssetRegistryHandle registry)
    {
        auto* h = reinterpret_cast<VAssetResolver_t*>(resolver);
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!h || !r)
            return fail(VASSET_ERR_INVALID_ARG, "null handle");
        h->resolver.loadFromAssetRegistry(r->registry);
        return VASSET_OK;
    }
    int32_t vasset_resolver_load_from_vpk(VAssetResolverHandle resolver, VAssetVpkHandle vpk)
    {
        auto* h = reinterpret_cast<VAssetResolver_t*>(resolver);
        auto* v = reinterpret_cast<VAssetVpk_t*>(vpk);
        if (!h || !v)
            return fail(VASSET_ERR_INVALID_ARG, "null handle");
        h->resolver.loadFromVPK(v->vpk);
        return VASSET_OK;
    }

    const char* vasset_resolver_resolve(VAssetResolverHandle resolver, const char* uuid)
    {
        static thread_local std::string buf;
        auto*       h = reinterpret_cast<VAssetResolver_t*>(resolver);
        vbase::UUID id {};
        std::string uri;
        if (h && parseUuid(uuid, id) && h->resolver.resolve(id, uri))
            return hold(buf, std::move(uri));
        return nullptr;
    }
    const char* vasset_resolver_reverse(VAssetResolverHandle resolver, const char* uri)
    {
        static thread_local std::string buf;
        auto*       h = reinterpret_cast<VAssetResolver_t*>(resolver);
        vbase::UUID id {};
        if (h && uri && h->resolver.reverseResolve(uri, id))
            return hold(buf, vbase::to_string(id));
        return nullptr;
    }

    // ---- Registry --------------------------------------------------------------------------------
    VAssetRegistryHandle vasset_registry_create(void) { return reinterpret_cast<VAssetRegistryHandle>(new VAssetRegistry_t()); }
    void vasset_registry_destroy(VAssetRegistryHandle registry) { delete reinterpret_cast<VAssetRegistry_t*>(registry); }

    void vasset_registry_set_root(VAssetRegistryHandle registry, const char* rootPath)
    {
        if (auto* r = reinterpret_cast<VAssetRegistry_t*>(registry))
            r->registry.setAssetRootPath(rootPath ? rootPath : "");
    }
    void vasset_registry_set_imported_folder(VAssetRegistryHandle registry, const char* name)
    {
        if (auto* r = reinterpret_cast<VAssetRegistry_t*>(registry))
            r->registry.setImportedFolderName(name ? name : "");
    }
    const char* vasset_registry_get_root(VAssetRegistryHandle registry)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        return r ? hold(buf, r->registry.getAssetRootPath()) : hold(buf, "");
    }
    const char* vasset_registry_get_imported_folder(VAssetRegistryHandle registry)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        return r ? hold(buf, r->registry.getImportedFolderName()) : hold(buf, "");
    }

    int32_t vasset_registry_load(VAssetRegistryHandle registry, const char* filename)
    {
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || !filename)
            return fail(VASSET_ERR_INVALID_ARG, "null handle or filename");
        auto res = r->registry.load(filename);
        return res ? VASSET_OK : failAsset(res.error(), "registry load failed");
    }
    int32_t vasset_registry_save(VAssetRegistryHandle registry, const char* filename)
    {
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || !filename)
            return fail(VASSET_ERR_INVALID_ARG, "null handle or filename");
        auto res = r->registry.save(filename);
        return res ? VASSET_OK : failAsset(res.error(), "registry save failed");
    }

    int32_t vasset_registry_register(VAssetRegistryHandle registry,
                                     const char*          uuid,
                                     const char*          sourcePath,
                                     const char*          importedPath,
                                     int32_t              type)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID id {};
        if (!r || !parseUuid(uuid, id))
            return fail(VASSET_ERR_INVALID_ARG, "null handle or bad uuid");
        auto res = r->registry.registerAsset(id,
                                             sourcePath ? sourcePath : "",
                                             importedPath ? importedPath : "",
                                             static_cast<vasset::VAssetType>(type));
        return res ? VASSET_OK : failAsset(res.error(), "registerAsset failed");
    }
    int32_t vasset_registry_update(VAssetRegistryHandle registry, const char* uuid, const char* newImportedPath)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID id {};
        if (!r || !parseUuid(uuid, id))
            return fail(VASSET_ERR_INVALID_ARG, "null handle or bad uuid");
        auto res = r->registry.updateRegistry(id, newImportedPath ? newImportedPath : "");
        return res ? VASSET_OK : failAsset(res.error(), "updateRegistry failed");
    }
    int32_t vasset_registry_unregister(VAssetRegistryHandle registry, const char* uuid)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID id {};
        if (!r || !parseUuid(uuid, id))
            return fail(VASSET_ERR_INVALID_ARG, "null handle or bad uuid");
        auto res = r->registry.unregisterAsset(id);
        return res ? VASSET_OK : failAsset(res.error(), "unregisterAsset failed");
    }
    void vasset_registry_cleanup(VAssetRegistryHandle registry)
    {
        if (auto* r = reinterpret_cast<VAssetRegistry_t*>(registry))
            r->registry.cleanup();
    }

    int32_t vasset_registry_contains(VAssetRegistryHandle registry, const char* uuid)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID id {};
        if (!r || !parseUuid(uuid, id))
            return 0;
        return r->registry.getRegistry().count(vbase::to_string(id)) ? 1 : 0;
    }
    const char* vasset_registry_source_path(VAssetRegistryHandle registry, const char* uuid)
    {
        static thread_local std::string buf;
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID id {};
        if (!r || !parseUuid(uuid, id))
            return hold(buf, "");
        return hold(buf, r->registry.lookup(id).sourcePath);
    }
    const char* vasset_registry_imported_path(VAssetRegistryHandle registry, const char* uuid)
    {
        static thread_local std::string buf;
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID id {};
        if (!r || !parseUuid(uuid, id))
            return hold(buf, "");
        return hold(buf, r->registry.lookup(id).importedPath);
    }
    int32_t vasset_registry_type(VAssetRegistryHandle registry, const char* uuid)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID id {};
        if (!r || !parseUuid(uuid, id))
            return 0;
        return static_cast<int32_t>(r->registry.lookup(id).type);
    }

    uint32_t vasset_registry_count(VAssetRegistryHandle registry)
    {
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r)
            return 0;
        r->uuidSnapshot.clear();
        r->uuidSnapshot.reserve(r->registry.getRegistry().size());
        for (const auto& [uuidStr, _] : r->registry.getRegistry())
            r->uuidSnapshot.push_back(uuidStr);
        return static_cast<uint32_t>(r->uuidSnapshot.size());
    }
    const char* vasset_registry_uuid_at(VAssetRegistryHandle registry, uint32_t index)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->uuidSnapshot.size())
            return hold(buf, "");
        return hold(buf, r->uuidSnapshot[index]);
    }

    const char* vasset_registry_source_asset_path(VAssetRegistryHandle registry, const char* assetFullPath, int32_t relative)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || !assetFullPath)
            return hold(buf, "");
        return hold(buf, r->registry.getSourceAssetPath(assetFullPath, relative != 0));
    }
    const char* vasset_registry_imported_asset_path(VAssetRegistryHandle registry, int32_t type, const char* assetName, int32_t relative)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || !assetName)
            return hold(buf, "");
        return hold(buf, r->registry.getImportedAssetPath(static_cast<vasset::VAssetType>(type), assetName, relative != 0));
    }

    // ---- Dependency graph ------------------------------------------------------------------------
    void vasset_registry_add_dependency(VAssetRegistryHandle registry,
                                        const char*          ownerUuid,
                                        int32_t              kind,
                                        const char*          targetUuid,
                                        const char*          targetPath,
                                        const char*          context,
                                        int32_t              required)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID owner {};
        if (!r || !parseUuid(ownerUuid, owner))
            return;
        vasset::VAssetDependency dep;
        dep.kind = static_cast<vasset::VAssetDependencyKind>(kind);
        (void)parseUuid(targetUuid, dep.targetUuid); // optional
        dep.targetPath = targetPath ? targetPath : "";
        dep.context    = context ? context : "";
        dep.required   = required != 0;
        r->registry.addDependency(owner, std::move(dep));
    }
    void vasset_registry_clear_dependencies(VAssetRegistryHandle registry, const char* ownerUuid)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID owner {};
        if (r && parseUuid(ownerUuid, owner))
            r->registry.setDependencies(owner, {});
    }

    uint32_t vasset_registry_dependency_count(VAssetRegistryHandle registry, const char* ownerUuid)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID owner {};
        if (!r || !parseUuid(ownerUuid, owner))
            return 0;
        r->depSnapshot = r->registry.dependencies(owner);
        return static_cast<uint32_t>(r->depSnapshot.size());
    }
    int32_t vasset_registry_dependency_kind_at(VAssetRegistryHandle registry, uint32_t index)
    {
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->depSnapshot.size())
            return 0;
        return static_cast<int32_t>(r->depSnapshot[index].kind);
    }
    const char* vasset_registry_dependency_target_uuid_at(VAssetRegistryHandle registry, uint32_t index)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->depSnapshot.size())
            return hold(buf, "");
        const auto& d = r->depSnapshot[index];
        return hold(buf, d.targetUuid.valid() ? vbase::to_string(d.targetUuid) : std::string {});
    }
    const char* vasset_registry_dependency_target_path_at(VAssetRegistryHandle registry, uint32_t index)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->depSnapshot.size())
            return hold(buf, "");
        return hold(buf, r->depSnapshot[index].targetPath);
    }
    const char* vasset_registry_dependency_context_at(VAssetRegistryHandle registry, uint32_t index)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->depSnapshot.size())
            return hold(buf, "");
        return hold(buf, r->depSnapshot[index].context);
    }
    int32_t vasset_registry_dependency_required_at(VAssetRegistryHandle registry, uint32_t index)
    {
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->depSnapshot.size())
            return 0;
        return r->depSnapshot[index].required ? 1 : 0;
    }

    uint32_t vasset_registry_dependent_count(VAssetRegistryHandle registry, const char* targetUuid)
    {
        auto*       r = reinterpret_cast<VAssetRegistry_t*>(registry);
        vbase::UUID target {};
        if (!r || !parseUuid(targetUuid, target))
            return 0;
        r->dependentSnapshot = r->registry.dependents(target);
        return static_cast<uint32_t>(r->dependentSnapshot.size());
    }
    const char* vasset_registry_dependent_owner_at(VAssetRegistryHandle registry, uint32_t index)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->dependentSnapshot.size())
            return hold(buf, "");
        return hold(buf, vbase::to_string(r->dependentSnapshot[index].ownerUuid));
    }
    int32_t vasset_registry_dependent_kind_at(VAssetRegistryHandle registry, uint32_t index)
    {
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->dependentSnapshot.size())
            return 0;
        return static_cast<int32_t>(r->dependentSnapshot[index].dependency.kind);
    }

    int32_t vasset_registry_validate(VAssetRegistryHandle registry, uint32_t* outIssueCount)
    {
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r)
        {
            if (outIssueCount)
                *outIssueCount = 0;
            return 0;
        }
        r->issueSnapshot = r->registry.validateDependencies().issues;
        if (outIssueCount)
            *outIssueCount = static_cast<uint32_t>(r->issueSnapshot.size());
        return r->issueSnapshot.empty() ? 1 : 0;
    }
    int32_t vasset_registry_issue_kind_at(VAssetRegistryHandle registry, uint32_t index)
    {
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->issueSnapshot.size())
            return 0;
        return static_cast<int32_t>(r->issueSnapshot[index].kind);
    }
    const char* vasset_registry_issue_owner_at(VAssetRegistryHandle registry, uint32_t index)
    {
        static thread_local std::string buf;
        auto* r = reinterpret_cast<VAssetRegistry_t*>(registry);
        if (!r || index >= r->issueSnapshot.size())
            return hold(buf, "");
        return hold(buf, vbase::to_string(r->issueSnapshot[index].ownerUuid));
    }
} // extern "C"
