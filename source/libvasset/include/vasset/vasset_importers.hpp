#pragma once

#include "vasset/asset_error.hpp"
#include "vasset/vgaussiansplat.hpp"
#include "vasset/vmesh.hpp"
#include "vbase/core/uuid.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>

#include <assimp/scene.h>

#include <functional>
#include <string>
#include <vector>

namespace vasset
{
    class VAssetRegistry;

    class VTextureImporter
    {
    public:
        struct ImportOptions
        {
            bool               generateMipmaps {true};
            bool               flipY {false};
            VTextureFileFormat targetTextureFileFormat {VTextureFileFormat::eKTX2};

            // BasisU options, only used if targetTextureFileFormat is eKTX2
            bool     uastc {false};         // true: high quality; false: fast compression
            bool     noSSE {false};         // true: disable SSE; false: enable SSE
            uint32_t qualityLevel {255};    // 1-255
            uint32_t compressionLevel {2};  // 0-4
            uint32_t basisUThreadCount {0}; // 0 = auto-detect

            // By default, only large source textures pay the BasisU encode cost.
            // Smaller source images keep their original encoded payload.
            bool     compressOnlyLargeTextures {true};
            uint32_t basisUCompressMinDimension {2048};
            uint64_t basisUCompressMinSourceBytes {2ULL * 1024ULL * 1024ULL};
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

    class VGaussianSplatImporter
    {
    public:
        struct ImportOptions
        {
            int zstdLevel {3};
        };

        VGaussianSplatImporter(VAssetRegistry& registry);

        VGaussianSplatImporter& setOptions(const ImportOptions& options);

        vbase::Result<vbase::UUID, AssetError>
        importGaussianSplat(vbase::StringView filePath, VGaussianSplat& outSplat, bool forceReimport = false) const;

    private:
        VAssetRegistry& m_Registry;
        ImportOptions   m_Options;
    };

    class VAssetImporter
    {
    public:
        struct ImportProgress
        {
            enum class Phase
            {
                eScan,
                eImport,
                eDone,
            };

            Phase       phase {Phase::eScan};
            size_t      processedFiles {0};
            size_t      totalFiles {0};
            std::string currentPath;
        };

        struct ImportOptions
        {
            std::vector<std::string> ignoredDirectories;
            std::function<void(const ImportProgress&)> progress;
        };

        VAssetImporter(VAssetRegistry& registry);

        VAssetImporter& setOptions(const ImportOptions& options);

        vbase::Result<void, AssetError> importOrReimportAssetFolder(vbase::StringView folderPath,
                                                                    bool              reimport = false);
        vbase::Result<void, AssetError> importOrReimportAsset(vbase::StringView filePath, bool reimport = false);

        VMeshImporter&          getMeshImporter() { return m_MeshImporter; }
        VTextureImporter&       getTextureImporter() { return m_TextureImporter; }
        VGaussianSplatImporter& getGaussianSplatImporter() { return m_GaussianSplatImporter; }

    private:
        VAssetRegistry&        m_Registry;
        VMeshImporter          m_MeshImporter;
        VTextureImporter       m_TextureImporter;
        VGaussianSplatImporter m_GaussianSplatImporter;
        ImportOptions          m_Options;
    };
} // namespace vasset
