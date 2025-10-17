#include <vasset/vasset.hpp>

#include <filesystem>
#include <iostream>

using namespace vasset;

int main()
{
    VAssetRegistry registry {};
    registry.setImportedFolder("imported");
    if (!std::filesystem::exists("imported"))
    {
        std::filesystem::create_directory("imported");
    }

    if (std::filesystem::exists("imported/asset_registry.json"))
    {
        registry.load("imported/asset_registry.json");
        std::cout << "Loaded existing asset registry with " << registry.getRegistry().size() << " entries."
                  << std::endl;
    }

    VAssetImporter assetImporter {registry};
    assetImporter.importAssetFolder("resources");

    registry.save("imported/asset_registry.json");

    VAssetRegistry loadedRegistry {};
    loadedRegistry.load("imported/asset_registry.json");

    std::cout << "Loaded registry:" << std::endl;
    const auto& reg = loadedRegistry.getRegistry();
    for (const auto& [uuid, entry] : reg)
    {
        std::cout << uuid << " -> " << entry.toString() << std::endl;
    }

    // Try load VMeshes
    for (const auto& [uuid, entry] : reg)
    {
        if (entry.type == VAssetType::eMesh)
        {
            std::string meshPath = "imported/" + entry.path;
            VMesh       mesh {};
            if (loadMesh(meshPath, mesh))
            {
                std::cout << "Loaded mesh: " << meshPath << " (" << mesh.name << ") with " << mesh.vertexCount << " vertices." << std::endl;
            }
        }
    }

    return 0;
}