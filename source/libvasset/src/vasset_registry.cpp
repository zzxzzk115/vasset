#include "vasset/vasset_registry.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace
{
    using json = nlohmann::json;
    using namespace vasset;

    VAssetType fromString(const std::string& str)
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
    void VAssetRegistry::setImportedFolder(const std::string& folder) { m_ImportedFolder = folder; }

    bool VAssetRegistry::registerAsset(const VUUID& uuid, const std::string& path, VAssetType type)
    {
        m_Registry[uuid.toString()] = {path, type};
        return true;
    }

    VAssetRegistry::AssetEntry VAssetRegistry::lookup(const VUUID& uuid) const
    {
        auto it = m_Registry.find(uuid.toString());
        return it != m_Registry.end() ? it->second : AssetEntry {};
    }

    const std::unordered_map<std::string, VAssetRegistry::AssetEntry>& VAssetRegistry::getRegistry() const
    {
        return m_Registry;
    }

    void VAssetRegistry::save(const std::string& filename) const
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

    void VAssetRegistry::load(const std::string& filename)
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

    std::string VAssetRegistry::getImportedAssetPath(VAssetType type, const std::string& assetName, bool relative) const
    {
        std::string path;

        std::string finalAssetName;

        if (assetName.empty())
        {
            // Use a random UUID as name to avoid collisions
            finalAssetName = VUUID::generate().toString();
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