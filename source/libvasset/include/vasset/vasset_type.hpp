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
        else
        {
            return VAssetType::eUnknown;
        }
    }
} // namespace vasset