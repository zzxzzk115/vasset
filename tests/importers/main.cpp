#include <vasset/vasset.hpp>

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
            std::string meshPath = loadedRegistry.getAssetRootPath() + "/" + entry.path;
            VMesh       mesh {};
            if (loadMesh(meshPath, mesh))
            {
                std::cout << "Loaded mesh: " << meshPath << " (" << mesh.name << ") with " << mesh.vertexCount
                          << " vertices." << std::endl;

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
                }
            }
        }
    }

    return 0;
}