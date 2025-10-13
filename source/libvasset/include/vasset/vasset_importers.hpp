#pragma once

#include "vasset/vmesh.hpp"

#include <string>

namespace vasset
{
    class VTextureImporter
    {
    public:
        struct ImportOptions
        {
            bool               generateMipmaps {false};
            bool               flipY {false};
            VTextureFileFormat targetTextureFileFormat {VTextureFileFormat::eKTX2};
        };

        VTextureImporter& setOptions(const ImportOptions& options);

        bool importTexture(const std::string& filePath, VTexture& outTexture) const;

    private:
        ImportOptions m_Options;
    };

    class VMeshImporter
    {
    public:
        struct ImportOptions
        {};

        VMeshImporter& setOptions(const ImportOptions& options);

        static bool importMesh(const std::string& filePath, VMesh& outMesh);
    };
} // namespace vasset