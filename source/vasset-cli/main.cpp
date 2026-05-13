#include <vasset/vasset_import.hpp>
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
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace vasset;

namespace
{
    std::string packLogicalPathForEntry(const VAssetRegistry::AssetEntry& entry)
    {
        if (!entry.sourcePath.empty())
            return entry.sourcePath;
        return entry.importedPath;
    }

    std::string packDataPathForEntry(const VAssetRegistry::AssetEntry& entry)
    {
        if (!entry.importedPath.empty())
            return entry.importedPath;
        return entry.sourcePath;
    }

    std::vector<std::byte> readBinaryFile(const std::filesystem::path& filePath)
    {
        std::ifstream f(filePath, std::ios::binary);
        if (!f)
            return {};

        f.seekg(0, std::ios::end);
        const size_t sz = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::vector<std::byte> data;
        data.resize(sz);
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
        for (const auto& includePath : includePaths)
        {
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

    bool validateTexturePayloadForPack(std::string_view              cookedPath,
                                       std::string_view              logicalPath,
                                       const std::vector<std::byte>& data)
    {
        VTexture texture {};
        auto     loadResult = loadTextureFromMemory(data, texture);
        if (!loadResult)
        {
            std::cerr << "Texture payload is not in current VTexture format. "
                         "Please run vasset-cli import before pack.\n"
                      << "  cooked: " << cookedPath << "\n"
                      << "  logical: " << logicalPath << std::endl;
            return false;
        }
        return true;
    }

    struct VpkValidationReport
    {
        size_t                   checkedIndexEntries {0};
        size_t                   checkedRegistryEntries {0};
        size_t                   checkedTsvEntries {0};
        size_t                   checkedFilesystemEntries {0};
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    void printIssueList(const char* title, const std::vector<std::string>& issues, size_t maxLines)
    {
        if (issues.empty())
            return;

        std::cout << title << " (" << issues.size() << "):" << std::endl;
        const size_t count = std::min(issues.size(), maxLines);
        for (size_t i = 0; i < count; ++i)
            std::cout << "  - " << issues[i] << std::endl;
        if (issues.size() > maxLines)
            std::cout << "  ... " << (issues.size() - maxLines) << " more" << std::endl;
    }

    bool fileExists(const std::filesystem::path& p)
    {
        std::error_code ec;
        const bool      exists = std::filesystem::exists(p, ec);
        return !ec && exists;
    }

    std::filesystem::path normalizePath(const std::filesystem::path& p)
    {
        std::error_code ec;
        auto            canonical = std::filesystem::weakly_canonical(p, ec);
        if (!ec)
            return canonical;
        return p.lexically_normal();
    }

    bool directoryExists(const std::filesystem::path& p)
    {
        std::error_code ec;
        if (!std::filesystem::exists(p, ec) || ec)
            return false;
        std::error_code ec2;
        return std::filesystem::is_directory(p, ec2) && !ec2;
    }

    bool resolveAssetRootPath(std::string_view rawAssetRoot, std::filesystem::path& outResolved, std::string& outHint)
    {
        const std::filesystem::path inputPath {std::string(rawAssetRoot)};
        if (directoryExists(inputPath))
        {
            outResolved = normalizePath(inputPath);
            return true;
        }

        if (!inputPath.is_relative())
        {
            outHint = "asset root does not exist: " + inputPath.generic_string();
            return false;
        }

        std::error_code ec;
        auto            cwd = std::filesystem::current_path(ec);
        if (ec)
        {
            outHint = "failed to get current directory for resolving asset root";
            return false;
        }

        // Try cwd first, then walk parents to support `xmake run` target directories.
        for (std::filesystem::path cursor = cwd; !cursor.empty();)
        {
            const auto probe = cursor / inputPath;
            if (directoryExists(probe))
            {
                outResolved = normalizePath(probe);
                return true;
            }

            if (cursor == cursor.root_path())
                break;
            cursor = cursor.parent_path();
        }

        outHint = "asset root not found from cwd or parent directories: " + std::string(rawAssetRoot) +
                  " (cwd=" + cwd.generic_string() + ")";
        return false;
    }

    bool tryInferAssetRoot(std::filesystem::path vpkPath, std::filesystem::path& outAssetRoot)
    {
        std::unordered_set<std::string>    dedup;
        std::vector<std::filesystem::path> candidates;

        const auto pushCandidate = [&](const std::filesystem::path& candidate) {
            if (candidate.empty())
                return;
            const auto normalized = normalizePath(candidate).generic_string();
            if (dedup.insert(normalized).second)
                candidates.push_back(normalizePath(candidate));
        };

        pushCandidate(std::filesystem::current_path() / "resources");
        for (auto p = vpkPath.parent_path(); !p.empty();)
        {
            pushCandidate(p);
            pushCandidate(p / "resources");
            if (p == p.root_path())
                break;
            p = p.parent_path();
        }

        for (const auto& candidate : candidates)
        {
            if (fileExists(candidate / "imported" / "asset_registry.tsv"))
            {
                outAssetRoot = candidate;
                return true;
            }
        }
        return false;
    }

    VpkValidationReport validateVpkPackage(const std::filesystem::path& vpkPath,
                                           const std::filesystem::path& assetRootOpt,
                                           const std::filesystem::path& registryPathOpt)
    {
        VpkValidationReport report;

        auto openResult = openVpk(vpkPath.generic_string());
        if (!openResult)
        {
            report.errors.push_back("Failed to open VPK: " + vpkPath.generic_string());
            return report;
        }

        const auto& pkg = openResult.value();

        std::unordered_map<std::string, uint64_t>    indexPathToRawSize;
        std::unordered_map<std::string, std::string> registryPathToUuid;
        std::unordered_map<std::string, std::string> registryUuidToPath;

        for (size_t i = 0; i < pkg.entries.size(); ++i)
        {
            const auto& entry = pkg.entries[i];
            if (entry.pathOffset + entry.pathSize > pkg.stringTable.size())
            {
                report.errors.push_back("Index entry #" + std::to_string(i) + " has invalid path offset/size");
                continue;
            }

            std::string logicalPath(pkg.stringTable.data() + entry.pathOffset, entry.pathSize);
            logicalPath = normalizePackFilterPath(std::move(logicalPath));
            if (logicalPath.empty())
            {
                report.errors.push_back("Index entry #" + std::to_string(i) + " has empty logical path");
                continue;
            }

            if (!indexPathToRawSize.emplace(logicalPath, entry.rawSize).second)
            {
                report.errors.push_back("Duplicate VPK index path: " + logicalPath);
                continue;
            }

            auto readResult = readVpkFile(pkg, vpkPath.generic_string(), logicalPath);
            if (!readResult)
            {
                report.errors.push_back("Failed to read VPK entry payload: " + logicalPath);
                continue;
            }
            if (readResult.value().size() != entry.rawSize)
            {
                report.errors.push_back("Payload size mismatch for path '" + logicalPath + "': expected " +
                                        std::to_string(entry.rawSize) + ", actual " +
                                        std::to_string(readResult.value().size()));
            }

            ++report.checkedIndexEntries;
        }

        for (size_t i = 0; i < pkg.registry.size(); ++i)
        {
            const auto& registryEntry = pkg.registry[i];
            if (registryEntry.pathOffset + registryEntry.pathSize > pkg.stringTable.size())
            {
                report.errors.push_back("Registry entry #" + std::to_string(i) + " has invalid path offset/size");
                continue;
            }

            std::string logicalPath(pkg.stringTable.data() + registryEntry.pathOffset, registryEntry.pathSize);
            logicalPath = normalizePackFilterPath(std::move(logicalPath));
            if (logicalPath.empty())
            {
                report.errors.push_back("Registry entry #" + std::to_string(i) + " has empty logical path");
                continue;
            }

            const std::string uuidStr = vbase::to_string(registryEntry.uuid);

            auto [pathIt, pathInserted] = registryPathToUuid.emplace(logicalPath, uuidStr);
            if (!pathInserted && pathIt->second != uuidStr)
            {
                report.errors.push_back("Registry path maps to multiple UUIDs: " + logicalPath);
                continue;
            }

            auto [uuidIt, uuidInserted] = registryUuidToPath.emplace(uuidStr, logicalPath);
            if (!uuidInserted && uuidIt->second != logicalPath)
            {
                report.errors.push_back("Registry UUID maps to multiple paths: " + uuidStr);
                continue;
            }

            if (!indexPathToRawSize.contains(logicalPath))
            {
                report.errors.push_back("Registry path missing in VPK index: " + logicalPath);
            }

            ++report.checkedRegistryEntries;
        }

        for (const auto& [path, _] : indexPathToRawSize)
        {
            if (!registryPathToUuid.contains(path))
                report.warnings.push_back("Index path is not listed in VPK registry: " + path);
        }

        if (!registryPathOpt.empty())
        {
            VAssetRegistry registry;
            registry.setAssetRootPath(assetRootOpt.generic_string());
            registry.setImportedFolderName("imported");

            auto loadResult = registry.load(registryPathOpt.generic_string());
            if (!loadResult)
            {
                report.errors.push_back("Failed to load TSV registry: " + registryPathOpt.generic_string());
            }
            else
            {
                std::unordered_map<std::string, std::string> tsvPathToUuid;
                for (const auto& [uuidStr, entry] : registry.getRegistry())
                {
                    const std::string expectedPath =
                        normalizePackFilterPath(!entry.sourcePath.empty() ? entry.sourcePath : entry.importedPath);
                    if (expectedPath.empty())
                        continue;

                    auto [it, inserted] = tsvPathToUuid.emplace(expectedPath, uuidStr);
                    if (!inserted && it->second != uuidStr)
                    {
                        report.warnings.push_back("TSV path maps to multiple UUIDs: " + expectedPath);
                    }

                    ++report.checkedTsvEntries;
                }

                for (const auto& [vpkPathKey, vpkUuid] : registryPathToUuid)
                {
                    auto it = tsvPathToUuid.find(vpkPathKey);
                    if (it == tsvPathToUuid.end())
                    {
                        report.errors.push_back("VPK path is not found in TSV: " + vpkPathKey);
                        continue;
                    }
                    if (it->second != vpkUuid)
                    {
                        report.errors.push_back("UUID mismatch for path '" + vpkPathKey + "': TSV=" + it->second +
                                                ", VPK=" + vpkUuid);
                    }
                }
            }
        }
        else
        {
            report.warnings.push_back("TSV validation skipped (no registry path provided or inferred).");
        }

        if (!assetRootOpt.empty())
        {
            for (const auto& [logicalPath, _] : indexPathToRawSize)
            {
                const auto expectedPath = assetRootOpt / std::filesystem::path(logicalPath);
                if (!fileExists(expectedPath))
                {
                    report.errors.push_back("Missing source file for VPK path: " + logicalPath + " -> " +
                                            expectedPath.generic_string());
                }
                ++report.checkedFilesystemEntries;
            }
        }
        else
        {
            report.warnings.push_back("Filesystem validation skipped (no asset root provided or inferred).");
        }

        return report;
    }
} // namespace

static int cmd_import(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: vasset-cli import <asset-root> [--reimport] [--ignore-dir <relative-dir>]" << std::endl;
        return 1;
    }

    std::string assetRootPathArg   = argv[1];
    std::filesystem::path assetRootPathResolved;
    std::string           assetRootResolveHint;
    if (!resolveAssetRootPath(assetRootPathArg, assetRootPathResolved, assetRootResolveHint))
    {
        std::cerr << "Failed to resolve asset root: " << assetRootResolveHint << std::endl;
        return 1;
    }
    std::string assetRootPath      = assetRootPathResolved.generic_string();
    std::string outputRegistryFile = assetRootPath + "/imported/asset_registry.tsv";
    bool                     reimport = false;
    std::vector<std::string> ignoredDirectories;
    for (int i = 2; i < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--reimport")
        {
            reimport = true;
        }
        else if (std::string_view(argv[i]) == "--ignore-dir")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for --ignore-dir" << std::endl;
                return 1;
            }
            ignoredDirectories.push_back(std::filesystem::path(argv[++i]).generic_string());
        }
    }

