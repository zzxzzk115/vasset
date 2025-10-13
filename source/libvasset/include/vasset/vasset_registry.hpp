#pragma once

#include "vasset/vuuid.hpp"

#include <string>
#include <unordered_map>

namespace vasset
{
    class VAssetRegistry
    {
    public:
        bool registerAsset(const VUUID& uuid, const std::string& path);

        std::string lookup(const VUUID& uuid) const;

        void save(const std::string& filename) const;

        void load(const std::string& filename);

    private:
		// UUID -> File Path
        std::unordered_map<VUUID, std::string> m_Registry;
    };
} // namespace vasset