#include "vasset/vasset_registry.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

namespace
{
    using json = nlohmann::json;
    using namespace vasset;

    VAssetType fromString(vbase::StringView str)
    {
        if (str == "texture")
            return VAssetType::eTexture;
        if (str == "material")
            return VAssetType::eMaterial;
        if (str == "mesh")
            return VAssetType::eMesh;
        return VAssetType::eUnknown;
    }

    void to_json(json& j, const VAssetRegistry::AssetEntry& e)
    {
        j = json {{"path", e.path}, {"type", toString(e.type)}};
    }

    void from_json(const json& j, VAssetRegistry::AssetEntry& e)
    {
        e.path = j.at("path").get<std::string>();
        e.type = fromString(j.at("type").get<std::string>());
    }
} // namespace

namespace vasset
{
    void VAssetRegistry::setImportedFolder(vbase::StringView folder) { m_ImportedFolder = folder; }

    bool VAssetRegistry::registerAsset(const vbase::UUID& uuid, vbase::StringView path, VAssetType type)
    {
        m_Registry[vbase::to_string(uuid)] = {path.data(), type};
        return true;
    }

    bool VAssetRegistry::updateRegistry(const vbase::UUID& uuid, vbase::StringView newPath)
    {
        auto it = m_Registry.find(vbase::to_string(uuid));
        if (it == m_Registry.end())
        {
            return false;
        }

        it->second.path = newPath;
        return true;
    }

    bool VAssetRegistry::unregisterAsset(const vbase::UUID& uuid)
    {
        auto it = m_Registry.find(vbase::to_string(uuid));
        if (it == m_Registry.end())
        {
            return false;
        }

        m_Registry.erase(it);
        return true;
    }

    VAssetRegistry::AssetEntry VAssetRegistry::lookup(const vbase::UUID& uuid) const
    {
        auto it = m_Registry.find(vbase::to_string(uuid));
        return it != m_Registry.end() ? it->second : AssetEntry {};
    }

    const std::unordered_map<std::string, VAssetRegistry::AssetEntry>& VAssetRegistry::getRegistry() const
    {
        return m_Registry;
    }

    void VAssetRegistry::save(vbase::StringView filename) const
    {
        nlohmann::json j;
        for (const auto& [uuidStr, entry] : m_Registry)
        {
            nlohmann::json jEntry;
            to_json(jEntry, entry);
            j[uuidStr] = jEntry;
        }
        std::ofstream out(filename);
        out << j.dump(4);
    }

    void VAssetRegistry::load(vbase::StringView filename)
    {
        std::ifstream in(filename);
        if (!in.is_open())
            return;
        nlohmann::json j;
        in >> j;
        for (const auto& [uuidStr, jEntry] : j.items())
        {
            VAssetRegistry::AssetEntry entry;
            from_json(jEntry, entry);
            m_Registry[uuidStr] = entry;
        }
    }

    void VAssetRegistry::cleanup()
    {
        // Traverse the registry and remove entries whose files do not exist
        for (auto it = m_Registry.begin(); it != m_Registry.end();)
        {
            std::string fullPath = m_ImportedFolder + "/" + it->second.path;
            if (!std::filesystem::exists(fullPath))
            {
                std::cerr << "Removing missing asset from registry: " << fullPath << std::endl;
                it = m_Registry.erase(it);
                std::filesystem::remove(fullPath);
            }
            else
            {
                ++it;
            }
        }
    }

    std::string VAssetRegistry::getImportedAssetPath(VAssetType type, vbase::StringView assetName, bool relative) const
    {
        std::string path;

        std::string finalAssetName;

        if (assetName.empty())
        {
            // Use a random UUID as name to avoid collisions
            finalAssetName = vbase::to_string(vbase::uuid_random());
        }
        else
        {
            finalAssetName = assetName;
        }

        switch (type)
        {
            case VAssetType::eTexture:
                path = "textures/" + finalAssetName + ".vtex";
                break;
            case VAssetType::eMaterial:
                path = "materials/" + finalAssetName + ".vmat";
                break;
            case VAssetType::eMesh:
                path = "meshes/" + finalAssetName + ".vmesh";
                break;
            case VAssetType::eUnknown:
            default:
                path = "unknown/" + finalAssetName;
                break;
        }

        if (!relative)
        {
            path = m_ImportedFolder + "/" + path;
        }

        return path;
    }
} // namespace vasset