    // Setup registry
    VAssetRegistry registry;
    registry.setAssetRootPath(assetRootPath);
    registry.setImportedFolderName("imported");

    if (!registry.load(outputRegistryFile))
        registry.load(assetRootPath + "/imported/asset_registry.vreg");
    // Command-line asset root is authoritative for this import run.
    registry.setAssetRootPath(assetRootPath);
    registry.setImportedFolderName("imported");

    // Setup importer
    VAssetImporter assetImporter(registry);
    assetImporter.setOptions({.ignoredDirectories = ignoredDirectories});

    // Import all assets
    auto ir = assetImporter.importOrReimportAssetFolder(assetRootPath, reimport);
    if (!ir)
    {
        std::cerr << "Failed to import assets from folder: " << assetRootPath << std::endl;
        return 1;
    }

    // Cleanup and save registry
    registry.cleanup();

    auto sr = registry.save(outputRegistryFile);
    if (!sr)
    {
        std::cerr << "Failed to save registry: " << outputRegistryFile << std::endl;
        return 1;
    }
    std::cout << "Import completed. Registry saved to: " << outputRegistryFile << std::endl;
    return 0;
}

static int cmd_pack(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "Usage: vasset-cli pack <asset-root> <out.vpk>\n"
                     "  - asset-root: root folder that contains source assets and imported registry\n"
                     "  - out.vpk: output package\n"
                     "Optional:\n"
                     "  --zstd <level>\n"
                     "  --include <logical-path-prefix>\n"
                  << std::endl;
        return 1;
    }

    std::string assetRootArg = argv[1];
    std::filesystem::path assetRootResolved;
    std::string           assetRootResolveHint;
    if (!resolveAssetRootPath(assetRootArg, assetRootResolved, assetRootResolveHint))
    {
        std::cerr << "Failed to resolve asset root: " << assetRootResolveHint << std::endl;
        return 1;
    }
    std::string assetRoot = assetRootResolved.generic_string();
    std::string outVpk    = argv[2];

    int                      zstdLevel = 6;
    std::vector<std::string> includePaths;
    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--zstd" && i + 1 < argc)
        {
            zstdLevel = argv[i + 1][0] - '0';
            std::cout << "Using zstd compression level: " << zstdLevel << std::endl;
            ++i;
        }
        else if (a == "--include" && i + 1 < argc)
        {
            includePaths.push_back(normalizePackFilterPath(argv[i + 1]));
            ++i;
        }
    }

    if (!includePaths.empty())
    {
        std::cout << "Applying pack include filters:" << std::endl;
        for (const auto& includePath : includePaths)
            std::cout << "  - " << includePath << std::endl;
    }

    namespace fs = std::filesystem;

    VAssetRegistry registry;
    registry.setAssetRootPath(assetRoot);
    registry.setImportedFolderName("imported");

    const std::string registryPath = (fs::path(assetRoot) / "imported" / "asset_registry.tsv").generic_string();
    if (fs::exists(registryPath) && registry.load(registryPath))
    {
        registry.setAssetRootPath(assetRoot);
        registry.setImportedFolderName("imported");

        std::vector<VpkWriteItem> items;
        for (const auto& [uuidStr, entry] : registry.getRegistry())
        {
            const std::string logicalPath = packLogicalPathForEntry(entry);
            if (logicalPath.empty())
                continue;
            if (!matchesPackFilters(logicalPath, includePaths))
                continue;

            const std::string      filePath = (fs::path(assetRoot) / packDataPathForEntry(entry)).generic_string();
            std::vector<std::byte> data     = readBinaryFile(filePath);
            if (data.empty() && !fs::exists(filePath))
            {
                std::cerr << "Missing pack file: " << filePath << " (" << uuidStr << ")" << std::endl;
                continue;
            }
            if (entry.type == VAssetType::eTexture && !validateTexturePayloadForPack(filePath, logicalPath, data))
                return 1;

            VpkWriteItem it;
            it.logicalPath = logicalPath;
            if (!vbase::try_parse_uuid(uuidStr.c_str(), it.uuid))
                it.uuid = vbase::uuid_from_string_key(logicalPath);
            it.bytes         = std::move(data);
            it.allowCompress = true;
            items.push_back(std::move(it));
        }

        if (items.empty())
        {
            std::cerr << "No packable assets found under: " << assetRoot << std::endl;
            return 1;
        }

        auto wr = writeVpk(outVpk, items, zstdLevel);
        if (!wr)
        {
            std::cerr << "Failed to write vpk: " << outVpk << std::endl;
            return 1;
        }

        std::cout << "Packed VPK: " << outVpk << " (" << items.size() << " entries)" << std::endl;
        return 0;
    }

    // Pack rule (pure pipeline):
    // - Scan for *.vimport under assetRoot
    // - Key:   VImport.source (logical path, e.g. "sprites/a.png")
    // - Value: bytes of VImport.output (cooked/imported file)

    std::vector<VpkWriteItem>       items;
    std::unordered_set<std::string> importedSourcePaths;
    size_t                          importCount = 0;

    for (const auto& entry : fs::recursive_directory_iterator(fs::path(assetRoot)))
    {
        if (!entry.is_regular_file())
            continue;

        const fs::path& p = entry.path();
        if (p.extension() != ".vimport")
            continue;

        ++importCount;

        auto vi = loadVImport(p.generic_string());
        if (!vi)
        {
            std::cerr << "Failed to read .vimport: " << p.generic_string() << std::endl;
            continue;
        }

        importedSourcePaths.insert(vi.value().source);
        if (!matchesPackFilters(vi.value().source, includePaths))
            continue;

        // Read cooked bytes from output path
        const std::string      outPath = (fs::path(assetRoot) / vi.value().output).generic_string();
        std::vector<std::byte> data    = readBinaryFile(outPath);
        if (data.empty() && !fs::exists(outPath))
        {
            std::cerr << "Missing cooked file: " << outPath << " (from " << p.generic_string() << ")" << std::endl;
            continue;
        }
        if (vi.value().importer == toString(VAssetType::eTexture) &&
            !validateTexturePayloadForPack(outPath, vi.value().source, data))
        {
            return 1;
        }

        VpkWriteItem it;
        it.logicalPath   = vi.value().source;
        it.uuid          = vi.value().uid; // Preserve UUID from VImport
        it.bytes         = std::move(data);
        it.allowCompress = true;

        items.push_back(std::move(it));
    }

    for (const auto& entry : fs::recursive_directory_iterator(fs::path(assetRoot)))
    {
        if (!entry.is_regular_file())
            continue;

        const fs::path& p = entry.path();
        if (p.extension() == ".vimport")
            continue;

        const std::string relPath = fs::relative(p, fs::path(assetRoot)).generic_string();
        if (relPath.empty())
            continue;

        if (relPath.rfind("imported/", 0) == 0 || relPath == "imported")
            continue;

        if (relPath.size() >= 4 && relPath.substr(relPath.size() - 4) == ".vpk")
            continue;

        if (!isRuntimeRawAssetPath(relPath))
            continue;

        if (importedSourcePaths.contains(relPath))
            continue;
        if (!matchesPackFilters(relPath, includePaths))
            continue;

        std::vector<std::byte> data = readBinaryFile(p);
        if (data.empty() && !fs::exists(p))
        {
            std::cerr << "Missing raw file: " << p.generic_string() << std::endl;
            continue;
        }

        VpkWriteItem it;
        it.logicalPath   = relPath;
        it.uuid          = vbase::uuid_from_string_key(relPath);
        it.bytes         = std::move(data);
        it.allowCompress = true;

        items.push_back(std::move(it));
    }

    if (items.empty())
    {
        std::cerr << "No packable assets found under: " << assetRoot << std::endl;
        return 1;
    }

    auto wr = writeVpk(outVpk, items, zstdLevel);
    if (!wr)
    {
        std::cerr << "Failed to write vpk: " << outVpk << std::endl;
        return 1;
    }

    std::cout << "Packed VPK: " << outVpk << " (" << items.size() << " entries, scanned " << importCount << " .vimport)"
              << std::endl;
    return 0;
}

