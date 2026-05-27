#include <vasset/vasset_import.hpp>
#include <vfilesystem/vfs/virtual_filesystem.hpp>

#include <filesystem>
#include <iostream>

using namespace vasset;

int main(int argc, char** argv)
{
    constexpr const char* MODEL_PATH = "models/DamagedHelmet/DamagedHelmet.gltf";
    constexpr const char* TEXTURE_PATH = "textures/awesomeface.png";
    constexpr const char* RES_MODEL_PATH = "res://models/DamagedHelmet/DamagedHelmet.gltf";
    constexpr const char* RES_MESH_PATH =
        "res://models/DamagedHelmet/DamagedHelmet.gltf#mesh/0_node_damagedHelmet_-6514_mesh_0_mesh_helmet_LP_13930damagedHelmet";
    constexpr const char* RES_TEXTURE_PATH = "res://textures/awesomeface.png";

    const std::filesystem::path assetRoot = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("resources");
    const std::filesystem::path outVpk    = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("out.vpk");
    const std::filesystem::path registryPath = assetRoot / "imported" / "asset_registry.tsv";

    VAssetRegistry registry;
    registry.setAssetRootPath(assetRoot.generic_string());
    registry.setImportedFolderName("imported");
    registry.load(registryPath.generic_string());
    registry.setAssetRootPath(assetRoot.generic_string());
    registry.setImportedFolderName("imported");

    VAssetImporter importer(registry);
    for (const char* assetPath : {MODEL_PATH, TEXTURE_PATH})
    {
        const std::filesystem::path fullPath = assetRoot / assetPath;
        auto importResult = importer.importOrReimportAsset(fullPath.generic_string(), false);
        if (!importResult)
        {
            std::cerr << "Failed to import asset: " << fullPath.generic_string() << std::endl;
            return 1;
        }
    }

    registry.cleanup();
    auto saveResult = registry.save(registryPath.generic_string());
    if (!saveResult)
        return 1;

    VpkPackOptions packOptions {};
    packOptions.includePaths = {"models/DamagedHelmet", "textures/awesomeface.png"};
    auto packResult = packAssetFolderToVpk(assetRoot.generic_string(), outVpk.generic_string(), packOptions);
    if (!packResult)
    {
        std::cerr << "Failed to pack VPK: " << outVpk.generic_string() << std::endl;
        return 1;
    }
    std::cout << "Packed VPK: " << outVpk.generic_string() << " (" << packResult.value() << " entries)" << std::endl;

    auto vpkFS = std::make_shared<VpkFileSystem>(outVpk.generic_string());
    vpkFS->openPackage();

    VUUIDResolver resolver {};
    resolver.setScheme("res");
    resolver.loadFromVPK(vpkFS->getVpk());

    vfilesystem::VirtualFileSystem vfs {};
    vfs.mount(vpkFS, "res");

    bool exists = vfs.exists(RES_MODEL_PATH);
    std::cout << RES_MODEL_PATH << " Exist? " << exists << std::endl;

    if (!exists)
    {
        std::cerr << "File not found in VPK: " << RES_MODEL_PATH << std::endl;
        return 1;
    }

    exists = vfs.exists(RES_MESH_PATH);
    std::cout << RES_MESH_PATH << " Exist? " << exists << std::endl;

    if (!exists)
    {
        std::cerr << "File not found in VPK: " << RES_MESH_PATH << std::endl;
        return 1;
    }

    auto r = vfs.open(RES_MESH_PATH, vfilesystem::FileMode::eRead);
    if (!r)
    {
        std::cerr << "Failed to open file in VPK: " << RES_MESH_PATH << std::endl;
        return 1;
    }

    auto vpkMemFile = std::move(r.value());

    VMesh mesh {};
    auto  res = loadMeshFromMemory(vpkMemFile->readAllBytes(), mesh);

    if (!res)
    {
        std::cerr << "Failed to load mesh from VPK memory" << std::endl;
        return 1;
    }

    std::cout << "Loaded mesh (" << RES_MESH_PATH << ") from VPK: " << mesh.name << " with "
              << mesh.vertexCount << " vertices. Material count: " << mesh.materials.size() << std::endl;

    for (const auto& material : mesh.materials)
    {
        std::cout << "  Material: " << material.name << std::endl;

        auto baseColorTexRef = material.core.pbrMR.baseColorTexture;
        if (baseColorTexRef.uuid.valid())
        {
            std::string baseColorTexPath;
            if (resolver.resolve(baseColorTexRef.uuid, baseColorTexPath))
            {
                std::cout << "    Base Color Texture: " << baseColorTexPath << std::endl;
                // Try load the texture from VPK
                auto r = vfs.open(baseColorTexPath, vfilesystem::FileMode::eRead);
                if (!r)
                {
                    std::cerr << "Failed to open base color texture in VPK: " << baseColorTexPath << std::endl;
                }
                else
                {
                    auto     vpkTexFile = std::move(r.value());
                    VTexture texture {};
                    auto     res = loadTextureFromMemory(vpkTexFile->readAllBytes(), texture);
                    if (res)
                    {
                        std::cout << "    Loaded base color texture from VPK: " << baseColorTexPath << " ("
                                  << texture.width << "x" << texture.height << ")" << std::endl;
                    }
                    else
                    {
                        std::cerr << "Failed to load base color texture from VPK memory: " << baseColorTexPath
                                  << std::endl;
                    }
                }
            }
            else
            {
                std::cout << "    Base Color Texture UUID not found in resolver: "
                          << vbase::to_string(baseColorTexRef.uuid) << std::endl;
            }
        }
    }

    exists = vfs.exists(RES_TEXTURE_PATH);

    if (!exists)
    {
        std::cerr << "File not found in VPK: " << RES_TEXTURE_PATH << std::endl;
        return 1;
    }

    std::cout << RES_TEXTURE_PATH << " Exist? " << exists << std::endl;

    r = vfs.open(RES_TEXTURE_PATH, vfilesystem::FileMode::eRead);
    if (!r)
    {
        std::cerr << "Failed to open texture file in VPK: " << RES_TEXTURE_PATH << std::endl;
        return 1;
    }

    vpkMemFile = std::move(r.value());

    VTexture texture {};
    res = loadTextureFromMemory(vpkMemFile->readAllBytes(), texture);

    if (!res)
    {
        std::cerr << "Failed to load texture from VPK memory" << std::endl;
        return 1;
    }

    std::cout << "Loaded texture (" << RES_TEXTURE_PATH << ") from VPK: " << " (" << texture.width << "x"
              << texture.height << ")" << std::endl;

    return 0;
}
