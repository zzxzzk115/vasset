#include "vasset/vimport.hpp"

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

    static inline void strip_quotes(std::string& s)
    {
        if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
            s = s.substr(1, s.size() - 2);
    }

    enum class Section : uint8_t
    {
        eNone,
        eVImport,
        eSource,
        eOutput,
        eParams,
    };

    static inline Section parse_section(const std::string& name)
    {
        if (name == "vimport")
            return Section::eVImport;
        if (name == "source")
            return Section::eSource;
        if (name == "output")
            return Section::eOutput;
        if (name == "params")
            return Section::eParams;
        return Section::eNone;
    }

    vbase::Result<VImport, AssetError> loadVImport(vbase::StringView filePath)
    {
        std::ifstream f(std::string(filePath), std::ios::binary);
        if (!f)
            return vbase::Result<VImport, AssetError>::err(AssetError::eNotFound);

        std::string line;
        Section     sec = Section::eNone;

        VImport out {};

        auto require_uuid = [&](const std::string& s) -> bool {
            vbase::UUID u {};
            if (!vbase::try_parse_uuid(s.c_str(), u))
                return false;
            out.uid = u;
            return true;
        };

        while (std::getline(f, line))
        {
            trim_inplace(line);
            if (line.empty())
                continue;
            if (line[0] == '#' || line[0] == ';')
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                std::string name = line.substr(1, line.size() - 2);
                trim_inplace(name);
                sec = parse_section(name);
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            trim_inplace(key);
            trim_inplace(val);
            strip_quotes(val);

            switch (sec)
            {
                case Section::eVImport:
                    if (key == "version")
                    {
                        try
                        {
                            out.version = static_cast<uint32_t>(std::stoul(val));
                        }
                        catch (...)
                        {
                            return vbase::Result<VImport, AssetError>::err(AssetError::eInvalidImportFile);
                        }
                    }
                    else if (key == "importer")
                        out.importer = val;
                    else if (key == "uid")
                    {
                        if (!require_uuid(val))
                            return vbase::Result<VImport, AssetError>::err(AssetError::eInvalidImportFile);
                    }
                    break;

                case Section::eSource:
                    if (key == "file")
                        out.source = val;
                    break;

                case Section::eOutput:
                    if (key == "file")
                        out.output = val;
                    break;

                case Section::eParams:
                    out.params[key] = val;
                    break;

                default:
                    break;
            }
        }

        if (out.importer.empty() || !out.uid.valid() || out.source.empty() || out.output.empty())
        {
            std::cerr << "Invalid .vimport: missing required fields" << std::endl;
            return vbase::Result<VImport, AssetError>::err(AssetError::eInvalidImportFile);
        }

        return vbase::Result<VImport, AssetError>::ok(std::move(out));
    }

    vbase::Result<void, AssetError> saveVImport(const VImport& import, vbase::StringView filePath)
    {
        std::ofstream f(std::string(filePath), std::ios::binary);
        if (!f)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        f << "[vimport]\n";
        f << "version=" << import.version << "\n";
        f << "importer=\"" << import.importer << "\"\n";
        f << "uid=\"" << vbase::to_string(import.uid) << "\"\n\n";

        f << "[source]\n";
        f << "file=\"" << import.source << "\"\n\n";

        f << "[output]\n";
        f << "file=\"" << import.output << "\"\n\n";

        f << "[params]\n";
        for (const auto& [k, v] : import.params)
            f << k << "=" << v << "\n";

        return vbase::Result<void, AssetError>::ok();
    }

} // namespace vasset
