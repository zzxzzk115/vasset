#include <vasset/vasset.hpp>
#include <vfilesystem/vfs/virtual_filesystem.hpp>

#include <iostream>

using namespace vasset;

int main()
{
    auto vpkFS = std::make_shared<VpkFileSystem>("out.vpk");
    vpkFS->openPackage();

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

    std::cout << "Loaded mesh (res://models/DamagedHelmet/DamagedHelmet.gltf) from VPK: " << mesh.name << " with " << mesh.vertexCount << " vertices." << std::endl;

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

    std::cout << "Loaded texture (res://textures/awesomeface.png) from VPK: " << " (" << texture.width << "x" << texture.height << ")" << std::endl;

    return 0;
}