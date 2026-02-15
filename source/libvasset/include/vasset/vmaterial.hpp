#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vtexture.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

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
        vbase::UUID uuid;
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
        vbase::UUID                   uuid;
        VMaterialType                 type {VMaterialType::ePBRMetallicRoughness};
        VMaterialPBRMetallicRoughness pbrMR;
        std::string                   name;
    };

    vbase::Result<void, AssetError> saveMaterial(const VMaterial& material, vbase::StringView filePath);
    vbase::Result<void, AssetError> loadMaterial(vbase::StringView filePath, VMaterial& outMaterial);
} // namespace vasset