#pragma once

#include "vasset/vmesh.hpp"

#include <assimp/scene.h>

#include <string>

namespace vasset
{
    class VAssetRegistry;

    class VTextureImporter
    {
    public:
        struct ImportOptions
        {
            bool               generateMipmaps {false};
            bool               flipY {false};
            VTextureFileFormat targetTextureFileFormat {VTextureFileFormat::eKTX2};

            // BasisU options, only used if targetTextureFileFormat is eKTX2
            bool     uastc {false};         // true: high quality; false: fast compression
            bool     noSSE {false};         // true: enable SSE; false: disable SSE
            uint32_t qualityLevel {128};    // 1-255
            uint32_t compressionLevel {2};  // 0-4
            uint32_t basisUThreadCount {0}; // 0 = auto-detect
        };

        VTextureImporter(VAssetRegistry& registry);

        VTextureImporter& setOptions(const ImportOptions& options);

        bool importTexture(const std::string& filePath, VTexture& outTexture, bool forceReimport = false) const;

    private:
        VAssetRegistry& m_Registry;
        ImportOptions   m_Options;
    };

    class VMeshImporter
    {
    public:
        struct ImportOptions
        {
            bool generateMeshlets {true};
        };

        VMeshImporter(VAssetRegistry& registry);

        VMeshImporter& setOptions(const ImportOptions& options);

        bool importMesh(const std::string& filePath, VMesh& outMesh, bool forceReimport = false);

    private:
        void        processNode(const aiNode*, const aiScene*, VMesh& outMesh) const;
        void        processMesh(const aiMesh*, const aiScene*, VMesh& outMesh) const;
        void        processMaterial(const aiMaterial*, VMaterial& outMaterial) const;
        VTextureRef loadTexture(const aiMaterial*, aiTextureType) const;

        static void generateMeshlets(VMesh& outMesh);

    private:
        VAssetRegistry&  m_Registry;
        VTextureImporter m_TextureImporter;
        ImportOptions    m_Options;
        std::string      m_FilePath;
    };

    class VAssetImporter
    {
    public:
        VAssetImporter(VAssetRegistry& registry);

        bool importOrReimportAssetFolder(const std::string& folderPath, bool reimport = false);
        bool importOrReimportAsset(const std::string& filePath, bool reimport = false);

        VMeshImporter&    getMeshImporter() { return m_MeshImporter; }
        VTextureImporter& getTextureImporter() { return m_TextureImporter; }

    private:
        VAssetRegistry&  m_Registry;
        VMeshImporter    m_MeshImporter;
        VTextureImporter m_TextureImporter;
    };
} // namespace vasset