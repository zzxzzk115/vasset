#include <vasset/vasset_import.hpp>
#include <vasset/vasset_registry.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_set>

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

    static std::vector<std::byte> readBinaryFile(const std::filesystem::path& filePath)
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
} // namespace

static int cmd_import(int argc, char** argv)
{
    if (argc < 1)
    {
        std::cout << "Usage: vasset-cli import <asset-root>" << std::endl;
        return 1;
    }

    std::string assetRootPath      = argv[1];
    std::string outputRegistryFile = assetRootPath + "/imported/asset_registry.tsv";

    // Setup registry
    VAssetRegistry registry;
    registry.setAssetRootPath(assetRootPath);
    registry.setImportedFolderName("imported");

    if (!registry.load(outputRegistryFile))
        registry.load(assetRootPath + "/imported/asset_registry.vreg");

    // Setup importer
    VAssetImporter assetImporter(registry);

    // Import all assets
    auto ir = assetImporter.importOrReimportAssetFolder(assetRootPath);
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
                  << std::endl;
        return 1;
    }

    std::string assetRoot = argv[1];
    std::string outVpk    = argv[2];

    int zstdLevel = 6;
    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--zstd" && i + 1 < argc)
        {
            zstdLevel = argv[i + 1][0] - '0';
            std::cout << "Using zstd compression level: " << zstdLevel << std::endl;
            ++i;
        }
    }

    namespace fs = std::filesystem;

    VAssetRegistry registry;
    registry.setAssetRootPath(assetRoot);
    registry.setImportedFolderName("imported");

    const std::string registryPath = (fs::path(assetRoot) / "imported" / "asset_registry.tsv").generic_string();
    if (fs::exists(registryPath) && registry.load(registryPath))
    {
        std::vector<VpkWriteItem> items;
        for (const auto& [uuidStr, entry] : registry.getRegistry())
        {
            const std::string logicalPath = packLogicalPathForEntry(entry);
            if (logicalPath.empty())
                continue;

            const std::string      filePath = (fs::path(assetRoot) / packDataPathForEntry(entry)).generic_string();
            std::vector<std::byte> data     = readBinaryFile(filePath);
            if (data.empty() && !fs::exists(filePath))
            {
                std::cerr << "Missing pack file: " << filePath << " (" << uuidStr << ")" << std::endl;
                continue;
            }

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

        // Read cooked bytes from output path
        const std::string      outPath = (fs::path(assetRoot) / vi.value().output).generic_string();
        std::vector<std::byte> data    = readBinaryFile(outPath);
        if (data.empty() && !fs::exists(outPath))
        {
            std::cerr << "Missing cooked file: " << outPath << " (from " << p.generic_string() << ")" << std::endl;
            continue;
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

        if (importedSourcePaths.contains(relPath))
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

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout <<
            R"(Usage:

    vasset-cli import <asset-root>
    vasset-cli pack <asset-root> <out.vpk> [--zstd N]
)" << std::endl;
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "import")
        return cmd_import(argc - 1, argv + 1);
    if (cmd == "pack")
        return cmd_pack(argc - 1, argv + 1);

    // Backward compatible: treat as legacy import usage (no explicit command)
    return cmd_import(argc, argv);
}
