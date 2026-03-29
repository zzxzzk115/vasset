#include <vasset/vasset_runtime.hpp>
#include <vfilesystem/vfs/virtual_filesystem.hpp>

#include <iostream>

using namespace vasset;

int main()
{
    auto vpkFS = std::make_shared<VpkFileSystem>("out.vpk");
    vpkFS->openPackage();

    VUUIDResolver resolver {};
    resolver.setScheme("res");
    resolver.loadFromVPK(vpkFS->getVpk());

    vfilesystem::VirtualFileSystem vfs {};
    vfs.mount(vpkFS, "res");

    bool exists = vfs.exists("res://models/DamagedHelmet/DamagedHelmet.gltf");
    std::cout << "res://models/DamagedHelmet/DamagedHelmet.gltf Exist? " << exists << std::endl;

    if (!exists)
    {
        std::cerr << "File not found in VPK: res://models/DamagedHelmet/DamagedHelmet.gltf" << std::endl;
        return 1;
    }

    auto r = vfs.open("res://models/DamagedHelmet/DamagedHelmet.gltf", vfilesystem::FileMode::eRead);
    if (!r)
    {
        std::cerr << "Failed to open file in VPK: res://models/DamagedHelmet/DamagedHelmet.gltf" << std::endl;
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

    std::cout << "Loaded mesh (res://models/DamagedHelmet/DamagedHelmet.gltf) from VPK: " << mesh.name << " with "
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

    exists = vfs.exists("res://textures/awesomeface.png");

    if (!exists)
    {
        std::cerr << "File not found in VPK: res://textures/awesomeface.png" << std::endl;
        return 1;
    }

    std::cout << "res://textures/awesomeface.png Exist? " << exists << std::endl;

    r = vfs.open("res://textures/awesomeface.png", vfilesystem::FileMode::eRead);
    if (!r)
    {
        std::cerr << "Failed to open texture file in VPK: res://textures/awesomeface.png" << std::endl;
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

    std::cout << "Loaded texture (res://textures/awesomeface.png) from VPK: " << " (" << texture.width << "x"
              << texture.height << ")" << std::endl;

    return 0;
}
