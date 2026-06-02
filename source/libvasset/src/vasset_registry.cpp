#include "vasset/vasset_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <unordered_set>

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

    static std::vector<std::string> split_tsv(const std::string& line)
    {
        std::vector<std::string> out;
        size_t                   begin = 0;
        while (begin <= line.size())
        {
            const size_t end = line.find('\t', begin);
            if (end == std::string::npos)
            {
                out.emplace_back(line.substr(begin));
                break;
            }
            out.emplace_back(line.substr(begin, end - begin));
            begin = end + 1;
        }
        return out;
    }

    static std::string string_from_view(vbase::StringView text)
    {
        return std::string(text.data(), text.size());
    }

    static bool string_to_bool(const std::string& text)
    {
        return text == "1" || text == "true" || text == "required";
    }

    const char* toString(VAssetDependencyKind kind)
    {
        switch (kind)
        {
            case VAssetDependencyKind::eSourceInclude:
                return "SourceInclude";
            case VAssetDependencyKind::eRuntimePayload:
                return "RuntimePayload";
            case VAssetDependencyKind::eMaterialTexture:
                return "MaterialTexture";
            case VAssetDependencyKind::eSkeleton:
                return "Skeleton";
            case VAssetDependencyKind::eAnimation:
                return "Animation";
            case VAssetDependencyKind::eScript:
                return "Script";
            case VAssetDependencyKind::eRenderGraphFeature:
                return "RenderGraphFeature";
            case VAssetDependencyKind::eShaderLibrary:
                return "ShaderLibrary";
            case VAssetDependencyKind::eSceneComponent:
                return "SceneComponent";
            case VAssetDependencyKind::eUnknown:
            default:
                return "Unknown";
        }
    }

    VAssetDependencyKind dependencyKindFromString(vbase::StringView text)
    {
        const auto s = string_from_view(text);
        if (s == "SourceInclude")
            return VAssetDependencyKind::eSourceInclude;
        if (s == "RuntimePayload")
            return VAssetDependencyKind::eRuntimePayload;
        if (s == "MaterialTexture")
            return VAssetDependencyKind::eMaterialTexture;
        if (s == "Skeleton")
            return VAssetDependencyKind::eSkeleton;
        if (s == "Animation")
            return VAssetDependencyKind::eAnimation;
        if (s == "Script")
            return VAssetDependencyKind::eScript;
        if (s == "RenderGraphFeature")
            return VAssetDependencyKind::eRenderGraphFeature;
        if (s == "ShaderLibrary")
            return VAssetDependencyKind::eShaderLibrary;
        if (s == "SceneComponent")
            return VAssetDependencyKind::eSceneComponent;
        return VAssetDependencyKind::eUnknown;
    }

    void VAssetRegistry::setAssetRootPath(vbase::StringView rootPath) { m_AssetRootPath = std::string(rootPath); }
    void VAssetRegistry::setImportedFolderName(vbase::StringView name) { m_ImportedFolderName = std::string(name); }

    vbase::Result<void, AssetError> VAssetRegistry::registerAsset(const vbase::UUID& uuid,
                                                                  vbase::StringView  sourcePath,
                                                                  vbase::StringView  importedPath,
                                                                  VAssetType         type)
    {
        if (!uuid.valid())
            return vbase::Result<void, AssetError>::err(AssetError::eInvalidFormat);

        const std::string uuidKey(vbase::to_string(uuid));
        const std::string sourcePathStr(sourcePath);

        // Keep source_path unique in the registry so reimports can safely migrate
        // imported paths without leaving duplicate rows behind.
        for (auto it = m_Registry.begin(); it != m_Registry.end();)
        {
            if (it->first != uuidKey && it->second.sourcePath == sourcePathStr)
                it = m_Registry.erase(it);
            else
                ++it;
        }

        AssetEntry e;
        e.sourcePath   = sourcePathStr;
        e.importedPath = std::string(importedPath);
        e.type         = type;

        m_Registry[uuidKey] = std::move(e);
        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetRegistry::updateRegistry(const vbase::UUID& uuid,
                                                                   vbase::StringView  newImportedPath)
    {
        auto key = vbase::to_string(uuid);
        auto it  = m_Registry.find(key);
        if (it == m_Registry.end())
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        it->second.importedPath = std::string(newImportedPath);
        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetRegistry::unregisterAsset(const vbase::UUID& uuid)
    {
        auto key = vbase::to_string(uuid);
        auto it  = m_Registry.find(key);
        if (it == m_Registry.end())
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        m_Registry.erase(it);
        m_Dependencies.erase(key);
        for (auto& [_, deps] : m_Dependencies)
        {
            deps.erase(std::remove_if(deps.begin(),
                                      deps.end(),
                                      [&](const VAssetDependency& dep) {
                                          return dep.targetUuid.valid() &&
                                                 vbase::to_string(dep.targetUuid) == key;
                                      }),
                       deps.end());
        }
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

    void VAssetRegistry::setDependencies(const vbase::UUID& uuid, std::vector<VAssetDependency> dependencies)
    {
        if (!uuid.valid())
            return;

        dependencies.erase(std::remove_if(dependencies.begin(),
                                          dependencies.end(),
                                          [](const VAssetDependency& dep) {
                                              return dep.kind == VAssetDependencyKind::eUnknown &&
                                                     !dep.targetUuid.valid() && dep.targetPath.empty();
                                          }),
                           dependencies.end());
        m_Dependencies[vbase::to_string(uuid)] = std::move(dependencies);
    }

    void VAssetRegistry::addDependency(const vbase::UUID& uuid, VAssetDependency dependency)
    {
        if (!uuid.valid())
            return;
        if (dependency.kind == VAssetDependencyKind::eUnknown && !dependency.targetUuid.valid() &&
            dependency.targetPath.empty())
        {
            return;
        }
        m_Dependencies[vbase::to_string(uuid)].push_back(std::move(dependency));
    }

    std::vector<VAssetDependency> VAssetRegistry::dependencies(const vbase::UUID& uuid) const
    {
        const auto it = m_Dependencies.find(vbase::to_string(uuid));
        return it != m_Dependencies.end() ? it->second : std::vector<VAssetDependency> {};
    }

    std::vector<VAssetDependent> VAssetRegistry::dependents(const vbase::UUID& uuid) const
    {
        std::vector<VAssetDependent> out;
        if (!uuid.valid())
            return out;

        const auto uuidStr = vbase::to_string(uuid);
        for (const auto& [ownerStr, deps] : m_Dependencies)
        {
            vbase::UUID owner {};
            if (!vbase::try_parse_uuid(ownerStr.c_str(), owner))
                continue;

            for (const auto& dep : deps)
            {
                if (dep.targetUuid.valid() && vbase::to_string(dep.targetUuid) == uuidStr)
                    out.push_back(VAssetDependent {.ownerUuid = owner, .dependency = dep});
            }
        }
        return out;
    }

    VAssetDependencyValidation VAssetRegistry::validateDependencies() const
    {
        VAssetDependencyValidation out;

        auto pathExists = [&](const std::string& path) {
            if (path.empty())
                return false;
            for (const auto& [_, entry] : m_Registry)
            {
                if (entry.sourcePath == path || entry.importedPath == path)
                    return true;
            }
            return false;
        };

        for (const auto& [ownerStr, deps] : m_Dependencies)
        {
            vbase::UUID owner {};
            if (!vbase::try_parse_uuid(ownerStr.c_str(), owner))
                continue;

            for (const auto& dep : deps)
            {
                if (!dep.required)
                    continue;

                bool found = false;
                if (dep.targetUuid.valid())
                    found = m_Registry.find(vbase::to_string(dep.targetUuid)) != m_Registry.end();
                if (!found && !dep.targetPath.empty())
                    found = pathExists(dep.targetPath);

                if (!found)
                {
                    VAssetDependencyIssue issue;
                    issue.kind       = VAssetDependencyIssue::Kind::eMissingTarget;
                    issue.ownerUuid  = owner;
                    issue.dependency = dep;
                    out.issues.push_back(std::move(issue));
                }
            }
        }

        std::unordered_set<std::string> visiting;
        std::unordered_set<std::string> visited;
        std::vector<std::string>        stack;

        auto emitCycle = [&](const std::string& repeated) {
            VAssetDependencyIssue issue;
            issue.kind = VAssetDependencyIssue::Kind::eCycle;
            bool inCycle = false;
            for (const auto& uuidStr : stack)
            {
                if (uuidStr == repeated)
                    inCycle = true;
                if (!inCycle)
                    continue;
                vbase::UUID uuid {};
                if (vbase::try_parse_uuid(uuidStr.c_str(), uuid))
                    issue.cycle.push_back(uuid);
            }
            vbase::UUID repeatedUuid {};
            if (vbase::try_parse_uuid(repeated.c_str(), repeatedUuid))
                issue.cycle.push_back(repeatedUuid);
            out.issues.push_back(std::move(issue));
        };

        std::function<void(const std::string&)> dfs = [&](const std::string& owner) {
            if (visited.contains(owner))
                return;
            if (visiting.contains(owner))
            {
                emitCycle(owner);
                return;
            }

            visiting.insert(owner);
            stack.push_back(owner);

            const auto depIt = m_Dependencies.find(owner);
            if (depIt != m_Dependencies.end())
            {
                for (const auto& dep : depIt->second)
                {
                    if (!dep.targetUuid.valid())
                        continue;
                    const auto target = vbase::to_string(dep.targetUuid);
                    if (m_Registry.find(target) != m_Registry.end())
                        dfs(target);
                }
            }

            stack.pop_back();
            visiting.erase(owner);
            visited.insert(owner);
        };

        for (const auto& [uuidStr, _] : m_Registry)
            dfs(uuidStr);

        return out;
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
        f << "# asset_root\t" << m_AssetRootPath << "\n";
        f << "# imported_folder\t" << m_ImportedFolderName << "\n";
        f << "# uuid\ttype\tsource_path\timported_path\n";
        f << "# @dep\towner_uuid\tkind\ttarget_uuid\ttarget_path\tcontext\trequired\n";

        for (const auto& [uuidStr, entry] : m_Registry)
        {
            f << uuidStr << "\t" << toString(entry.type) << "\t" << entry.sourcePath << "\t" << entry.importedPath
              << "\n";
        }
        for (const auto& [ownerStr, deps] : m_Dependencies)
        {
            if (m_Registry.find(ownerStr) == m_Registry.end())
                continue;
            for (const auto& dep : deps)
            {
                f << "@dep\t" << ownerStr << "\t" << toString(dep.kind) << "\t"
                  << (dep.targetUuid.valid() ? vbase::to_string(dep.targetUuid) : std::string {}) << "\t"
                  << dep.targetPath << "\t" << dep.context << "\t" << (dep.required ? "1" : "0") << "\n";
            }
        }

        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetRegistry::load(vbase::StringView filename)
    {
        std::ifstream f(std::string(filename), std::ios::binary);
        if (!f)
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        m_Registry.clear();
        m_Dependencies.clear();

        std::string line;
        while (std::getline(f, line))
        {
            trim_inplace(line);
            if (line.empty())
                continue;

            if (line[0] == '#')
            {
                constexpr std::string_view assetRootKey = "# asset_root\t";
                constexpr std::string_view importedKey  = "# imported_folder\t";
                if (line.starts_with(assetRootKey))
                    m_AssetRootPath = line.substr(assetRootKey.size());
                else if (line.starts_with(importedKey))
                    m_ImportedFolderName = line.substr(importedKey.size());
                continue;
            }

            const auto cols = split_tsv(line);
            if (cols.empty())
                continue;

            if (cols[0] == "@dep")
            {
                if (cols.size() < 5)
                    continue;

                vbase::UUID owner {};
                if (!vbase::try_parse_uuid(cols[1].c_str(), owner))
                    continue;

                VAssetDependency dep;
                dep.kind = dependencyKindFromString(cols[2]);
                if (!cols[3].empty())
                    (void)vbase::try_parse_uuid(cols[3].c_str(), dep.targetUuid);
                dep.targetPath = cols[4];
                if (cols.size() > 5)
                    dep.context = cols[5];
                if (cols.size() > 6)
                    dep.required = string_to_bool(cols[6]);

                addDependency(owner, std::move(dep));
                continue;
            }

            if (cols.size() < 4)
                continue;

            vbase::UUID id {};
            if (!vbase::try_parse_uuid(cols[0].c_str(), id))
                continue;

            AssetEntry e;
            e.type = fromString(cols[1]);
            if (e.type == VAssetType::eUnknown)
                continue;
            e.sourcePath   = cols[2];
            e.importedPath = cols[3];

            m_Registry[vbase::to_string(id)] = std::move(e);
        }

        if (!f.eof())
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        return vbase::Result<void, AssetError>::ok();
    }

    void VAssetRegistry::cleanup()
    {
        // Remove entries whose source or imported payload no longer exists.
        for (auto it = m_Registry.begin(); it != m_Registry.end();)
        {
            std::string physicalSourcePath = it->second.sourcePath;
            if (const auto fragment = physicalSourcePath.find('#'); fragment != std::string::npos)
                physicalSourcePath.resize(fragment);

            const auto sourcePath = std::filesystem::path(m_AssetRootPath) / physicalSourcePath;
            const auto importPath = std::filesystem::path(m_AssetRootPath) / it->second.importedPath;

            const bool missingSource = !physicalSourcePath.empty() && !std::filesystem::exists(sourcePath);
            const bool missingImport = !it->second.importedPath.empty() && !std::filesystem::exists(importPath);
            if (missingSource || missingImport)
                it = m_Registry.erase(it);
            else
                ++it;
        }
    }

    std::string VAssetRegistry::getAssetRootPath() const { return m_AssetRootPath; }
    std::string VAssetRegistry::getImportedFolderName() const { return m_ImportedFolderName; }

    std::string VAssetRegistry::getSourceAssetPath(vbase::StringView assetFullPath, bool relative) const
    {
        std::filesystem::path fullPath(assetFullPath);
        std::filesystem::path assetRoot(m_AssetRootPath);

        if (relative)
        {
            std::filesystem::path relativePath = std::filesystem::relative(fullPath, assetRoot);
            return relativePath.generic_string();
        }
        else
        {
            return fullPath.generic_string();
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
            return fullPath.generic_string();
        }

        return out;
    }
} // namespace vasset
