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
        eScriptableObjectLua,
        eRenderGraphJson,
        eMaterialGraphJson,
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
