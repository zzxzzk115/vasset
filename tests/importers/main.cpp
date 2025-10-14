#include <vasset/vasset.hpp>

#include <filesystem>
#include <iostream>

using namespace vasset;

int main()
{
    VAssetRegistry registry {};
    if (std::filesystem::exists("asset_registry.json"))
    {
        registry.load("asset_registry.json");
        std::cout << "Loaded existing asset registry with " << registry.getRegistry().size() << " entries."
                  << std::endl;
    }

    VAssetImporter assetImporter {registry};
    assetImporter.importAssetFolder("resources");

    registry.save("asset_registry.json");

    VAssetRegistry loadedRegistry {};
    loadedRegistry.load("asset_registry.json");

    std::cout << "Loaded registry:" << std::endl;
    const auto& reg = loadedRegistry.getRegistry();
    for (const auto& [uuid, entry] : reg)
    {
        std::cout << uuid << " -> " << entry.toString() << std::endl;
    }

    return 0;
}