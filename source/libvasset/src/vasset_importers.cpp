#include "vasset/vasset_importers.hpp"
#include "vasset/vasset_registry.hpp"

#include <ktx.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define DDSKTX_IMPLEMENT
#include <dds-ktx.h>

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <meshoptimizer.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <variant>

namespace
{
    bool isEXR(const std::string& ext) { return ext == ".exr"; }

    bool isSTB(const std::string& ext)
    {
        return ext == ".hdr" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
               ext == ".gif" || ext == ".psd" || ext == ".pic";
    }

    bool isKTXDDS(const std::string& ext) { return ext == ".ktx" || ext == ".dds"; }

    bool isKTX2(const std::string& ext) { return ext == ".ktx2"; }

    bool isCompressedTexture(const std::string& ext) { return isKTXDDS(ext) || isKTX2(ext); }

    bool isValidTexture(const std::string& ext) { return isCompressedTexture(ext) || isEXR(ext) || isSTB(ext); }

    bool isValidModel(const std::string& ext)
    {
        return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".dae";
    }

    vasset::VTextureFormat toVTextureFormat(ddsktx_format format)
    {
        switch (format)
        {
            case DDSKTX_FORMAT_BC1:
                return vasset::VTextureFormat::eBC1;
            case DDSKTX_FORMAT_BC2:
                return vasset::VTextureFormat::eBC2;
            case DDSKTX_FORMAT_BC3:
                return vasset::VTextureFormat::eBC3;
            case DDSKTX_FORMAT_BC4:
                return vasset::VTextureFormat::eBC4;
            case DDSKTX_FORMAT_BC5:
                return vasset::VTextureFormat::eBC5;
            case DDSKTX_FORMAT_BC6H:
                return vasset::VTextureFormat::eBC6H;
            case DDSKTX_FORMAT_BC7:
                return vasset::VTextureFormat::eBC7;
            case DDSKTX_FORMAT_ETC2:
                return vasset::VTextureFormat::eETC2;
            case DDSKTX_FORMAT_ETC2A:
                return vasset::VTextureFormat::eETC2A;
            case DDSKTX_FORMAT_ETC2A1:
                return vasset::VTextureFormat::eETC2A1;
            case DDSKTX_FORMAT_PTC12A:
                return vasset::VTextureFormat::ePTC12A;
            case DDSKTX_FORMAT_PTC14A:
                return vasset::VTextureFormat::ePTC14A;
            case DDSKTX_FORMAT_PTC12:
                return vasset::VTextureFormat::ePTC12;
            case DDSKTX_FORMAT_PTC14:
                return vasset::VTextureFormat::ePTC14;
            case DDSKTX_FORMAT_PTC22:
                return vasset::VTextureFormat::ePTC22;
            case DDSKTX_FORMAT_PTC24:
                return vasset::VTextureFormat::ePTC24;
            case DDSKTX_FORMAT_ASTC4x4:
                return vasset::VTextureFormat::eASTC_4x4;
            case DDSKTX_FORMAT_ASTC5x5:
                return vasset::VTextureFormat::eASTC_5x5;
            case DDSKTX_FORMAT_ASTC6x6:
                return vasset::VTextureFormat::eASTC_6x6;
            case DDSKTX_FORMAT_ASTC8x5:
                return vasset::VTextureFormat::eASTC_8x5;
            case DDSKTX_FORMAT_ASTC8x6:
                return vasset::VTextureFormat::eASTC_8x6;
            case DDSKTX_FORMAT_ASTC10x5:
                return vasset::VTextureFormat::eASTC_10x5;
            case DDSKTX_FORMAT_A8:
                return vasset::VTextureFormat::eA8;
            case DDSKTX_FORMAT_R8:
                return vasset::VTextureFormat::eR8;
            case DDSKTX_FORMAT_RGBA8:
                return vasset::VTextureFormat::eRGBA8;
            case DDSKTX_FORMAT_RGBA8S:
                return vasset::VTextureFormat::eRGBA8S;
            case DDSKTX_FORMAT_RG16:
                return vasset::VTextureFormat::eRG16;
            case DDSKTX_FORMAT_RGB8:
                return vasset::VTextureFormat::eRGB8;
            case DDSKTX_FORMAT_R16:
                return vasset::VTextureFormat::eR16;
            case DDSKTX_FORMAT_R32F:
                return vasset::VTextureFormat::eR32F;
            case DDSKTX_FORMAT_R16F:
                return vasset::VTextureFormat::eR16F;
            case DDSKTX_FORMAT_RG16F:
                return vasset::VTextureFormat::eRG16F;
            case DDSKTX_FORMAT_RG16S:
                return vasset::VTextureFormat::eRG16S;
            case DDSKTX_FORMAT_RGBA16F:
                return vasset::VTextureFormat::eRGBA16F;
            case DDSKTX_FORMAT_RGBA16:
                return vasset::VTextureFormat::eRGBA16;
            case DDSKTX_FORMAT_BGRA8:
                return vasset::VTextureFormat::eBGRA8;
            case DDSKTX_FORMAT_RGB10A2:
                return vasset::VTextureFormat::eA2RGB10;
            case DDSKTX_FORMAT_RG11B10F:
                return vasset::VTextureFormat::eB10RG11;
            case DDSKTX_FORMAT_RG8:
                return vasset::VTextureFormat::eRG8;
            case DDSKTX_FORMAT_RG8S:
                return vasset::VTextureFormat::eRG8S;
            case _DDSKTX_FORMAT_COMPRESSED:
                throw std::runtime_error {"Undefined compressed format."};
            case DDSKTX_FORMAT_ETC1:
                throw std::runtime_error {"ETC1 format is not supported."};
            case DDSKTX_FORMAT_ATC:
            case DDSKTX_FORMAT_ATCE:
            case DDSKTX_FORMAT_ATCI:
                throw std::runtime_error {"ATC formats are not supported."};
            default:
                return vasset::VTextureFormat::eUnknown;
        }
    }