static int cmd_validate_vpk(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: vasset-cli validate-vpk <path/to/resources.vpk>\n"
                     "Optional:\n"
                     "  --asset-root <asset-root>\n"
                     "  --registry <path/to/asset_registry.tsv>\n"
                  << std::endl;
        return 1;
    }

    std::filesystem::path vpkPath(argv[1]);
    std::filesystem::path assetRoot;
    std::filesystem::path registryPath;

    for (int i = 2; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--asset-root")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for --asset-root" << std::endl;
                return 1;
            }
            assetRoot = argv[++i];
        }
        else if (arg == "--registry")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for --registry" << std::endl;
                return 1;
            }
            registryPath = argv[++i];
        }
        else
        {
            std::cerr << "Unknown option: " << arg << std::endl;
            return 1;
        }
    }

    if (assetRoot.empty())
    {
        std::filesystem::path inferredAssetRoot;
        if (tryInferAssetRoot(vpkPath, inferredAssetRoot))
            assetRoot = inferredAssetRoot;
    }

    if (registryPath.empty() && !assetRoot.empty())
    {
        auto candidate = assetRoot / "imported" / "asset_registry.tsv";
        if (fileExists(candidate))
            registryPath = candidate;
    }

    if (!fileExists(vpkPath))
    {
        std::cerr << "VPK file not found: " << vpkPath.generic_string() << std::endl;
        return 1;
    }

    std::cout << "Validating VPK: " << normalizePath(vpkPath).generic_string() << std::endl;
    if (!assetRoot.empty())
        std::cout << "Asset root: " << normalizePath(assetRoot).generic_string() << std::endl;
    else
        std::cout << "Asset root: <not provided>" << std::endl;
    if (!registryPath.empty())
        std::cout << "TSV registry: " << normalizePath(registryPath).generic_string() << std::endl;
    else
        std::cout << "TSV registry: <not provided>" << std::endl;

    auto report = validateVpkPackage(vpkPath, assetRoot, registryPath);

    std::cout << "\nValidation summary:" << std::endl;
    std::cout << "  index entries checked: " << report.checkedIndexEntries << std::endl;
    std::cout << "  registry entries checked: " << report.checkedRegistryEntries << std::endl;
    std::cout << "  tsv entries checked: " << report.checkedTsvEntries << std::endl;
    std::cout << "  filesystem entries checked: " << report.checkedFilesystemEntries << std::endl;

    printIssueList("Warnings", report.warnings, 20);
    printIssueList("Errors", report.errors, 40);

    if (!report.errors.empty())
    {
        std::cerr << "\nVPK validation FAILED." << std::endl;
        return 1;
    }

    std::cout << "\nVPK validation PASSED." << std::endl;
    return 0;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
        {
        std::cout <<
            R"(Usage:

    vasset-cli import <asset-root> [--reimport]
    vasset-cli pack <asset-root> <out.vpk> [--zstd N] [--include logical/path]
    vasset-cli validate-vpk <path/to/resources.vpk> [--asset-root <asset-root>] [--registry <asset_registry.tsv>]
)" << std::endl;
            return 1;
        }

        std::string cmd = argv[1];
        if (cmd == "import")
            return cmd_import(argc - 1, argv + 1);
        if (cmd == "pack")
            return cmd_pack(argc - 1, argv + 1);
        if (cmd == "validate-vpk")
            return cmd_validate_vpk(argc - 1, argv + 1);

        // Backward compatible: treat as legacy import usage (no explicit command)
        return cmd_import(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 3;
    }
    catch (...)
    {
        std::cerr << "Unhandled unknown exception." << std::endl;
        return 3;
    }
}
