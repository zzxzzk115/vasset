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
#include <unordered_map>
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

            // BasisU options, only used if targetTextureFileFormat is eKTX2.
            // Defaults favor fast, low-artifact editor imports. Use higher compression
            // levels explicitly for slower final/package-quality imports.
            bool     uastc {true};          // true: UASTC quality path; false: ETC1S smaller-size path
            bool     noSSE {false};         // true: disable SSE; false: enable SSE
            uint32_t qualityLevel {128};    // ETC1S only, 1-255
            uint32_t compressionLevel {0};  // UASTC 0-4, 0 = fastest
            uint32_t basisUThreadCount {0}; // 0 = hardware concurrency

            // By default, only large source textures pay the BasisU encode cost.
            // Smaller source images keep their original encoded payload.
            bool     compressOnlyLargeTextures {true};
            uint32_t basisUCompressMinDimension {2048};
            uint64_t basisUCompressMinSourceBytes {2ULL * 1024ULL * 1024ULL};

            // Editor import guardrail: very large authoring textures are
            // resized before KTX2/BasisU encoding so preview/runtime loads do
            // not stall on 4K+ source payloads.
            bool     downscaleLargeTextures {true};
            uint32_t downscaleMinDimension {4096};
            uint32_t downscaleTargetDimension {2048};

            // Normal-map authoring formats such as BC5/ATI2 only store XY.
            // When enabled, import bakes them to a runtime-generic RGB normal map.
            bool bakeNormalMap {false};
            bool directXNormalMap {false};
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
            bool optimizeVertexCache {true};
            bool optimizeOverdraw {true};
            bool optimizeVertexFetch {true};
        };

        VMeshImporter(VAssetRegistry& registry);

        VMeshImporter& setOptions(const ImportOptions& options);
        VMeshImporter& setProgressCallback(std::function<void(std::string, size_t, size_t)> callback);

        vbase::Result<vbase::UUID, AssetError>
        importMesh(vbase::StringView filePath, VMesh& outMesh, bool forceReimport = false);

    private:
        vbase::Result<vbase::UUID, AssetError>
        importModelPrefab(vbase::StringView filePath, VMesh& outMesh, bool forceReimport);
        void processNode(const aiNode*, const aiScene*, VMesh& outMesh) const;
        void processMesh(const aiMesh*, const aiScene*, VMesh& outMesh) const;
        void processMaterial(const aiMaterial*, VMaterial& outMaterial) const;
        // Load and import texture referenced by Assimp material.
        // - If index is omitted, loads the first texture (index 0).
        VTextureRef loadTexture(const aiMaterial*, aiTextureType) const;
        VTextureRef loadTexture(const aiMaterial*, aiTextureType, unsigned index) const;
        VTextureRef loadTexture(const aiMaterial*, aiTextureType, unsigned index, bool directXNormalMap) const;

        static void generateMeshlets(VMesh& outMesh);
        static void optimizeMeshIndices(VMesh& outMesh, const ImportOptions& options);
        void notifyProgress(std::string item, size_t processed, size_t total) const;

    private:
        VAssetRegistry&  m_Registry;
        VTextureImporter m_TextureImporter;
        ImportOptions    m_Options;
        std::string      m_FilePath;
        std::function<void(std::string, size_t, size_t)> m_ProgressCallback;
        mutable std::unordered_map<std::string, vbase::UUID> m_ModelTextureCache;
        mutable size_t m_ModelProgressProcessed {0};
        mutable size_t m_ModelProgressTotal {0};
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
            struct Diagnostic
            {
                std::string path;
                size_t      line {0};
                size_t      column {0};
                std::string message;
            };

            struct ShaderVirtualIncludeFile
            {
                std::string virtualPath;
                std::string sourceText;
            };

            std::vector<std::string> ignoredDirectories;
            std::vector<ShaderVirtualIncludeFile> shaderVirtualIncludes;
            std::function<void(const ImportProgress&)> progress;
            std::function<void(const Diagnostic&)> diagnostics;
            bool importShaderLibraries {true};
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
