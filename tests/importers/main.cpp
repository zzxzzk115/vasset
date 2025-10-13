#include <vasset/vasset.hpp>

#include <iostream>

using namespace vasset;

int main()
{
    VTextureImporter importer {};
    VTexture         outTexture {};
    // importer.setOptions({.targetTextureFileFormat = VTextureFileFormat::ePNG});
    importer.importTexture("resources/textures/awesomeface.png", outTexture);
    saveTexture(outTexture, "awesomeface.vtex");

    std::cout << outTexture.toString() << std::endl;
    return 0;
}