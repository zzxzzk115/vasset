#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vasset_type.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vasset
{
    enum class VAssetDependencyKind : uint8_t
    {
        eUnknown = 0,
        eSourceInclude,
        eRuntimePayload,
        eMaterialTexture,
        eSkeleton,
        eAnimation,
        eScript,
        eRenderGraphFeature,
        eShaderLibrary,
        eSceneComponent,
    };

    const char*          toString(VAssetDependencyKind kind);
    VAssetDependencyKind dependencyKindFromString(vbase::StringView text);

    struct VAssetDependency
    {
        VAssetDependencyKind kind {VAssetDependencyKind::eUnknown};
        vbase::UUID          targetUuid {};
        std::string          targetPath;
        std::string          context;
        bool                 required {true};
    };

    struct VAssetDependencyIssue
    {
        enum class Kind : uint8_t
        {
            eMissingTarget = 0,
            eCycle,
        };

        Kind             kind {Kind::eMissingTarget};
        vbase::UUID      ownerUuid {};
        VAssetDependency dependency {};
        std::vector<vbase::UUID> cycle;
    };

    struct VAssetDependent
    {
        vbase::UUID      ownerUuid {};
        VAssetDependency dependency {};
    };

    struct VAssetDependencyValidation
    {
        std::vector<VAssetDependencyIssue> issues;

        bool ok() const { return issues.empty(); }
    };

    class VAssetRegistry
    {
    public:
        struct AssetEntry
        {
            std::string sourcePath;   // relative to asset root, e.g. "models/house.fbx"
            std::string importedPath; // relative to asset root, e.g. "imported/models/house"
            VAssetType  type {VAssetType::eUnknown};

            std::string toString() const
            {
                return "AssetEntry { sourcePath: " + sourcePath + ", importedPath: " + importedPath +
                       ", type: " + vasset::toString(type) + " }";
            }
        };

        void setAssetRootPath(vbase::StringView rootPath);
        void setImportedFolderName(vbase::StringView name);

        vbase::Result<void, AssetError> registerAsset(const vbase::UUID& uuid,
                                                      vbase::StringView  sourcePath,
                                                      vbase::StringView  importedPath,
                                                      VAssetType         type);
        vbase::Result<void, AssetError> updateRegistry(const vbase::UUID& uuid, vbase::StringView newImportedPath);
        vbase::Result<void, AssetError> unregisterAsset(const vbase::UUID& uuid);

        AssetEntry lookup(const vbase::UUID& uuid) const;

        const std::unordered_map<std::string, AssetEntry>& getRegistry() const;

        void setDependencies(const vbase::UUID& uuid, std::vector<VAssetDependency> dependencies);
        void addDependency(const vbase::UUID& uuid, VAssetDependency dependency);
        std::vector<VAssetDependency> dependencies(const vbase::UUID& uuid) const;
        std::vector<VAssetDependent>  dependents(const vbase::UUID& uuid) const;
        VAssetDependencyValidation    validateDependencies() const;

        vbase::Result<void, AssetError> save(vbase::StringView filename) const;

        vbase::Result<void, AssetError> load(vbase::StringView filename);

        void cleanup();

        std::string getAssetRootPath() const;
        std::string getImportedFolderName() const;

        std::string getSourceAssetPath(vbase::StringView assetFullPath, bool relative) const;
        std::string getImportedAssetPath(VAssetType type, vbase::StringView assetName, bool relative) const;

    private:
        std::string m_AssetRootPath {"assets"};
        std::string m_ImportedFolderName {"imported"};

        // Key: UUID string, Value: AssetEntry
        std::unordered_map<std::string, AssetEntry> m_Registry;

        // Key: owner UUID string, Value: outgoing dependency edges
        std::unordered_map<std::string, std::vector<VAssetDependency>> m_Dependencies;
    };
} // namespace vasset
