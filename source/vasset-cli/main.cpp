#include <vasset/vasset.hpp>

#include <filesystem>
#include <iostream>

using namespace vasset;

int main(int argc, char** argv)
{
    // Argument parsing
    if (argc < 3)
    {
        std::cout << "Usage: vasset-cli <asset-folder> <imported-folder> [working-directory]" << std::endl;
        return 1;
    }

    std::string assetFolder    = argv[1];
    std::string importedFolder = argv[2];

    std::string workingFolder;
    if (argc > 3)
        workingFolder = argv[3];

    std::string outputRegistryFile = importedFolder + "/asset_registry.json";
    if (!workingFolder.empty())
    {
        assetFolder        = workingFolder + "/" + assetFolder;
        importedFolder     = workingFolder + "/" + importedFolder;
        outputRegistryFile = workingFolder + "/" + outputRegistryFile;
    }

    VAssetRegistry registry {};
    registry.setImportedFolder(importedFolder);
    if (std::filesystem::exists(outputRegistryFile))
    {
        registry.load(outputRegistryFile);
        std::cout << "Loaded existing asset registry with " << registry.getRegistry().size() << " entries."
                  << std::endl;
        const auto& reg = registry.getRegistry();
        for (const auto& [uuid, entry] : reg)
        {
            std::cout << uuid << " -> " << entry.toString() << std::endl;
        }
        return 0;
    }

    VAssetImporter assetImporter {registry};
    if (!assetImporter.importAssetFolder(assetFolder))
    {
        std::cerr << "Failed to import assets from folder: " << assetFolder << std::endl;
        return 1;
    }

    registry.save(outputRegistryFile);

    std::cout << "Saved asset registry to " << outputRegistryFile << std::endl;
    std::cout << "Import completed." << std::endl;
    std::cout << "Total assets in registry: " << registry.getRegistry().size() << std::endl;
    const auto& reg = registry.getRegistry();
    for (const auto& [uuid, entry] : reg)
    {
        std::cout << uuid << " -> " << entry.toString() << std::endl;
    }

    return 0;
}