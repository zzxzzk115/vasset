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
        eSkeleton,
        eAnimation,
        eGaussianSplat,
        eScene,
        eSceneManifest,
        eScriptLua,
        eScriptableObjectLua,
        eRenderGraphJson,
        eMaterialGraphJson,
        eAnimatorGraphJson,
        eShaderLibraryManifest,
        eShaderLibrary,
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
            case VAssetType::eSkeleton:
                return "skeleton";
            case VAssetType::eAnimation:
                return "animation";
            case VAssetType::eGaussianSplat:
                return "gaussian_splat";
            case VAssetType::eScene:
                return "scene";
            case VAssetType::eSceneManifest:
                return "scene_manifest";
            case VAssetType::eScriptLua:
                return "script_lua";
            case VAssetType::eScriptableObjectLua:
                return "scriptable_object_lua";
            case VAssetType::eRenderGraphJson:
                return "render_graph_json";
            case VAssetType::eMaterialGraphJson:
                return "material_graph_json";
            case VAssetType::eAnimatorGraphJson:
                return "animator_graph_json";
            case VAssetType::eShaderLibraryManifest:
                return "shader_library_manifest";
            case VAssetType::eShaderLibrary:
                return "shader_library";
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
        else if (typeStr == "skeleton")
        {
            return VAssetType::eSkeleton;
        }
        else if (typeStr == "animation")
        {
            return VAssetType::eAnimation;
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
        else if (typeStr == "scriptable_object_lua")
        {
            return VAssetType::eScriptableObjectLua;
        }
        else if (typeStr == "render_graph_json")
        {
            return VAssetType::eRenderGraphJson;
        }
        else if (typeStr == "material_graph_json")
        {
            return VAssetType::eMaterialGraphJson;
        }
        else if (typeStr == "animator_graph_json")
        {
            return VAssetType::eAnimatorGraphJson;
        }
        else if (typeStr == "shader_library_manifest")
        {
            return VAssetType::eShaderLibraryManifest;
        }
        else if (typeStr == "shader_library")
        {
            return VAssetType::eShaderLibrary;
        }
        else
        {
            return VAssetType::eUnknown;
        }
    }
} // namespace vasset