    std::vector<uint8_t> readAll(const std::filesystem::path& p)
    {
        std::ifstream f(p, std::ios::binary | std::ios::ate);
        if (!f)
            return {};
        const size_t         sz = static_cast<size_t>(f.tellg());
        std::vector<uint8_t> data(sz);
        f.seekg(0);
        if (!f.read(reinterpret_cast<char*>(data.data()), sz))
            return {};
        return data;
    }
} // namespace

namespace
{
    struct MatPropValue
    {
        std::variant<aiString, int, float, double, bool, aiColor3D, aiColor4D, std::vector<uint8_t>, aiBlendMode> value;
        bool parsed = false;
    };

    struct MatPropKey
    {
        std::string key;
        unsigned    semantic = 0;
        unsigned    index    = 0;
        bool        operator==(const MatPropKey& other) const noexcept
        {
            return key == other.key && semantic == other.semantic && index == other.index;
        }
    };
    struct MatPropKeyHash
    {
        size_t operator()(const MatPropKey& k) const noexcept
        {
            size_t h1 = std::hash<std::string> {}(k.key);
            size_t h2 = std::hash<unsigned> {}(k.semantic);
            size_t h3 = std::hash<unsigned> {}(k.index);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };
    using MatPropMap = std::unordered_map<MatPropKey, MatPropValue, MatPropKeyHash>;

    MatPropMap parseMaterialProperties(const aiMaterial* material)
    {
        MatPropMap result;
        if (!material)
            return result;

        for (unsigned i = 0; i < material->mNumProperties; ++i)
        {
            aiMaterialProperty* prop = material->mProperties[i];
            MatPropKey          key {prop->mKey.C_Str(), prop->mSemantic, prop->mIndex};
            MatPropValue        entry {};

            switch (prop->mType)
            {
                case aiPTI_String: {
                    aiString value;
                    if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                        entry.value = std::move(value);
                    break;
                }
                case aiPTI_Float: {
                    if (prop->mDataLength / sizeof(float) == 3)
                    {
                        aiColor3D value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                    }
                    else if (prop->mDataLength / sizeof(float) == 4)
                    {
                        aiColor4D value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                    }
                    else
                    {
                        float value;
                        if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                            entry.value = std::move(value);
                    }
                    break;
                }
                case aiPTI_Double: {
                    double value;
                    if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                        entry.value = std::move(value);
                    break;
                }
                case aiPTI_Integer: {
                    int value;
                    if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                        entry.value = std::move(value);
                    bool bvalue;
                    if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, bvalue) == aiReturn_SUCCESS)
                        entry.value = std::move(bvalue);
                    aiBlendMode bmvalue;
                    if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, bmvalue) == aiReturn_SUCCESS)
                        entry.value = std::move(bmvalue);
                    break;
                }
                case aiPTI_Buffer: {
                    std::vector<uint8_t> buf(prop->mDataLength);
                    std::memcpy(buf.data(), prop->mData, prop->mDataLength);
                    entry.value = std::move(buf);
                    break;
                }
                default: {
                    std::cerr << "Warning: Unsupported material property type: " << prop->mType
                              << " for key: " << prop->mKey.C_Str() << std::endl;
                    break;
                }
            }
            result.emplace(std::move(key), std::move(entry));
        }
        return result;
    }

    template<typename T>
    bool tryGet(MatPropMap& props, const char* key, unsigned semantic, unsigned index, T& out)
    {
        MatPropKey mk {key, semantic, index};
        auto       it = props.find(mk);
        if (it != props.end())
        {
            it->second.parsed = true;
            if (auto p = std::get_if<T>(&it->second.value))
            {
                out = *p;
                return true;
            }
        }
        return false;
    }

    glm::mat4 aiToGlm(const aiMatrix4x4& from)
    {
        glm::mat4 to;
        // clang-format off
        // Column-major order
        to[0][0] = from.a1; to[1][0] = from.a2; to[2][0] = from.a3; to[3][0] = from.a4;
        to[0][1] = from.b1; to[1][1] = from.b2; to[2][1] = from.b3; to[3][1] = from.b4;
        to[0][2] = from.c1; to[1][2] = from.c2; to[2][2] = from.c3; to[3][2] = from.c4;
        to[0][3] = from.d1; to[1][3] = from.d2; to[2][3] = from.d3; to[3][3] = from.d4;
        // clang-format on
        return to;
    }

    vasset::VMaterialBlendMode toBlendMode(aiBlendMode mode)
    {
        switch (mode)
        {
            case aiBlendMode_Default:
                return vasset::VMaterialBlendMode::eAlpha;
            case aiBlendMode_Additive:
                return vasset::VMaterialBlendMode::eAdditive;
            default:
                return vasset::VMaterialBlendMode::eNone;
        }
    }
} // namespace

