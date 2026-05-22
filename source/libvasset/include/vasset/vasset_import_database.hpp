#pragma once

#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace vasset
{
    class VAssetImportDatabase
    {
    public:
        struct Record
        {
            std::string uid;
            std::string importer;
            std::string source;
            std::string output;
            std::string importerVersion;
            std::string outputSchema;
            uint64_t    sourceHash {0};
            uint64_t    dependencyHash {0};
            uint64_t    paramsHash {0};
        };

        void clear();

        [[nodiscard]] const Record* findBySource(vbase::StringView source) const;
        [[nodiscard]] const Record* findByUid(vbase::StringView uid) const;

        void upsert(Record record);
        void removeBySource(vbase::StringView source);

        vbase::Result<void, AssetError> load(vbase::StringView filename);
        vbase::Result<void, AssetError> save(vbase::StringView filename) const;

        [[nodiscard]] const std::unordered_map<std::string, Record>& records() const { return m_BySource; }

    private:
        std::unordered_map<std::string, Record> m_BySource;
        std::unordered_map<std::string, std::string> m_SourceByUid;
    };
} // namespace vasset
