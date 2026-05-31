#include <vasset/vasset_import.hpp>

#include <filesystem>
#include <iostream>

using namespace vasset;

int main()
{
    VAssetRegistry registry {};
    registry.setAssetRootPath("resources");
    registry.setImportedFolderName("imported");
    if (!std::filesystem::exists("resources/imported"))
    {
        std::filesystem::create_directory("resources/imported");
    }

    if (std::filesystem::exists("resources/imported/asset_registry.vreg"))
    {
        registry.load("resources/imported/asset_registry.vreg");
        std::cout << "Loaded existing asset registry with " << registry.getRegistry().size() << " entries."
                  << std::endl;
    }

    VAssetImporter assetImporter {registry};
    assetImporter.importOrReimportAssetFolder("resources");

    std::string spzPath = "resources/splats/hornedlizard.spz";
    if (!std::filesystem::exists(spzPath))
    {
        spzPath = "external/vasset/resources/splats/hornedlizard.spz";
    }

    auto spzImport = assetImporter.importOrReimportAsset(spzPath, true);
    if (!spzImport)
    {
        std::cerr << "Failed to import SPZ file: " << spzPath << std::endl;
        return 1;
    }

    registry.save("resources/imported/asset_registry.vreg");

    VAssetRegistry loadedRegistry {};
    loadedRegistry.load("resources/imported/asset_registry.vreg");

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
            std::string meshPath = loadedRegistry.getAssetRootPath() + "/" + entry.importedPath;
            VMesh       mesh {};
            if (loadMesh(meshPath, mesh))
            {
                std::cout << "Loaded mesh: " << meshPath << " (" << mesh.name << ") with " << mesh.vertexCount
                          << " vertices." << std::endl;
                if (!mesh.hasLocalBounds)
                {
                    std::cerr << "Imported mesh is missing local bounds: " << meshPath << std::endl;
                    return 1;
                }

                for (const auto& subMesh : mesh.subMeshes)
                {
                    // Meshlets info
                    if (!subMesh.meshletGroup.meshlets.empty())
                    {
                        std::cout << "  Meshlets: " << subMesh.meshletGroup.meshlets.size() << " meshlets."
                                  << std::endl;
                        for (size_t j = 0; j < subMesh.meshletGroup.meshlets.size(); ++j)
                        {
                            const auto& meshlet = subMesh.meshletGroup.meshlets[j];
                            std::cout << "    Meshlet " << j << ": " << meshlet.vertexCount << " vertices, "
                                      << meshlet.triangleCount << " triangles, material index " << meshlet.materialIndex
                                      << ", center(" << meshlet.center.x << ", " << meshlet.center.y << ", "
                                      << meshlet.center.z << "), radius " << meshlet.radius << std::endl;
                        }
                    }

                    // Material info
                    if (subMesh.materialIndex >= 0 && subMesh.materialIndex < static_cast<int>(reg.size()))
                    {
                        const auto& material = mesh.materials[subMesh.materialIndex];
                        std::cout << "  Material: " << material.name << std::endl;
                    }
                }
            }
        }
        else if (entry.type == VAssetType::eGaussianSplat)
        {
            std::string    splatPath = loadedRegistry.getAssetRootPath() + "/" + entry.importedPath;
            VGaussianSplat splat {};
            if (loadGaussianSplat(splatPath, splat))
            {
                std::cout << "Loaded gaussian splat: " << splatPath << " (" << splat.name << ") with "
                          << splat.numPoints << " points, shDegree=" << splat.shDegree
                          << ", antialiased=" << (splat.antialiased ? "true" : "false") << std::endl;
            }
        }
    }

    return 0;
}
