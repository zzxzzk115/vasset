#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vasset_type.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

#include <string>
#include <unordered_map>

namespace vasset
{
    class VAssetRegistry
    {
    public:
        struct AssetEntry
        {
            std::string path;
            VAssetType  type {VAssetType::eUnknown};

            std::string toString() const
            {
                return "AssetEntry { path: " + path + ", type: " + vasset::toString(type) + " }";
            }
        };

        void setAssetRootPath(vbase::StringView rootPath);
        void setImportedFolderName(vbase::StringView name);

        vbase::Result<void, AssetError> registerAsset(const vbase::UUID& uuid, vbase::StringView path, VAssetType type);
        vbase::Result<void, AssetError> updateRegistry(const vbase::UUID& uuid, vbase::StringView newPath);
        vbase::Result<void, AssetError> unregisterAsset(const vbase::UUID& uuid);

        AssetEntry lookup(const vbase::UUID& uuid) const;

        const std::unordered_map<std::string, AssetEntry>& getRegistry() const;

        vbase::Result<void, AssetError> save(vbase::StringView filename) const;

        vbase::Result<void, AssetError> load(vbase::StringView filename);

        void cleanup();

        std::string getAssetRootPath() const;

        std::string getSourceAssetPath(vbase::StringView assetFullPath, bool relative) const;
        std::string getImportedAssetPath(VAssetType type, vbase::StringView assetName, bool relative) const;

    private:
        std::string m_AssetRootPath {"assets"};
        std::string m_ImportedFolderName {"imported"};

        // Key: UUID string, Value: AssetEntry
        std::unordered_map<std::string, AssetEntry> m_Registry;
    };
} // namespace vasset