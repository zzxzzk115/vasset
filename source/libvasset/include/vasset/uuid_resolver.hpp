#pragma once

#include "vasset/vasset_registry.hpp"
#include "vasset/vpk.hpp"

#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

#include <string>
#include <unordered_map>

namespace vasset
{
    class VUUIDResolver final
    {
    public:
        void setScheme(vbase::StringView scheme) { m_Scheme = std::string(scheme); }

        void clear()
        {
            m_UUIDToPath.clear();
            m_PathToUUID.clear();
        }

        void loadFromAssetRegistry(const VAssetRegistry& registry)
        {
            clear();

            for (const auto& [uuidStr, entry] : registry.getRegistry())
            {
                vbase::UUID uuid {};
                if (!vbase::try_parse_uuid(uuidStr.c_str(), uuid))
                    continue;

                if (!entry.sourcePath.empty())
                    registerAsset(uuid, entry.sourcePath);
                if (!entry.importedPath.empty() && entry.importedPath != entry.sourcePath)
                    m_PathToUUID[entry.importedPath] = uuid;
            }
        }

        void loadFromVPK(const VpkReadOnly& vpk)
        {
            clear();

            for (const auto& r : vpk.registry)
            {
                if (r.pathOffset + r.pathSize > vpk.stringTable.size())
                    continue;

                const char* s = vpk.stringTable.data() + r.pathOffset;
                registerAsset(r.uuid, vbase::StringView(s, r.pathSize));
            }
        }

        void registerAsset(const vbase::UUID& uuid, vbase::StringView logicalPath)
        {
            std::string path(logicalPath);

            m_UUIDToPath[uuid] = path;
            m_PathToUUID[path] = uuid;
        }

        bool resolve(const vbase::UUID& uuid, std::string& outUri) const
        {
            auto it = m_UUIDToPath.find(uuid);
            if (it == m_UUIDToPath.end())
                return false;

            if (m_Scheme.empty())
                outUri = it->second;
            else
                outUri = m_Scheme + "://" + it->second;

            return true;
        }

        bool reverseResolve(vbase::StringView uri, vbase::UUID& outUUID) const
        {
            std::string path(uri);

            if (!m_Scheme.empty())
            {
                if (path.rfind(m_Scheme, 0) != 0)
                    return false;

                path = path.substr(m_Scheme.size() + 3); // +3 to skip "://" prefix
            }

            auto it = m_PathToUUID.find(path);
            if (it == m_PathToUUID.end())
                return false;

            outUUID = it->second;
            return true;
        }

    private:
        std::string                                  m_Scheme;
        std::unordered_map<vbase::UUID, std::string> m_UUIDToPath;
        std::unordered_map<std::string, vbase::UUID> m_PathToUUID;
    };
} // namespace vasset
