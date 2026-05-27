#include <vasset/vasset_pack.hpp>

#include <vasset/vasset_registry.hpp>
#include <vasset/vpk.hpp>
#include <vasset/vtexture.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>

namespace vasset
{
    namespace
    {
        std::vector<std::byte> readBinaryFile(const std::filesystem::path& filePath)
        {
            std::ifstream f(filePath, std::ios::binary);
            if (!f)
                return {};

            f.seekg(0, std::ios::end);
            const size_t sz = static_cast<size_t>(f.tellg());
            f.seekg(0, std::ios::beg);

            std::vector<std::byte> data(sz);
            if (sz)
                f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));
            return data;
        }

        std::string normalizePackFilterPath(std::string path)
        {
            for (char& ch : path)
            {
                if (ch == '\\')
                    ch = '/';
            }

            while (!path.empty() && path.front() == '/')
                path.erase(path.begin());
            while (!path.empty() && path.back() == '/')
                path.pop_back();

            return path;
        }

        bool matchesPackFilters(std::string_view logicalPath, const std::vector<std::string>& includePaths)
        {
            if (includePaths.empty())
                return true;

            const std::string normalizedPath = normalizePackFilterPath(std::string(logicalPath));
            for (const auto& includePathRaw : includePaths)
            {
                const std::string includePath = normalizePackFilterPath(includePathRaw);
                if (includePath.empty())
                    continue;
                if (normalizedPath == includePath)
                    return true;
                if (normalizedPath.size() > includePath.size() && normalizedPath.rfind(includePath, 0) == 0 &&
                    normalizedPath[includePath.size()] == '/')
                    return true;
            }
            return false;
        }

        bool isRuntimeRawAssetPath(const std::string& relPath)
        {
            std::filesystem::path p(relPath);
            std::string           ext = p.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            return ext == ".vscn" || ext == ".vmanifest" || ext == ".lua";
        }

        bool shouldPackSourcePayloadForEntry(const VAssetRegistry::AssetEntry& entry)
        {
            std::filesystem::path p(entry.sourcePath);
            std::string           ext = p.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            return (entry.type == VAssetType::eScene && ext == ".vscn") ||
                   (entry.type == VAssetType::eSceneManifest && ext == ".vmanifest") ||
                   (entry.type == VAssetType::eScriptLua && ext == ".lua");
        }

        VAssetType inferRuntimeRawAssetType(const std::string& relPath)
        {
            std::filesystem::path p(relPath);
            std::string           ext = p.extension().generic_string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });

            if (ext == ".vscn")
                return VAssetType::eScene;
            if (ext == ".vmanifest")
                return VAssetType::eSceneManifest;
            if (ext == ".lua")
                return VAssetType::eScriptLua;
            return VAssetType::eUnknown;
        }

        std::string packLogicalPathForEntry(const VAssetRegistry::AssetEntry& entry)
        {
            if (!entry.sourcePath.empty())
                return entry.sourcePath;
            return entry.importedPath;
        }

        std::string packDataPathForEntry(const VAssetRegistry::AssetEntry& entry,
                                         const std::filesystem::path&      assetRoot)
        {
            if (!entry.sourcePath.empty() && shouldPackSourcePayloadForEntry(entry))
            {
                std::error_code ec;
                if (std::filesystem::is_regular_file(assetRoot / entry.sourcePath, ec))
                    return entry.sourcePath;
            }
            if (!entry.importedPath.empty())
                return entry.importedPath;
            return entry.sourcePath;
        }

        bool validateTexturePayloadForPack(std::string_view              cookedPath,
                                           std::string_view              logicalPath,
                                           const std::vector<std::byte>& data)
        {
            VTexture texture {};
            auto     loadResult = loadTextureFromMemory(data, texture);
            if (!loadResult)
            {
                std::cerr << "Texture payload is not in current VTexture format.\n"
                          << "  cooked: " << cookedPath << "\n"
                          << "  logical: " << logicalPath << std::endl;
                return false;
            }
            return true;
        }
    } // namespace

    vbase::Result<void, AssetError> importAssetFolder(vbase::StringView                  assetRoot,
                                                      const AssetFolderImportOptions& options)
    {
        namespace fs = std::filesystem;

        const fs::path assetRootPath = fs::absolute(fs::path(std::string(assetRoot))).lexically_normal();
        if (!fs::is_directory(assetRootPath))
        {
            std::cerr << "Failed to resolve asset root: " << assetRootPath.generic_string() << std::endl;
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);
        }

        const std::string assetRootString = assetRootPath.generic_string();
        const std::string registryPath = (assetRootPath / "imported" / "asset_registry.tsv").generic_string();

        VAssetRegistry registry;
        registry.setAssetRootPath(assetRootString);
        registry.setImportedFolderName("imported");
        if (!registry.load(registryPath))
            registry.load((assetRootPath / "imported" / "asset_registry.vreg").generic_string());
        registry.setAssetRootPath(assetRootString);
        registry.setImportedFolderName("imported");

        VAssetImporter assetImporter(registry);
        assetImporter.setOptions(options.importOptions);

        auto importResult = assetImporter.importOrReimportAssetFolder(assetRootString, options.reimport);
        if (!importResult)
            return importResult;

        registry.cleanup();
        return registry.save(registryPath);
    }

    vbase::Result<size_t, AssetError> packAssetFolderToVpk(vbase::StringView assetRoot,
                                                           vbase::StringView outVpk,
                                                           const VpkPackOptions& options)
    {
        namespace fs = std::filesystem;

        const fs::path assetRootPath = fs::absolute(fs::path(std::string(assetRoot))).lexically_normal();
        if (!fs::is_directory(assetRootPath))
        {
            std::cerr << "Failed to resolve asset root: " << assetRootPath.generic_string() << std::endl;
            return vbase::Result<size_t, AssetError>::err(AssetError::eNotFound);
        }

        const std::string assetRootString = assetRootPath.generic_string();
        const std::string registryPath = (assetRootPath / "imported" / "asset_registry.tsv").generic_string();

        VAssetRegistry registry;
        registry.setAssetRootPath(assetRootString);
        registry.setImportedFolderName("imported");
        if (!registry.load(registryPath))
        {
            std::cerr << "Missing asset registry: " << registryPath << std::endl;
            return vbase::Result<size_t, AssetError>::err(AssetError::eNotFound);
        }
        registry.setAssetRootPath(assetRootString);
        registry.setImportedFolderName("imported");

        std::vector<VpkWriteItem>       items;
        std::unordered_set<std::string> packedLogicalPaths;
        for (const auto& [uuidStr, entry] : registry.getRegistry())
        {
            const std::string logicalPath = packLogicalPathForEntry(entry);
            if (logicalPath.empty() || !matchesPackFilters(logicalPath, options.includePaths))
                continue;

            const fs::path filePath = assetRootPath / packDataPathForEntry(entry, assetRootPath);
            std::vector<std::byte> data = readBinaryFile(filePath);
            if (data.empty() && !fs::exists(filePath))
            {
                std::cerr << "Missing pack file: " << filePath.generic_string() << " (" << uuidStr << ")"
                          << std::endl;
                continue;
            }
            if (entry.type == VAssetType::eTexture &&
                !validateTexturePayloadForPack(filePath.generic_string(), logicalPath, data))
            {
                return vbase::Result<size_t, AssetError>::err(AssetError::eInvalidFormat);
            }

            VpkWriteItem item;
            item.logicalPath = logicalPath;
            if (!vbase::try_parse_uuid(uuidStr.c_str(), item.uuid))
                item.uuid = vbase::uuid_from_string_key(logicalPath);
            item.type          = entry.type;
            item.bytes         = std::move(data);
            item.allowCompress = true;

            packedLogicalPaths.insert(logicalPath);
            items.push_back(std::move(item));
        }

        for (const auto& entry : fs::recursive_directory_iterator(assetRootPath))
        {
            if (!entry.is_regular_file())
                continue;

            const fs::path& p = entry.path();
            const std::string relPath = fs::relative(p, assetRootPath).generic_string();
            if (relPath.empty() || relPath.rfind("imported/", 0) == 0 || relPath == "imported")
                continue;
            if (p.extension() == ".vimport" || (relPath.size() >= 4 && relPath.substr(relPath.size() - 4) == ".vpk"))
                continue;
            if (!isRuntimeRawAssetPath(relPath) || packedLogicalPaths.contains(relPath) ||
                !matchesPackFilters(relPath, options.includePaths))
                continue;

            std::vector<std::byte> data = readBinaryFile(p);
            if (data.empty() && !fs::exists(p))
            {
                std::cerr << "Missing raw file: " << p.generic_string() << std::endl;
                continue;
            }

            VpkWriteItem item;
            item.logicalPath   = relPath;
            item.uuid          = vbase::uuid_from_string_key(relPath);
            item.type          = inferRuntimeRawAssetType(relPath);
            item.bytes         = std::move(data);
            item.allowCompress = true;

            packedLogicalPaths.insert(relPath);
            items.push_back(std::move(item));
        }

        if (items.empty())
        {
            std::cerr << "No packable assets found under: " << assetRootString << std::endl;
            return vbase::Result<size_t, AssetError>::err(AssetError::eNotFound);
        }

        auto writeResult = writeVpk(outVpk, items, options.zstdLevel);
        if (!writeResult)
            return vbase::Result<size_t, AssetError>::err(writeResult.error());

        return vbase::Result<size_t, AssetError>::ok(items.size());
    }
} // namespace vasset
