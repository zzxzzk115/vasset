#include "vasset/vasset_registry.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace vasset
{
    bool VAssetRegistry::registerAsset(const VUUID& uuid, const std::string& path)
    {
        m_Registry[uuid] = path;
        return true;
    }

    std::string VAssetRegistry::lookup(const VUUID& uuid) const
    {
        auto it = m_Registry.find(uuid);
        return it != m_Registry.end() ? it->second : "";
    }

    void VAssetRegistry::save(const std::string& filename) const
    {
        nlohmann::json j;
        for (const auto& [uuid, path] : m_Registry)
        {
            j[uuid.toString()] = path;
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
        for (const auto& [uuidStr, path] : j.items())
        {
            m_Registry[VUUID::fromString(uuidStr)] = path;
        }
    }
} // namespace vasset