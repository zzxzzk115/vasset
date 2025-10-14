#pragma once

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
} // namespace vasset