namespace vasset
{
    VTextureImporter::VTextureImporter(VAssetRegistry& registry) : m_Registry(registry) {}

    VTextureImporter& VTextureImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    bool VTextureImporter::importTexture(const std::string& filePath, VTexture& outTexture, bool forceReimport) const
    {
        // Check path
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return false;

        // Check registry
        const std::string relativeImportedPath =
            m_Registry.getImportedAssetPath(VAssetType::eTexture, osPath.stem().string(), true);
        auto entry = m_Registry.lookup(VUUID::fromFilePath(relativeImportedPath));
        if (entry.type != VAssetType::eUnknown && !forceReimport)
        {
            // Load existing texture
            std::cout << "Texture already imported: " << entry.path << std::endl;
            return true;
        }

        // Set UUID
        outTexture.uuid = VUUID::fromFilePath(relativeImportedPath);

        // Check extension
        std::string ext = osPath.extension().string();
        if (isKTXDDS(ext))
        {
            auto pathStr = osPath.string();

#ifndef _WIN32
            // Ensure the path is in the correct format for KTX/DDS parsing
            // \\ -> /
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
#endif

            auto path = std::filesystem::path {pathStr};

            // C++ file I/O
            auto fileBytes = readAll(path);
            if (fileBytes.empty())
                return false;

            ddsktx_texture_info tc {0};

            if (!ddsktx_parse(&tc, fileBytes.data(), static_cast<int>(fileBytes.size()), nullptr))
            {
                return false;
            }

            // Fill outTexture
            outTexture.width           = static_cast<uint32_t>(tc.width);
            outTexture.height          = static_cast<uint32_t>(tc.height);
            outTexture.depth           = static_cast<uint32_t>(tc.depth);
            outTexture.mipLevels       = tc.num_mips;
            outTexture.arrayLayers     = tc.num_layers;
            outTexture.isCubemap       = (tc.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP) != 0;
            outTexture.generateMipmaps = m_Options.generateMipmaps;
            outTexture.type            = VTextureDimension::e2D;
            outTexture.format          = toVTextureFormat(tc.format);
            outTexture.fileFormat      = (ext == ".ktx") ? VTextureFileFormat::eKTX : VTextureFileFormat::eDDS;
            outTexture.data.assign(fileBytes.begin(), fileBytes.end());
        }
        else if (isKTX2(ext))
        {
            // Read KTX2 file
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
                return false;

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<char> bytes(size);
            if (!file.read(bytes.data(), size))
                return false;

            // Validate KTX2
            ktxTexture2* kTexture = nullptr;
            if (ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(bytes.data()),
                                             size,
                                             KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                             &kTexture) != KTX_SUCCESS)
            {
                return false;
            }

            int          width      = kTexture->baseWidth;
            int          height     = kTexture->baseHeight;
            int          depth      = kTexture->baseDepth;
            int          levels     = kTexture->numLevels;
            int          layers     = kTexture->numLayers;
            bool         isCubemap  = kTexture->numFaces == 6;
            bool         genMips    = kTexture->generateMipmaps;
            int          dimensions = kTexture->numDimensions;
            ktx_uint32_t format     = kTexture->vkFormat;

            // Fill outTexture
            outTexture.width           = width;
            outTexture.height          = height;
            outTexture.depth           = depth;
            outTexture.mipLevels       = levels;
            outTexture.arrayLayers     = layers;
            outTexture.isCubemap       = isCubemap;
            outTexture.generateMipmaps = genMips;
            outTexture.type            = static_cast<VTextureDimension>(dimensions);
            outTexture.format          = static_cast<VTextureFormat>(format);
            outTexture.data.assign(bytes.begin(), bytes.end());
        }
        else
        {
            // Load as raw image
            int   width, height, channels;
            void* pixels = nullptr;
            bool  hdr    = false;

            if (isEXR(ext))
            {
                // Supported by tinyexr
                const char* err = nullptr;
                float*      img = nullptr;

                if (LoadEXR(&img, &width, &height, osPath.string().c_str(), &err) != TINYEXR_SUCCESS)
                {
                    if (err)
                    {
                        std::cerr << "Failed to load EXR image: " << err << std::endl;
                        FreeEXRErrorMessage(err);
                    }
                    return false;
                }

                channels = 4; // RGBA
                pixels   = img;
                hdr      = true;
            }
            else if (isSTB(ext))
            {
                // Load image file
                auto* file = fopen(filePath.c_str(), "rb");
                if (!file)
                {
                    return false;
                }

                // Supported by stb_image
                hdr = stbi_is_hdr_from_file(file);

                stbi_set_flip_vertically_on_load(m_Options.flipY ? 1 : 0);

                if (hdr)
                {
                    pixels = stbi_loadf_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
                }
                else
                {
                    pixels = stbi_load_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
                }
                fclose(file);
                if (!pixels)
                    return false;
            }
            else
            {
                std::cerr << "Unsupported image format: " << ext << std::endl;
                return false;
            }

            auto targetFormat = m_Options.targetTextureFileFormat;

            if (targetFormat == VTextureFileFormat::eKTX || targetFormat == VTextureFileFormat::eDDS)
            {
                std::cerr << "Warning: Target texture file format KTX or DDS is not supported for image files. "
                             "Defaulting to KTX2."
                          << std::endl;
                targetFormat = VTextureFileFormat::eKTX2;
            }

            // Load other image formats
            if (targetFormat != VTextureFileFormat::eKTX2)
            {
                // Fill outTexture
                outTexture.width           = width;
                outTexture.height          = height;
                outTexture.depth           = 1;
                outTexture.mipLevels       = 1;
                outTexture.arrayLayers     = 1;
                outTexture.isCubemap       = false;
                outTexture.generateMipmaps = m_Options.generateMipmaps;
                outTexture.type            = VTextureDimension::e2D;
                outTexture.format          = hdr ? VTextureFormat::eRGBA32F : VTextureFormat::eRGBA8;
                outTexture.fileFormat      = m_Options.targetTextureFileFormat;
                outTexture.data.assign(reinterpret_cast<uint8_t*>(pixels),
                                       reinterpret_cast<uint8_t*>(pixels) +
                                           (hdr ? width * height * 4 * sizeof(float) : width * height * 4));
                stbi_image_free(pixels);
                return true;
            }

            // Convert to ktx2
            ktxTexture2*         kTexture = nullptr;
            ktxTextureCreateInfo ci {};
            ci.baseWidth       = width;
            ci.baseHeight      = height;
            ci.baseDepth       = 1;
            ci.numLevels       = 1;
            ci.numLayers       = 1;
            ci.numFaces        = 1;
            ci.numDimensions   = 2;
            ci.isArray         = KTX_FALSE;
            ci.generateMipmaps = m_Options.generateMipmaps;
            ci.vkFormat        = static_cast<uint32_t>(hdr ? VTextureFormat::eRGBA32F : VTextureFormat::eRGBA8);

            if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &kTexture) != KTX_SUCCESS)
            {
                stbi_image_free(pixels);
                return false;
            }

            if (ktxTexture_SetImageFromMemory(ktxTexture(kTexture),
                                              0,
                                              0,
                                              0,
                                              reinterpret_cast<const ktx_uint8_t*>(pixels),
                                              hdr ? width * height * 4 * sizeof(float) : width * height * 4) !=
                KTX_SUCCESS)
            {
                stbi_image_free(pixels);
                ktxTexture_Destroy(ktxTexture(kTexture));
                return false;
            }
            stbi_image_free(pixels);

            // BasisU Compression
            ktxBasisParams params {};
            params.uastc            = m_Options.uastc;
            params.noSSE            = m_Options.noSSE;
            params.qualityLevel     = m_Options.qualityLevel;
            params.compressionLevel = m_Options.compressionLevel;
            params.threadCount      = m_Options.basisUThreadCount;
            ktxTexture2_CompressBasisEx(kTexture, &params);

            // Write to memory
            ktx_uint8_t* bytes = nullptr;
            ktx_size_t   size  = 0;
            if (ktxTexture_WriteToMemory(ktxTexture(kTexture), &bytes, &size) != KTX_SUCCESS)
            {
                ktxTexture_Destroy(ktxTexture(kTexture));
                return false;
            }

            // Fill outTexture
            outTexture.width           = width;
            outTexture.height          = height;
            outTexture.depth           = ci.baseDepth;
            outTexture.mipLevels       = ci.numLevels;
            outTexture.arrayLayers     = ci.numLayers;
            outTexture.isCubemap       = ci.numFaces == 6;
            outTexture.generateMipmaps = ci.generateMipmaps;
            outTexture.type            = static_cast<VTextureDimension>(ci.numDimensions);
            outTexture.format          = hdr ? VTextureFormat::eRGBA32F : VTextureFormat::eRGBA8;
            outTexture.fileFormat      = VTextureFileFormat::eKTX2;
            outTexture.data.assign(bytes, bytes + size);

            ktxTexture_Destroy(ktxTexture(kTexture));
        }

        const std::string importedPath =
            m_Registry.getImportedAssetPath(VAssetType::eTexture, osPath.stem().string(), false);
        if (!saveTexture(outTexture, importedPath, osPath))
        {
            std::cerr << "Warning: Failed to save texture: " << importedPath << std::endl;
            return false;
        }
        m_Registry.registerAsset(outTexture.uuid, relativeImportedPath, VAssetType::eTexture);

        return true;
    }

    VMeshImporter::VMeshImporter(VAssetRegistry& registry) : m_Registry(registry), m_TextureImporter(registry) {}

    VMeshImporter& VMeshImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    bool VMeshImporter::importMesh(const std::string& filePath, VMesh& outMesh, bool forceReimport)
    {
        // Check path
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return false;

        // Check registry
        const std::string relativeImportedPath =
            m_Registry.getImportedAssetPath(VAssetType::eMesh, osPath.stem().string(), true);
        auto entry = m_Registry.lookup(VUUID::fromFilePath(relativeImportedPath));
        if (entry.type != VAssetType::eUnknown && !forceReimport)
        {
            // Load existing mesh
            std::cout << "Mesh already imported: " << entry.path << std::endl;
            return true;
        }

        // Set UUID
        outMesh.uuid = VUUID::fromFilePath(relativeImportedPath);

        m_FilePath = filePath;

        // Set Assimp process flags
        // Ignore scene graph, as we flatten everything into a single VMesh
        unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_PreTransformVertices |
                             aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_GenUVCoords;

        Assimp::Importer importer {};
        const aiScene*   scene = importer.ReadFile(filePath, flags);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode || !scene->HasMeshes())
            return false;

        outMesh.name           = scene->mRootNode->mName.C_Str();
        outMesh.sourceFileName = osPath.stem().string();

        // Process the scene
        processNode(scene->mRootNode, scene, outMesh);

        // Generate meshlets if enabled
        if (m_Options.generateMeshlets)
        {
            generateMeshlets(outMesh);
        }

        // Set vertex flags
        outMesh.vertexFlags |= VVertexFlags::ePosition;
        if (outMesh.normals.size() == outMesh.vertexCount)
            outMesh.vertexFlags |= VVertexFlags::eNormal;
        if (outMesh.colors.size() == outMesh.vertexCount)
            outMesh.vertexFlags |= VVertexFlags::eColor;
        if (outMesh.texCoords0.size() == outMesh.vertexCount)
            outMesh.vertexFlags |= VVertexFlags::eTexCoord0;
        if (outMesh.texCoords1.size() == outMesh.vertexCount)
            outMesh.vertexFlags |= VVertexFlags::eTexCoord1;
        if (outMesh.tangents.size() == outMesh.vertexCount)
            outMesh.vertexFlags |= VVertexFlags::eTangent;
        // Note: Joint indices and weights would require additional processing, e.g., from bones

        const std::string importedPath =
            m_Registry.getImportedAssetPath(VAssetType::eMesh, osPath.stem().string(), false);
        if (!saveMesh(outMesh, importedPath, osPath))
        {
            std::cerr << "Warning: Failed to save mesh: " << importedPath << std::endl;
            return false;
        }
        m_Registry.registerAsset(outMesh.uuid, relativeImportedPath, VAssetType::eMesh);

        return true;
    }

    void VMeshImporter::processNode(const aiNode* node, const aiScene* scene, VMesh& outMesh) const
    {
        if (!node)
            return;

        // Process all the node's meshes
        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            processMesh(mesh, scene, outMesh);
        }

        // Then do the same for each of its children
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            processNode(node->mChildren[i], scene, outMesh);
        }
    }

    void VMeshImporter::processMesh(const aiMesh* mesh, const aiScene* scene, VMesh& outMesh) const
    {
        if (!mesh)
            return;

        VSubMesh subMesh {};
        subMesh.vertexOffset  = static_cast<uint32_t>(outMesh.vertexCount);
        subMesh.vertexCount   = mesh->mNumVertices;
        subMesh.indexOffset   = static_cast<uint32_t>(outMesh.indices.size());
        subMesh.indexCount    = mesh->mNumFaces * 3; // Assuming triangulated
        subMesh.materialIndex = mesh->mMaterialIndex;
        subMesh.name          = mesh->mName.C_Str();

        // Process vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            // Positions
            if (mesh->HasPositions())
            {
                VPosition pos {};
                pos.x = mesh->mVertices[i].x;
                pos.y = mesh->mVertices[i].y;
                pos.z = mesh->mVertices[i].z;
                outMesh.positions.push_back(pos);
            }

            // Normals
            if (mesh->HasNormals())
            {
                VNormal norm {};
                norm.x = mesh->mNormals[i].x;
                norm.y = mesh->mNormals[i].y;
                norm.z = mesh->mNormals[i].z;
                outMesh.normals.push_back(norm);
            }

            // Colors
            if (mesh->HasVertexColors(0))
            {
                VColor color {};
                color.r = mesh->mColors[0][i].r;
                color.g = mesh->mColors[0][i].g;
                color.b = mesh->mColors[0][i].b;
                outMesh.colors.push_back(color);
            }

            // Texture Coordinates
            if (mesh->HasTextureCoords(0))
            {
                VTexCoord texCoord {};
                texCoord.x = mesh->mTextureCoords[0][i].x;
                texCoord.y = mesh->mTextureCoords[0][i].y;
                outMesh.texCoords0.push_back(texCoord);
            }
            if (mesh->HasTextureCoords(1))
            {
                VTexCoord texCoord {};
                texCoord.x = mesh->mTextureCoords[1][i].x;
                texCoord.y = mesh->mTextureCoords[1][i].y;
                outMesh.texCoords1.push_back(texCoord);
            }

            // Tangents
            if (mesh->HasTangentsAndBitangents())
            {
                glm::vec3 tangent   = {mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z};
                glm::vec3 bitangent = {mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z};
                VTangent  tangentWithHandness = VTangent(
                    tangent, glm::dot(glm::cross(outMesh.normals.back(), bitangent), tangent) < 0.0f ? -1.0f : 1.0f);
                outMesh.tangents.push_back(tangentWithHandness);
            }
        }

        // Process indices
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
        {
            aiFace face = mesh->mFaces[i];
            if (face.mNumIndices != 3)
            {
                std::cerr << "Warning: Non-triangulated face found in mesh: " << mesh->mName.C_Str() << std::endl;
                continue;
            }
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
            {
                outMesh.indices.push_back(face.mIndices[j]);
            }
        }

        // Process material
        if (mesh->mMaterialIndex < scene->mNumMaterials)
        {
            // Get material name
            aiMaterial* aiMat        = scene->mMaterials[mesh->mMaterialIndex];
            std::string materialName = aiMat->GetName().C_Str();
            if (materialName.empty())
            {
                materialName = std::format("{}_{}", outMesh.sourceFileName, mesh->mMaterialIndex);
            }
            else
            {
                materialName = std::format("{}_{}", outMesh.sourceFileName, materialName);
            }

            const std::string relativeImportedPath =
                m_Registry.getImportedAssetPath(VAssetType::eMaterial, materialName, true);

            auto uuid  = VUUID::fromFilePath(relativeImportedPath);
            auto entry = m_Registry.lookup(uuid);
            if (entry.type != VAssetType::eUnknown)
            {
                // Use existing material
                std::cout << "Material already imported: " << entry.path << std::endl;
                outMesh.materials.push_back(VMaterialRef {uuid});
            }
            else
            {
                VMaterial vMat {};

                vMat.uuid = uuid;

                processMaterial(aiMat, vMat);

                const std::string importedPath =
                    m_Registry.getImportedAssetPath(VAssetType::eMaterial, materialName, false);
                if (!saveMaterial(vMat, importedPath))
                {
                    std::cerr << "Warning: Failed to save material: " << importedPath << std::endl;
                }
                m_Registry.registerAsset(vMat.uuid, relativeImportedPath, VAssetType::eMaterial);
                outMesh.materials.push_back(VMaterialRef {vMat.uuid});
            }
        }

        outMesh.subMeshes.push_back(subMesh);
        outMesh.vertexCount += mesh->mNumVertices;
    }

    void VMeshImporter::processMaterial(const aiMaterial* material, VMaterial& outMaterial) const
    {
        if (!material)
            return;

        auto props = parseMaterialProperties(material);

        // Name
        aiString name;
        if (tryGet(props, AI_MATKEY_NAME, name))
        {
            outMaterial.name = name.C_Str();
        }

        // Read color properties
        aiColor3D kd(1, 1, 1), ks(0, 0, 0), ke(0, 0, 0), ka(0, 0, 0);
        tryGet(props, AI_MATKEY_COLOR_DIFFUSE, kd);
        tryGet(props, AI_MATKEY_COLOR_SPECULAR, ks);
        tryGet(props, AI_MATKEY_COLOR_EMISSIVE, ke);
        tryGet(props, AI_MATKEY_COLOR_AMBIENT, ka);

        // Read scalar properties
        float Ns = 0.0f, d = 1.0f, Ni = 1.5f, emissiveIntensity = 1.0f;
        tryGet(props, AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity);
        tryGet(props, AI_MATKEY_SHININESS, Ns);
        tryGet(props, AI_MATKEY_OPACITY, d);
        tryGet(props, AI_MATKEY_REFRACTI, Ni);

        // Alpha masking & Blend mode
        aiString alphaMode;
        if (tryGet(props, AI_MATKEY_GLTF_ALPHAMODE, alphaMode))
        {
            auto alphaModeStr = std::string(alphaMode.C_Str());
            if (alphaModeStr == "MASK")
                outMaterial.pbrMR.alphaMode = VMaterialAlphaMode::eMask;
            else if (alphaModeStr == "BLEND")
                outMaterial.pbrMR.alphaMode = VMaterialAlphaMode::eBlend;
            else
                outMaterial.pbrMR.alphaMode = VMaterialAlphaMode::eOpaque;
        }
        float alphaCutoff = 0.5f;
        if (tryGet(props, AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff))
        {
            outMaterial.pbrMR.alphaCutoff = alphaCutoff;
        }
        aiBlendMode blendMode = aiBlendMode_Default;
        if (tryGet(props, AI_MATKEY_BLEND_FUNC, blendMode))
        {
            outMaterial.pbrMR.blendMode = toBlendMode(blendMode);
        }
        else
        {
            outMaterial.pbrMR.blendMode =
                d < 1.0f ? toBlendMode(aiBlendMode::aiBlendMode_Default) : VMaterialBlendMode::eNone;
        }

        outMaterial.pbrMR.baseColor = glm::vec4(kd.r, kd.g, kd.b, 1.0);
        outMaterial.pbrMR.opacity   = d;

        outMaterial.pbrMR.metallicFactor =
            std::clamp((0.2126f * ks.r + 0.7152f * ks.g + 0.0722f * ks.b - 0.04f) / (1.0f - 0.04f), 0.0f, 1.0f);
        outMaterial.pbrMR.roughnessFactor        = glm::clamp(std::sqrt(2.0f / (Ns + 2.0f)), 0.04f, 1.0f);
        outMaterial.pbrMR.emissiveColorIntensity = glm::vec4(ke.r, ke.g, ke.b, emissiveIntensity);
        outMaterial.pbrMR.ior                    = Ni;
        outMaterial.pbrMR.ambientColor           = glm::vec4(ka.r, ka.g, ka.b, 1.0f);

        // Override with common PBR extensions if present
        aiColor4D baseColorFactorPBR(1, 1, 1, 1), emissiveIntensityPBR(0, 0, 0, 1);
        float     metallicFactorPBR  = 0.0f;
        float     roughnessFactorPBR = 0.5f;
        if (tryGet(props, AI_MATKEY_BASE_COLOR, baseColorFactorPBR))
        {
            outMaterial.pbrMR.baseColor =
                glm::vec4(baseColorFactorPBR.r, baseColorFactorPBR.g, baseColorFactorPBR.b, 1.0);
        }
        if (tryGet(props, AI_MATKEY_METALLIC_FACTOR, metallicFactorPBR))
        {
            outMaterial.pbrMR.metallicFactor = metallicFactorPBR;
        }
        if (tryGet(props, AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactorPBR))
        {
            outMaterial.pbrMR.roughnessFactor = roughnessFactorPBR;
        }
        if (tryGet(props, AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensityPBR))
        {
            outMaterial.pbrMR.emissiveColorIntensity = glm::vec4(
                emissiveIntensityPBR.r, emissiveIntensityPBR.g, emissiveIntensityPBR.b, emissiveIntensityPBR.a);
        }

        // Textures
        outMaterial.pbrMR.baseColorTexture         = loadTexture(material, aiTextureType_DIFFUSE);
        outMaterial.pbrMR.alphaTexture             = loadTexture(material, aiTextureType_OPACITY);
        outMaterial.pbrMR.metallicTexture          = loadTexture(material, aiTextureType_METALNESS);
        outMaterial.pbrMR.roughnessTexture         = loadTexture(material, aiTextureType_DIFFUSE_ROUGHNESS);
        outMaterial.pbrMR.specularTexture          = loadTexture(material, aiTextureType_SPECULAR);
        outMaterial.pbrMR.normalTexture            = loadTexture(material, aiTextureType_NORMALS);
        outMaterial.pbrMR.ambientOcclusionTexture  = loadTexture(material, aiTextureType_LIGHTMAP);
        outMaterial.pbrMR.emissiveTexture          = loadTexture(material, aiTextureType_EMISSIVE);
        outMaterial.pbrMR.metallicRoughnessTexture = loadTexture(material, aiTextureType_GLTF_METALLIC_ROUGHNESS);

        // Double sided?
        bool doubleSided = false;
        if (tryGet(props, AI_MATKEY_TWOSIDED, doubleSided))
        {
            outMaterial.pbrMR.doubleSided = doubleSided;
        }

        // Unhandled properties warning
        for (auto& [k, v] : props)
        {
            if (!v.parsed)
            {
                std::cerr << "Warning: Unhandled material property: " << k.key << " (semantic: " << k.semantic
                          << ", index: " << k.index << ")" << std::endl;
            }
        }

        // Log material info
        std::cout << "Imported material: " << outMaterial.name << std::endl;
    }

    VTextureRef VMeshImporter::loadTexture(const aiMaterial* material, aiTextureType type) const
    {
        if (!material)
            return {};

        if (material->GetTextureCount(type) > 0)
        {
            aiString str;
            if (material->GetTexture(type, 0, &str) == aiReturn_SUCCESS)
            {
                VTexture              texture {};
                std::filesystem::path relativePath(str.C_Str());

                std::filesystem::path texPath = std::filesystem::path(m_FilePath).parent_path() / relativePath;

                if (!m_TextureImporter.importTexture(texPath.generic_string(), texture))
                {
                    return {};
                }

                std::cout << "  Loaded texture: " << texPath << ", " << texture.toString() << std::endl;

                return VTextureRef {texture.uuid};
            }
        }
        return {};
    }

    void VMeshImporter::generateMeshlets(VMesh& outMesh)
    {
        // --- Build meshlets per submesh ---
        for (auto& sub : outMesh.subMeshes)
        {
            // Extra: meshlets
            // https://github.com/zeux/meshoptimizer/tree/v0.24#clusterization
            sub.meshletGroup.meshlets.clear();
            sub.meshletGroup.meshletTriangles.clear();
            sub.meshletGroup.meshletVertices.clear();

            const size_t maxVerts = 64;
            const size_t maxTris  = 124;

            const uint32_t* subIndices = outMesh.indices.data() + sub.indexOffset;
            size_t          subCount   = sub.indexCount;

            // allocate space for meshlet vertices and triangles
            size_t                       maxMeshlets = meshopt_buildMeshletsBound(subCount, maxVerts, maxTris);
            std::vector<meshopt_Meshlet> meshletsTemp(maxMeshlets);

            // store current offsets
            size_t baseVertOffset = sub.meshletGroup.meshletVertices.size();
            size_t baseTriOffset  = sub.meshletGroup.meshletTriangles.size();

            sub.meshletGroup.meshletVertices.resize(baseVertOffset + maxMeshlets * maxVerts);
            sub.meshletGroup.meshletTriangles.resize(baseTriOffset + maxMeshlets * maxTris * 3);

            // build meshlets
            size_t meshletCount = meshopt_buildMeshlets(meshletsTemp.data(),
                                                        sub.meshletGroup.meshletVertices.data() + baseVertOffset,
                                                        sub.meshletGroup.meshletTriangles.data() + baseTriOffset,
                                                        subIndices,
                                                        subCount,
                                                        reinterpret_cast<const float*>(outMesh.positions.data()),
                                                        outMesh.positions.size(),
                                                        sizeof(glm::vec3),
                                                        maxVerts,
                                                        maxTris,
                                                        0.5f);

            meshletsTemp.resize(meshletCount);

            // copy to Meshlet
            for (size_t i = 0; i < meshletCount; ++i)
            {
                const auto& src = meshletsTemp[i];
                VMeshlet    dst {};

                dst.vertexOffset   = baseVertOffset + src.vertex_offset;
                dst.triangleOffset = baseTriOffset + src.triangle_offset;
                dst.vertexCount    = src.vertex_count;
                dst.triangleCount  = src.triangle_count;

                // --- Assign material index directly from submesh ---
                dst.materialIndex = sub.materialIndex;

                auto bounds = meshopt_computeMeshletBounds(&sub.meshletGroup.meshletVertices[dst.vertexOffset],
                                                           &sub.meshletGroup.meshletTriangles[dst.triangleOffset],
                                                           dst.triangleCount,
                                                           reinterpret_cast<const float*>(outMesh.positions.data()),
                                                           outMesh.positions.size(),
                                                           sizeof(glm::vec3));

                dst.center     = glm::vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
                dst.radius     = bounds.radius;
                dst.coneAxis   = glm::vec3(bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]);
                dst.coneCutoff = bounds.cone_cutoff;
                dst.coneApex   = glm::vec3(bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]);

                sub.meshletGroup.meshlets.push_back(dst);
            }

            // Resize to actual used size
            const auto& last = sub.meshletGroup.meshlets.back();

            sub.meshletGroup.meshletVertices.resize(last.vertexOffset + last.vertexCount);
            sub.meshletGroup.meshletTriangles.resize(last.triangleOffset + ((last.triangleCount * 3 + 3) & ~3));
        }
    }

    VAssetImporter::VAssetImporter(VAssetRegistry& registry) :
        m_Registry(registry), m_TextureImporter(registry), m_MeshImporter(registry)
    {}

    bool VAssetImporter::importOrReimportAssetFolder(const std::string& folderPath, bool reimport)
    {
        // Check path
        std::filesystem::path osPath(folderPath);
        if (!std::filesystem::exists(osPath) || !std::filesystem::is_directory(osPath))
            return false;

        // Iterate over files in the directory
        for (const auto& entry : std::filesystem::directory_iterator(osPath))
        {
            if (entry.is_regular_file())
            {
                std::string filePath = entry.path().string();
                std::string ext      = entry.path().extension().string();

                if (isValidTexture(ext))
                {
                    VTexture texture;
                    if (m_TextureImporter.importTexture(filePath, texture, reimport))
                    {
                        std::cout << "Imported texture: " << filePath << std::endl;
                    }
                    else
                    {
                        std::cerr << "Failed to import texture: " << filePath << std::endl;
                    }
                }
                else if (isValidModel(ext))
                {
                    VMesh mesh;
                    if (m_MeshImporter.importMesh(filePath, mesh, reimport))
                    {
                        std::cout << "Imported mesh: " << filePath << std::endl;
                    }
                    else
                    {
                        std::cerr << "Failed to import mesh: " << filePath << std::endl;
                    }
                }
                // else
                // {
                //     std::cout << "Unsupported file type, skipping: " << filePath << std::endl;
                // }
            }
            else if (entry.is_directory())
            {
                // Recursively import from subdirectories
                if (!importOrReimportAssetFolder(entry.path().string(), reimport))
                {
                    std::cerr << "Failed to import assets from folder: " << entry.path().string() << std::endl;
                }
            }
        }

        return true;
    }

    bool VAssetImporter::importOrReimportAsset(const std::string& filePath, bool reimport)
    {
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return false;

        if (std::filesystem::is_directory(osPath))
        {
            std::cerr << "Provided path is a directory, expected a file: " << filePath << std::endl;
            return false;
        }

        std::string ext = osPath.extension().string();

        if (isValidTexture(ext))
        {
            VTexture texture;
            if (m_TextureImporter.importTexture(filePath, texture, reimport))
            {
                std::cout << "Imported/Reimported texture: " << filePath << std::endl;
                return true;
            }
            else
            {
                std::cerr << "Failed to import/reimport texture: " << filePath << std::endl;
                return false;
            }
        }
        else if (isValidModel(ext))
        {
            VMesh mesh;
            if (m_MeshImporter.importMesh(filePath, mesh, reimport))
            {
                std::cout << "Imported/Reimported mesh: " << filePath << std::endl;
                return true;
            }
            else
            {
                std::cerr << "Failed to import/reimport mesh: " << filePath << std::endl;
                return false;
            }
        }

        std::cerr << "Unsupported asset type for import/reimport: " << filePath << std::endl;
        return false;
    }
} // namespace vasset