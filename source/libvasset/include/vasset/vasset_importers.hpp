#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vmesh.hpp"
#include "vbase/core/uuid.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>

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

        vbase::Result<vbase::UUID, AssetError>
        importTexture(vbase::StringView filePath, VTexture& outTexture, bool forceReimport = false) const;

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

        vbase::Result<vbase::UUID, AssetError>
        importMesh(vbase::StringView filePath, VMesh& outMesh, bool forceReimport = false);

    private:
        void processNode(const aiNode*, const aiScene*, VMesh& outMesh) const;
        void processMesh(const aiMesh*, const aiScene*, VMesh& outMesh) const;
        void processMaterial(const aiMaterial*, VMaterial& outMaterial) const;
        // Load and import texture referenced by Assimp material.
        // - If index is omitted, loads the first texture (index 0).
        VTextureRef loadTexture(const aiMaterial*, aiTextureType) const;
        VTextureRef loadTexture(const aiMaterial*, aiTextureType, unsigned index) const;

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

        vbase::Result<void, AssetError> importOrReimportAssetFolder(vbase::StringView folderPath,
                                                                    bool              reimport = false);
        vbase::Result<void, AssetError> importOrReimportAsset(vbase::StringView filePath, bool reimport = false);

        VMeshImporter&    getMeshImporter() { return m_MeshImporter; }
        VTextureImporter& getTextureImporter() { return m_TextureImporter; }

    private:
        VAssetRegistry&  m_Registry;
        VMeshImporter    m_MeshImporter;
        VTextureImporter m_TextureImporter;
    };
} // namespace vasset