#pragma once

#include "vasset/vtexture.hpp"

#include <glm/glm.hpp>

#include <string>

namespace vasset
{
    enum class VMaterialType
    {
        eNone,
        ePBRMetallicRoughness,
    };

    enum class VMaterialAlphaMode
    {
        eOpaque,
        eMask,
        eBlend
    };

    enum class VMaterialBlendMode
    {
        eNone,
        eAlpha,
        eAdditive,
        eMultiply
    };

    struct VMaterialRef
    {
        VUUID uuid;
    };

    struct VMaterialPBRMetallicRoughness
    {
        VTextureRef baseColorTexture;
        glm::vec4   baseColor {1.0f, 1.0f, 1.0f, 1.0f};

        VTextureRef        alphaTexture;
        float              alphaCutoff {0.5f};
        VMaterialAlphaMode alphaMode {VMaterialAlphaMode::eOpaque}; // OPAQUE, MASK, BLEND

        float              opacity {1.0f};
        VMaterialBlendMode blendMode {VMaterialBlendMode::eNone}; // NONE, ALPHA, ADDITIVE, MULTIPLY

        VTextureRef metallicTexture;
        float       metallicFactor {0.0f};

        VTextureRef roughnessTexture;
        float       roughnessFactor {0.0f};

        // Some FBX materials pack Metallic & Roughness in the specular map's G & B channels
        VTextureRef specularTexture;

        VTextureRef normalTexture;

        VTextureRef ambientOcclusionTexture;

        VTextureRef emissiveTexture;
        glm::vec4   emissiveColorIntensity {0.0f, 0.0f, 0.0f, 1.0f};

        glm::vec4 ambientColor {0.0f, 0.0f, 0.0f, 1.0f};

        float ior {1.0f};

        VTextureRef metallicRoughnessTexture;
        bool        doubleSided {true};
    };

    struct VMaterial
    {
        VUUID                         uuid;
        VMaterialType                 type {VMaterialType::ePBRMetallicRoughness};
        VMaterialPBRMetallicRoughness pbrMR;
        std::string                   name;
    };

    bool saveMaterial(const VMaterial& material, const std::string& filePath);
    bool loadMaterial(const std::string& filePath, VMaterial& outMaterial);
} // namespace vasset