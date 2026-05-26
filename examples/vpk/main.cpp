#include <vasset/vasset_runtime.hpp>
#include <vfilesystem/vfs/virtual_filesystem.hpp>

#include <iostream>

using namespace vasset;

int main()
{
    constexpr const char* MODEL_PATH = "res://models/DamagedHelmet/DamagedHelmet.gltf";
    constexpr const char* MESH_PATH =
        "res://models/DamagedHelmet/DamagedHelmet.gltf#mesh/0_node_damagedHelmet_-6514_mesh_0_mesh_helmet_LP_13930damagedHelmet";
    constexpr const char* TEXTURE_PATH = "res://textures/environment_maps/citrus_orchard_puresky_1k.hdr";

    auto vpkFS = std::make_shared<VpkFileSystem>("out.vpk");
    vpkFS->openPackage();

    VUUIDResolver resolver {};
    resolver.setScheme("res");
    resolver.loadFromVPK(vpkFS->getVpk());

    vfilesystem::VirtualFileSystem vfs {};
    vfs.mount(vpkFS, "res");

    bool exists = vfs.exists(MODEL_PATH);
    std::cout << MODEL_PATH << " Exist? " << exists << std::endl;

    if (!exists)
    {
        std::cerr << "File not found in VPK: " << MODEL_PATH << std::endl;
        return 1;
    }

    exists = vfs.exists(MESH_PATH);
    std::cout << MESH_PATH << " Exist? " << exists << std::endl;

    if (!exists)
    {
        std::cerr << "File not found in VPK: " << MESH_PATH << std::endl;
        return 1;
    }

    auto r = vfs.open(MESH_PATH, vfilesystem::FileMode::eRead);
    if (!r)
    {
        std::cerr << "Failed to open file in VPK: " << MESH_PATH << std::endl;
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

    std::cout << "Loaded mesh (" << MESH_PATH << ") from VPK: " << mesh.name << " with "
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

    exists = vfs.exists(TEXTURE_PATH);

    if (!exists)
    {
        std::cerr << "File not found in VPK: " << TEXTURE_PATH << std::endl;
        return 1;
    }

    std::cout << TEXTURE_PATH << " Exist? " << exists << std::endl;

    r = vfs.open(TEXTURE_PATH, vfilesystem::FileMode::eRead);
    if (!r)
    {
        std::cerr << "Failed to open texture file in VPK: " << TEXTURE_PATH << std::endl;
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

    std::cout << "Loaded texture (" << TEXTURE_PATH << ") from VPK: " << " (" << texture.width << "x"
              << texture.height << ")" << std::endl;

    return 0;
}
