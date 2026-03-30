#pragma once

#include <vbase/core/string_view.hpp>

#include <string>

namespace vasset
{
    enum class VAssetType
    {
        eUnknown = 0,
        eTexture,
        eMaterial,
        eMesh,
        eGaussianSplat,
        eScene,
        eSceneManifest,
        eScriptLua,
    };

    inline std::string toString(VAssetType type)
    {
        switch (type)
        {
            case VAssetType::eTexture:
                return "texture";
            case VAssetType::eMaterial:
                return "material";
            case VAssetType::eMesh:
                return "mesh";
            case VAssetType::eGaussianSplat:
                return "gaussian_splat";
            case VAssetType::eScene:
                return "scene";
            case VAssetType::eSceneManifest:
                return "scene_manifest";
            case VAssetType::eScriptLua:
                return "script_lua";
            case VAssetType::eUnknown:
            default:
                return "unknown";
        }
    }

    inline VAssetType fromString(vbase::StringView typeStr)
    {
        if (typeStr == "texture")
        {
            return VAssetType::eTexture;
        }
        else if (typeStr == "material")
        {
            return VAssetType::eMaterial;
        }
        else if (typeStr == "mesh")
        {
            return VAssetType::eMesh;
        }
        else if (typeStr == "gaussian_splat")
        {
            return VAssetType::eGaussianSplat;
        }
        else if (typeStr == "scene")
        {
            return VAssetType::eScene;
        }
        else if (typeStr == "scene_manifest")
        {
            return VAssetType::eSceneManifest;
        }
        else if (typeStr == "script_lua")
        {
            return VAssetType::eScriptLua;
        }
        else
        {
            return VAssetType::eUnknown;
        }
    }
} // namespace vasset
