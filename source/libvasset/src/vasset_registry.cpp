#include "vasset/vasset_registry.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace vasset
{
    static inline void trim_inplace(std::string& s)
    {
        auto   is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
        size_t a     = 0;
        while (a < s.size() && is_ws(static_cast<unsigned char>(s[a])))
            ++a;
        size_t b = s.size();
        while (b > a && is_ws(static_cast<unsigned char>(s[b - 1])))
            --b;
        if (a == 0 && b == s.size())
            return;
        s = s.substr(a, b - a);
    }

    void VAssetRegistry::setAssetRootPath(vbase::StringView rootPath) { m_AssetRootPath = std::string(rootPath); }
    void VAssetRegistry::setImportedFolderName(vbase::StringView name) { m_ImportedFolderName = std::string(name); }

    vbase::Result<void, AssetError>
    VAssetRegistry::registerAsset(const vbase::UUID& uuid, vbase::StringView path, VAssetType type)
    {
        if (!uuid.valid())
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        AssetEntry e;
        e.path = std::string(path);
        e.type = type;

        m_Registry[vbase::to_string(uuid)] = std::move(e);
        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetRegistry::updateRegistry(const vbase::UUID& uuid, vbase::StringView newPath)
    {
        auto key = vbase::to_string(uuid);
        auto it  = m_Registry.find(key);
        if (it == m_Registry.end())
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        it->second.path = std::string(newPath);
        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetRegistry::unregisterAsset(const vbase::UUID& uuid)
    {
        auto key = vbase::to_string(uuid);
        auto it  = m_Registry.find(key);
        if (it == m_Registry.end())
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        m_Registry.erase(it);
        return vbase::Result<void, AssetError>::ok();
    }

    VAssetRegistry::AssetEntry VAssetRegistry::lookup(const vbase::UUID& uuid) const
    {
        auto it = m_Registry.find(vbase::to_string(uuid));
        if (it == m_Registry.end())
            return AssetEntry {};
        return it->second;
    }

    const std::unordered_map<std::string, VAssetRegistry::AssetEntry>& VAssetRegistry::getRegistry() const
    {
        return m_Registry;
    }

    vbase::Result<void, AssetError> VAssetRegistry::save(vbase::StringView filename) const
    {
        std::filesystem::path p(filename);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());

        std::ofstream f(p, std::ios::binary);
        if (!f)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        f << "# vasset registry (tsv)\n";
        f << "# uuid\ttype\tpath\n";

        for (const auto& [uuidStr, entry] : m_Registry)
        {
            f << uuidStr << "\t" << toString(entry.type) << "\t" << entry.path << "\n";
        }

        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetRegistry::load(vbase::StringView filename)
    {
        std::ifstream f(std::string(filename), std::ios::binary);
        if (!f)
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        // read all lines
        f.seekg(0, std::ios::end);
        const size_t size = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::string file;
        file.resize(size);
        if (size > 0)
            f.read(file.data(), size);

        if (!f && size != 0)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        m_Registry.clear();

        // reserve
        size_t lineCount = 0;
        for (char c : file)
            if (c == '\n')
                ++lineCount;

        m_Registry.reserve(lineCount);

        const char* p   = file.data();
        const char* end = p + file.size();

        while (p < end)
        {
            // skip empty lines
            if (*p == '\n' || *p == '\r')
            {
                ++p;
                continue;
            }

            // skip comments
            if (*p == '#')
            {
                while (p < end && *p != '\n')
                    ++p;
                continue;
            }

            // parse uuid
            const char* uuidBegin = p;
            while (p < end && *p != '\t' && *p != '\n')
                ++p;

            if (p >= end || *p != '\t')
            {
                while (p < end && *p != '\n')
                    ++p;
                continue;
            }

            vbase::StringView uuidView(uuidBegin, p - uuidBegin);
            ++p; // skip '\t'

            // parse type
            const char* typeBegin = p;
            while (p < end && *p != '\t' && *p != '\n')
                ++p;

            if (p >= end || *p != '\t')
            {
                while (p < end && *p != '\n')
                    ++p;
                continue;
            }

            vbase::StringView typeView(typeBegin, p - typeBegin);
            ++p;

            // parse path
            const char* pathBegin = p;
            while (p < end && *p != '\n')
                ++p;

            vbase::StringView pathView(pathBegin, p - pathBegin);

            // skip new lines
            if (p < end && *p == '\n')
                ++p;

            // create an entry
            AssetEntry e;
            e.type = fromString(typeView);
            if (e.type == VAssetType::eUnknown)
                continue;

            e.path.assign(pathView.data(), pathView.size());

            vbase::UUID id {};
            if (!vbase::try_parse_uuid(std::string(uuidView).c_str(), id))
                continue;

            m_Registry[vbase::to_string(id)] = std::move(e);
        }

        return vbase::Result<void, AssetError>::ok();
    }

    void VAssetRegistry::cleanup()
    {
        // Remove entries whose target file no longer exists.
        for (auto it = m_Registry.begin(); it != m_Registry.end();)
        {
            auto osPath = std::filesystem::path(m_AssetRootPath) / it->second.path;

            if (!std::filesystem::exists(osPath))
                it = m_Registry.erase(it);
            else
                ++it;
        }
    }

    std::string VAssetRegistry::getAssetRootPath() const { return m_AssetRootPath; }

    std::string VAssetRegistry::getSourceAssetPath(vbase::StringView assetFullPath, bool relative) const
    {
        std::filesystem::path fullPath(assetFullPath);
        std::filesystem::path assetRoot(m_AssetRootPath);

        if (relative)
        {
            std::filesystem::path relativePath = std::filesystem::relative(fullPath, assetRoot);
            return relativePath.string();
        }
        else
        {
            return fullPath.string();
        }
    }

    std::string VAssetRegistry::getImportedAssetPath(VAssetType type, vbase::StringView assetName, bool relative) const
    {
        std::string folder = m_ImportedFolderName;

        std::string out = folder + "/" + toString(type) + "/" + std::string(assetName);

        if (!relative)
        {
            std::filesystem::path assetRoot(m_AssetRootPath);
            std::filesystem::path fullPath = assetRoot / out;
            return fullPath.string();
        }

        return out;
    }
} // namespace vasset
