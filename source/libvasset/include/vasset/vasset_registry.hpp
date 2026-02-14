#pragma once

#include "vasset/vasset_type.hpp"

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

        void setImportedFolder(vbase::StringView folder);

        bool registerAsset(const vbase::UUID& uuid, vbase::StringView path, VAssetType type);
        bool updateRegistry(const vbase::UUID& uuid, vbase::StringView newPath);
        bool unregisterAsset(const vbase::UUID& uuid);

        AssetEntry lookup(const vbase::UUID& uuid) const;

        const std::unordered_map<std::string, AssetEntry>& getRegistry() const;

        void save(vbase::StringView filename) const;

        void load(vbase::StringView filename);

        void cleanup();

        std::string getImportedAssetPath(VAssetType type, vbase::StringView assetName, bool relative) const;

    private:
        std::string m_ImportedFolder {"imported"};

        // Key: UUID string, Value: AssetEntry
        std::unordered_map<std::string, AssetEntry> m_Registry;
    };
} // namespace vasset