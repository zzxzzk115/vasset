#include "vasset/vasset_import_database.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace vasset
{
    namespace
    {
        std::vector<std::string> splitTabs(const std::string& line)
        {
            std::vector<std::string> out;
            std::string              item;
            std::stringstream        ss(line);
            while (std::getline(ss, item, '\t'))
                out.push_back(std::move(item));
            return out;
        }

        uint64_t parseU64(const std::string& value)
        {
            try
            {
                return static_cast<uint64_t>(std::stoull(value));
            }
            catch (...)
            {
                return 0;
            }
        }
    } // namespace

    void VAssetImportDatabase::clear()
    {
        m_BySource.clear();
        m_SourceByUid.clear();
    }

    const VAssetImportDatabase::Record* VAssetImportDatabase::findBySource(vbase::StringView source) const
    {
        auto it = m_BySource.find(std::string(source));
        return it == m_BySource.end() ? nullptr : &it->second;
    }

    const VAssetImportDatabase::Record* VAssetImportDatabase::findByUid(vbase::StringView uid) const
    {
        auto uidIt = m_SourceByUid.find(std::string(uid));
        if (uidIt == m_SourceByUid.end())
            return nullptr;
        auto sourceIt = m_BySource.find(uidIt->second);
        return sourceIt == m_BySource.end() ? nullptr : &sourceIt->second;
    }

    void VAssetImportDatabase::upsert(Record record)
    {
        if (record.source.empty())
            return;

        if (!record.uid.empty())
            m_SourceByUid[record.uid] = record.source;
        m_BySource[record.source] = std::move(record);
    }

    void VAssetImportDatabase::removeBySource(vbase::StringView source)
    {
        auto it = m_BySource.find(std::string(source));
        if (it == m_BySource.end())
            return;
        if (!it->second.uid.empty())
            m_SourceByUid.erase(it->second.uid);
        m_BySource.erase(it);
    }

    vbase::Result<void, AssetError> VAssetImportDatabase::load(vbase::StringView filename)
    {
        clear();

        std::ifstream f(std::string(filename), std::ios::binary);
        if (!f)
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        std::string line;
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            auto cols = splitTabs(line);
            if (cols.size() < 9)
                continue;

            Record r {};
            r.uid             = std::move(cols[0]);
            r.importer        = std::move(cols[1]);
            r.source          = std::move(cols[2]);
            r.output          = std::move(cols[3]);
            r.importerVersion = std::move(cols[4]);
            r.outputSchema    = std::move(cols[5]);
            r.sourceHash      = parseU64(cols[6]);
            r.dependencyHash  = parseU64(cols[7]);
            r.paramsHash      = parseU64(cols[8]);
            upsert(std::move(r));
        }

        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetImportDatabase::save(vbase::StringView filename) const
    {
        std::filesystem::path p(filename);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());

        std::ofstream f(p, std::ios::binary);
        if (!f)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        f << "# vasset import database (tsv)\n";
        f << "# uid\timporter\tsource\toutput\timporter_version\toutput_schema\tsource_hash\tdependency_hash\tparams_hash\n";

        for (const auto& [_, r] : m_BySource)
        {
            f << r.uid << '\t' << r.importer << '\t' << r.source << '\t' << r.output << '\t'
              << r.importerVersion << '\t' << r.outputSchema << '\t' << r.sourceHash << '\t'
              << r.dependencyHash << '\t' << r.paramsHash << '\n';
        }

        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset
