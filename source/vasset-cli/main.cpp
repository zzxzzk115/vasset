#include <vasset/vasset.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace vasset;

static int cmd_import(int argc, char** argv)
{
    if (argc < 1)
    {
        std::cout << "Usage: vasset-cli import <asset-root>" << std::endl;
        return 1;
    }

    std::string assetRootPath      = argv[1];
    std::string outputRegistryFile = assetRootPath + "/imported/asset_registry.vreg";

    // Setup registry
    VAssetRegistry registry;
    registry.setAssetRootPath(assetRootPath);
    registry.setImportedFolderName("imported");

    registry.load(outputRegistryFile);

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
    std::cout << "✅ Import completed. Registry saved to: " << outputRegistryFile << std::endl;
    return 0;
}

static int cmd_pack(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "Usage: vasset-cli pack <asset-root> <out.vpk>\n"
                     "  - asset-root: root folder that contains source assets and .vimport files\n"
                     "  - out.vpk: output package\n"
                     "Optional:\n"
                     "  --zstd <level>\n"
                  << std::endl;
        return 1;
    }

    std::string assetRoot = argv[1];
    std::string outVpk    = argv[2];

    int zstdLevel = 3;
    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--zstd" && i + 1 < argc)
        {
            zstdLevel = argv[i + 1][0] - '0';
            ++i;
        }
    }

    // Pack rule (pure pipeline):
    // - Scan for *.vimport under assetRoot
    // - Key:   VImport.source (logical path, e.g. "sprites/a.png")
    // - Value: bytes of VImport.output (cooked vxxx)
    //
    // This keeps runtime addressing stable by source path, while allowing editor remap at development time.

    namespace fs = std::filesystem;

    std::vector<VpkWriteItem> items;
    size_t                    importCount = 0;

    for (const auto& entry : fs::recursive_directory_iterator(fs::path(assetRoot)))
    {
        if (!entry.is_regular_file())
            continue;

        const fs::path& p = entry.path();
        if (p.extension() != ".vimport")
            continue;

        ++importCount;

        auto vi = loadVImport(p.string());
        if (!vi)
        {
            std::cerr << "Failed to read .vimport: " << p.string() << std::endl;
            continue;
        }

        // Read cooked bytes from output path
        const std::string& outPath = fs::path(assetRoot) / vi.value().output;

        std::ifstream f(outPath, std::ios::binary);
        if (!f)
        {
            std::cerr << "Missing cooked file: " << outPath << " (from " << p.string() << ")" << std::endl;
            continue;
        }

        f.seekg(0, std::ios::end);
        const size_t sz = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);

        std::vector<std::byte> data;
        data.resize(sz);
        if (sz)
            f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(sz));

        VpkWriteItem it;
        it.logicalPath   = vi.value().source;
        it.bytes         = std::move(data);
        it.allowCompress = true;
        items.push_back(std::move(it));
    }

    if (items.empty())
    {
        std::cerr << "No .vimport found under: " << assetRoot << std::endl;
        return 1;
    }

    auto wr = writeVpk(outVpk, items, zstdLevel);
    if (!wr)
    {
        std::cerr << "Failed to write vpk: " << outVpk << std::endl;
        return 1;
    }

    std::cout << "✅ Packed VPK: " << outVpk << " (" << items.size() << " entries, scanned " << importCount
              << " .vimport)" << std::endl;
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
