#pragma once

#include "vasset/vasset_type.hpp"
#include "vasset/vuuid.hpp"

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

        void setImportedFolder(const std::string& folder);

        bool registerAsset(const VUUID& uuid, const std::string& path, VAssetType type);

        AssetEntry lookup(const VUUID& uuid) const;

        const std::unordered_map<std::string, AssetEntry>& getRegistry() const;

        void save(const std::string& filename) const;

        void load(const std::string& filename);

        std::string getImportedAssetPath(VAssetType type, const std::string& assetName) const;

    private:
        std::string m_ImportedFolder {"imported"};

        // Key: UUID string, Value: AssetEntry
        std::unordered_map<std::string, AssetEntry> m_Registry;
    };
} // namespace vasset