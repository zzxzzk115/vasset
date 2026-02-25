#pragma once

#include "vasset/vtexture.hpp"

#include <vbase/core/uuid.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vasset
{
    // ------------------------------------------------------------
    // Material model routing
    // ------------------------------------------------------------
    enum class VMaterialModel : uint8_t
    {
        eUnknown = 0,

        // glTF
        eUnlit,
        ePBRMetallicRoughness,
        ePBRSpecularGlossiness,

        // DCC fallbacks
        ePhong,

        // Reserved for future engine-specific models
        eCustom
    };

    // ------------------------------------------------------------
    // Alpha
    // ------------------------------------------------------------
    enum class VMaterialAlphaMode : uint8_t
    {
        eOpaque = 0,
        eMask,
        eBlend
    };

    enum class VMaterialBlendMode : uint8_t
    {
        eNone = 0,
        eAlpha,
        eAdditive,
        eMultiply
    };

    // ------------------------------------------------------------
    // Feature flags (best-effort hints extracted from Assimp)
    // These are *hints* for routing/optimization; full fidelity is
    // preserved in VMaterialProperty list.
    // ------------------------------------------------------------
    enum VMaterialFeatureFlags : uint32_t
    {
        eFeature_None         = 0,
        eFeature_ClearCoat    = 1u << 0,
        eFeature_Transmission = 1u << 1,
        eFeature_Sheen        = 1u << 2,
        eFeature_Volume       = 1u << 3,
        eFeature_Specular     = 1u << 4,
        eFeature_Iridescence  = 1u << 5,
        eFeature_Anisotropy   = 1u << 6
    };

    // ------------------------------------------------------------
    // Dynamic material properties (lossless Assimp capture)
    // ------------------------------------------------------------
    // Mirrors aiPropertyTypeInfo values, but we keep our own enum to
    // avoid leaking Assimp headers into vasset public headers.
    enum class VMaterialPropertyType : uint8_t
    {
        eFloat   = 0,
        eDouble  = 1,
        eString  = 2,
        eInteger = 3,
        eBuffer  = 4,
        eUnknown = 255
    };

    struct VMaterialProperty
    {
        // Raw assimp property identity
        std::string key;
        uint32_t    semantic {0};
        uint32_t    index {0};

        VMaterialPropertyType type {VMaterialPropertyType::eUnknown};

        // Raw bytes. For strings, stores UTF-8 bytes without a null terminator.
        std::vector<uint8_t> data;
    };

    // ------------------------------------------------------------
    // Texture bindings captured from Assimp (lossless per-type/index)
    // ------------------------------------------------------------
    // `type` is the numeric value of Assimp aiTextureType.
    struct VMaterialTextureBinding
    {
        uint16_t type {0};
        uint16_t index {0};

        // From aiMaterial::GetTexture(...)
        uint8_t uvIndex {0};
        uint8_t mapping {0};
        uint8_t op {0};
        uint8_t mapModeU {0};
        uint8_t mapModeV {0};

        float blend {1.0f};

        VTextureRef texture;
    };

    // ------------------------------------------------------------
    // Core model payloads (fixed-layout fast path)
    // Full fidelity is still preserved in properties.
    // ------------------------------------------------------------

    struct VMaterialPBRMetallicRoughness
    {
        // Factors
        glm::vec4 baseColor {1.0f, 1.0f, 1.0f, 1.0f};
        float     metallicFactor {1.0f};
        float     roughnessFactor {1.0f};

        // Alpha
        float              alphaCutoff {0.5f};
        VMaterialAlphaMode alphaMode {VMaterialAlphaMode::eOpaque};
        float              opacity {1.0f};
        VMaterialBlendMode blendMode {VMaterialBlendMode::eNone};

        // Extras
        glm::vec4 emissiveColorIntensity {0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 ambientColor {0.0f, 0.0f, 0.0f, 1.0f};
        float     ior {1.0f};
        bool      doubleSided {false};

        // Textures
        VTextureRef baseColorTexture;
        VTextureRef alphaTexture;

        VTextureRef metallicTexture;
        VTextureRef roughnessTexture;
        VTextureRef metallicRoughnessTexture;
        VTextureRef specularTexture; // legacy/fbx packing

        VTextureRef normalTexture;
        VTextureRef ambientOcclusionTexture;
        VTextureRef emissiveTexture;
    };

    struct VMaterialPBRSpecularGlossiness
    {
        glm::vec4 diffuseColor {1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec3 specularFactor {1.0f, 1.0f, 1.0f};
        float     glossinessFactor {1.0f};

        VTextureRef diffuseTexture;
        VTextureRef specularGlossinessTexture;
    };

    struct VMaterialUnlit
    {
        glm::vec4   color {1.0f, 1.0f, 1.0f, 1.0f};
        VTextureRef colorTexture;
    };

    struct VMaterialPhong
    {
        glm::vec4 diffuse {1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec3 specular {0.0f, 0.0f, 0.0f};
        float     shininess {0.0f};
        float     opacity {1.0f};
        float     ior {1.5f};
        glm::vec3 emissive {0.0f, 0.0f, 0.0f};

        VTextureRef diffuseTexture;
        VTextureRef specularTexture;
        VTextureRef normalTexture;
        VTextureRef opacityTexture;
        VTextureRef emissiveTexture;
    };

    union VMaterialCoreData
    {
        VMaterialPBRMetallicRoughness  pbrMR;
        VMaterialPBRSpecularGlossiness pbrSG;
        VMaterialUnlit                 unlit;
        VMaterialPhong                 phong;

        VMaterialCoreData() {}
        ~VMaterialCoreData() {}
    };

    struct VMaterial
    {
        VMaterialModel model {VMaterialModel::ePBRMetallicRoughness};

        // Feature flags (hints). Full fidelity is in `properties`.
        uint32_t features {VMaterialFeatureFlags::eFeature_None};

        // Fixed-layout core payload (fast-path for common shading)
        VMaterialCoreData core;

        // Full lossless texture bindings from Assimp.
        std::vector<VMaterialTextureBinding> textures;

        // Full lossless property capture from Assimp.
        std::vector<VMaterialProperty> properties;

        std::string name;

        VMaterial() { core.pbrMR = {}; }
    };
} // namespace vasset
