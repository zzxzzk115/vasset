#include "vasset/vmaterial.hpp"

#include <filesystem>
#include <fstream>

namespace vasset
{
    bool saveMaterial(const VMaterial& material, vbase::StringView filePath)
    {
        // Binary writing
        std::filesystem::path path(filePath);
        if (path.has_parent_path() && !std::filesystem::exists(path.parent_path()))
            std::filesystem::create_directories(path.parent_path());
        std::ofstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        // 16 bytes for magic number
        const char magic[16] = "VMATERIAL\0";
        file.write(magic, sizeof(magic));

        // 4 bytes for UUID
        file.write(reinterpret_cast<const char*>(&material.uuid), sizeof(material.uuid));

        // 4 bytes for material type
        file.write(reinterpret_cast<const char*>(&material.type), sizeof(material.type));

        // Serialize PBRMetallicRoughness properties
        // Base Color
        file.write(reinterpret_cast<const char*>(&material.pbrMR.baseColor), sizeof(material.pbrMR.baseColor));

        // Alpha Cutoff
        file.write(reinterpret_cast<const char*>(&material.pbrMR.alphaCutoff), sizeof(material.pbrMR.alphaCutoff));

        // Alpha Mode
        file.write(reinterpret_cast<const char*>(&material.pbrMR.alphaMode), sizeof(material.pbrMR.alphaMode));

        // Opacity
        file.write(reinterpret_cast<const char*>(&material.pbrMR.opacity), sizeof(material.pbrMR.opacity));

        // Blend Mode
        file.write(reinterpret_cast<const char*>(&material.pbrMR.blendMode), sizeof(material.pbrMR.blendMode));

        // Metallic Factor
        file.write(reinterpret_cast<const char*>(&material.pbrMR.metallicFactor),
                   sizeof(material.pbrMR.metallicFactor));

        // Roughness Factor
        file.write(reinterpret_cast<const char*>(&material.pbrMR.roughnessFactor),
                   sizeof(material.pbrMR.roughnessFactor));

        // Emissive Color Intensity
        file.write(reinterpret_cast<const char*>(&material.pbrMR.emissiveColorIntensity),
                   sizeof(material.pbrMR.emissiveColorIntensity));

        // Ambient Color
        file.write(reinterpret_cast<const char*>(&material.pbrMR.ambientColor), sizeof(material.pbrMR.ambientColor));

        // IOR
        file.write(reinterpret_cast<const char*>(&material.pbrMR.ior), sizeof(material.pbrMR.ior));

        // Double Sided
        file.write(reinterpret_cast<const char*>(&material.pbrMR.doubleSided), sizeof(material.pbrMR.doubleSided));

        // 4 bytes for name length
        uint32_t nameLength = static_cast<uint32_t>(material.name.size());
        file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));

        // Name string
        file.write(material.name.c_str(), nameLength);

        // Texture Refs
        auto writeTextureRef = [&](const VTextureRef& texRef) {
            file.write(reinterpret_cast<const char*>(&texRef.uuid), sizeof(texRef.uuid));
        };

        writeTextureRef(material.pbrMR.baseColorTexture);
        writeTextureRef(material.pbrMR.alphaTexture);
        writeTextureRef(material.pbrMR.metallicTexture);
        writeTextureRef(material.pbrMR.roughnessTexture);
        writeTextureRef(material.pbrMR.specularTexture);
        writeTextureRef(material.pbrMR.normalTexture);
        writeTextureRef(material.pbrMR.ambientOcclusionTexture);
        writeTextureRef(material.pbrMR.emissiveTexture);
        writeTextureRef(material.pbrMR.metallicRoughnessTexture);

        file.close();

        return true;
    }

    bool loadMaterial(vbase::StringView filePath, VMaterial& outMaterial)
    {
        // Binary reading
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        // 16 bytes for magic number
        char magic[16];
        file.read(magic, sizeof(magic));
        if (std::string(magic) != "VMATERIAL")
            return false;

        // 4 bytes for UUID
        file.read(reinterpret_cast<char*>(&outMaterial.uuid), sizeof(outMaterial.uuid));

        // 4 bytes for material type
        file.read(reinterpret_cast<char*>(&outMaterial.type), sizeof(outMaterial.type));

        // Deserialize PBRMetallicRoughness properties
        // Base Color
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.baseColor), sizeof(outMaterial.pbrMR.baseColor));

        // Alpha Cutoff
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.alphaCutoff), sizeof(outMaterial.pbrMR.alphaCutoff));

        // Alpha Mode
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.alphaMode), sizeof(outMaterial.pbrMR.alphaMode));

        // Opacity
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.opacity), sizeof(outMaterial.pbrMR.opacity));

        // Blend Mode
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.blendMode), sizeof(outMaterial.pbrMR.blendMode));

        // Metallic Factor
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.metallicFactor), sizeof(outMaterial.pbrMR.metallicFactor));

        // Roughness Factor
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.roughnessFactor),
                  sizeof(outMaterial.pbrMR.roughnessFactor));

        // Emissive Color Intensity
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.emissiveColorIntensity),
                  sizeof(outMaterial.pbrMR.emissiveColorIntensity));

        // Ambient Color
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.ambientColor), sizeof(outMaterial.pbrMR.ambientColor));

        // IOR
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.ior), sizeof(outMaterial.pbrMR.ior));

        // Double Sided
        file.read(reinterpret_cast<char*>(&outMaterial.pbrMR.doubleSided), sizeof(outMaterial.pbrMR.doubleSided));

        // 4 bytes for name length
        uint32_t nameLength = 0;
        file.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));

        // Name string
        outMaterial.name.resize(nameLength);
        file.read(&outMaterial.name[0], nameLength);

        // Texture Refs
        auto readTextureRef = [&](VTextureRef& texRef) {
            file.read(reinterpret_cast<char*>(&texRef.uuid), sizeof(texRef.uuid));
        };

        readTextureRef(outMaterial.pbrMR.baseColorTexture);
        readTextureRef(outMaterial.pbrMR.alphaTexture);
        readTextureRef(outMaterial.pbrMR.metallicTexture);
        readTextureRef(outMaterial.pbrMR.roughnessTexture);
        readTextureRef(outMaterial.pbrMR.specularTexture);
        readTextureRef(outMaterial.pbrMR.normalTexture);
        readTextureRef(outMaterial.pbrMR.ambientOcclusionTexture);
        readTextureRef(outMaterial.pbrMR.emissiveTexture);
        readTextureRef(outMaterial.pbrMR.metallicRoughnessTexture);

        file.close();

        return true;
    }
} // namespace vasset