#include "vasset/vasset_importers.hpp"
#include "vasset/vasset_registry.hpp"
#include "vasset/vasset_type.hpp"
#include "vasset/vgaussiansplat.hpp"
#include "vasset/vimport.hpp"

#include <vbase/core/result.hpp>

#include <ktx.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

#define DDSKTX_IMPLEMENT
#include <dds-ktx.h>

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <gf/core/gauss_ir.h>
#include <gf/io/registry.h>

#include <glm/gtc/quaternion.hpp>

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <variant>

namespace
{
    std::string assetErrorLabel(vasset::AssetError error)
    {
        switch (error)
        {
            case vasset::AssetError::eOk:
                return "ok";
            case vasset::AssetError::eNotFound:
                return "not_found";
            case vasset::AssetError::eInvalidFormat:
                return "invalid_format";
            case vasset::AssetError::eInvalidImportFile:
                return "invalid_import_file";
            case vasset::AssetError::eUnknownImporter:
                return "unknown_importer";
            case vasset::AssetError::eImportFailed:
                return "import_failed";
            case vasset::AssetError::eIOError:
                return "io_error";
            case vasset::AssetError::eNotSupported:
                return "not_supported";
            case vasset::AssetError::eOutOfMemory:
                return "out_of_memory";
            default:
                return "unknown_error";
        }
    }

    bool isEXR(vbase::StringView ext) { return ext == ".exr"; }

    bool isSTB(vbase::StringView ext)
    {
        return ext == ".hdr" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga" ||
               ext == ".gif" || ext == ".psd" || ext == ".pic";
    }

    bool isKTXDDS(vbase::StringView ext) { return ext == ".ktx" || ext == ".dds"; }

    bool isKTX2(vbase::StringView ext) { return ext == ".ktx2"; }

    bool isCompressedTexture(vbase::StringView ext) { return isKTXDDS(ext) || isKTX2(ext); }

    bool isValidTexture(vbase::StringView ext) { return isCompressedTexture(ext) || isEXR(ext) || isSTB(ext); }

    vasset::VTextureFileFormat textureFileFormatFromExtension(vbase::StringView ext)
    {
        if (ext == ".jpg")
            return vasset::VTextureFileFormat::eJPG;
        if (ext == ".jpeg")
            return vasset::VTextureFileFormat::eJPEG;
        if (ext == ".png")
            return vasset::VTextureFileFormat::ePNG;
        if (ext == ".tga")
            return vasset::VTextureFileFormat::eTGA;
        if (ext == ".bmp")
            return vasset::VTextureFileFormat::eBMP;
        if (ext == ".psd")
            return vasset::VTextureFileFormat::ePSD;
        if (ext == ".gif")
            return vasset::VTextureFileFormat::eGIF;
        if (ext == ".hdr")
            return vasset::VTextureFileFormat::eHDR;
        if (ext == ".pic")
            return vasset::VTextureFileFormat::ePIC;
        if (ext == ".exr")
            return vasset::VTextureFileFormat::eEXR;
        return vasset::VTextureFileFormat::eUnknown;
    }

    bool isValidModel(vbase::StringView ext)
    {
        return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".dae";
    }

    bool isValidGaussianSplat(vbase::StringView ext)
    {
        return ext == ".ply" || ext == ".spz" || ext == ".splat" || ext == ".ksplat";
    }

    bool isValidScene(vbase::StringView ext) { return ext == ".vscn"; }

    bool isValidSceneManifest(vbase::StringView ext) { return ext == ".vmanifest"; }

    bool isValidScriptLua(vbase::StringView ext) { return ext == ".lua"; }

    bool isValidSourceTextAsset(vbase::StringView ext)
    {
        return isValidScene(ext) || isValidSceneManifest(ext) || isValidScriptLua(ext);
    }

    bool isIgnoredAssetImportDirectory(const std::string& relPath)
    {
        // "imported" is generated output. "training" holds COLMAP/images/raw
        // point clouds for offline tools, not runtime assets for vasset import.
        return relPath == "imported" || relPath == "training";
    }

    bool isIgnoredAssetImportPath(const std::string& relPath)
    {
        return isIgnoredAssetImportDirectory(relPath) || relPath.rfind("imported/", 0) == 0 ||
               relPath.rfind("training/", 0) == 0;
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

    // Helper to calculate number of mip levels for given dimensions
    uint32_t calculateMipLevels(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return 1;
        return 1 + static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(std::max(width, height)))));
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

    bool writeAll(const std::filesystem::path& p, const std::vector<uint8_t>& data)
    {
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path());

        std::ofstream f(p, std::ios::binary);
        if (!f)
            return false;

        if (!data.empty())
            f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));

        return static_cast<bool>(f);
    }

    bool endsWith(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string gaussianSplatRegistryKey(const std::filesystem::path& path)
    {
        const std::string filename = path.filename().generic_string();
        if (endsWith(filename, ".compressed.ply"))
            return "compressed.ply";

        const std::string ext = path.extension().generic_string();
        if (ext == ".ply")
            return "ply";
        if (ext == ".spz")
            return "spz";
        if (ext == ".splat")
            return "splat";
        if (ext == ".ksplat")
            return "ksplat";
        return {};
    }

    std::string gaussianSplatAssetBaseName(const std::filesystem::path& path)
    {
        const std::string filename = path.filename().generic_string();
        if (endsWith(filename, ".compressed.ply"))
            return filename.substr(0, filename.size() - std::strlen(".compressed.ply"));
        return path.stem().generic_string();
    }

    bool gaussianSplatNeedsPlyAxisConversion(const std::string& formatKey)
    {
        return formatKey == "ply" || formatKey == "compressed.ply";
    }

    glm::vec3 convertGaussianPlyPositionToEngine(const glm::vec3& position)
    {
        // Standard 3DGS PLY data is commonly exported in an OpenCV/COLMAP-style
        // basis (X right, Y down, Z forward). Vultra's camera space uses Y up
        // and -Z forward, so rotate the cloud 180 degrees around X on import.
        return glm::vec3(position.x, -position.y, -position.z);
    }

    glm::quat convertGaussianPlyRotationToEngine(const glm::quat& sourceRotation)
    {
        const glm::quat basisRotation(0.0f, 1.0f, 0.0f, 0.0f);
        return glm::normalize(basisRotation * sourceRotation);
    }

    const std::vector<float>* findGaussianExtra(const gf::GaussianCloudIR&           cloud,
                                                std::initializer_list<const char*> names)
    {
        for (const char* name : names)
        {
            auto it = cloud.extras.find(name);
            if (it != cloud.extras.end())
                return &it->second;
        }
        return nullptr;
    }

    std::vector<float> copyPerPointFloatExtra(const gf::GaussianCloudIR&           cloud,
                                              int32_t                              pointCount,
                                              std::initializer_list<const char*> names)
    {
        const auto* values = findGaussianExtra(cloud, names);
        if (!values || values->size() != static_cast<size_t>(pointCount))
            return {};

        std::vector<float> out = *values;
        for (float& value : out)
        {
            if (!std::isfinite(value))
                value = 0.0f;
        }
        return out;
    }

    uint32_t gaussianExtraToUint(float value)
    {
        if (!std::isfinite(value))
            return 0u;
        if (value < 0.0f)
            return 0u;
        if (value >= static_cast<float>(std::numeric_limits<uint32_t>::max()))
            return std::numeric_limits<uint32_t>::max();
        return static_cast<uint32_t>(std::llround(value));
    }

    std::vector<uint32_t> copyPerPointUintExtra(const gf::GaussianCloudIR&           cloud,
                                                int32_t                              pointCount,
                                                std::initializer_list<const char*> names)
    {
        const auto* values = findGaussianExtra(cloud, names);
        if (!values || values->size() != static_cast<size_t>(pointCount))
            return {};

        std::vector<uint32_t> out;
        out.reserve(values->size());
        for (float value : *values)
            out.push_back(gaussianExtraToUint(value));
        return out;
    }

    vasset::VGaussianSplatLodData extractGaussianSplatLodData(const gf::GaussianCloudIR& cloud, int32_t pointCount)
    {
        vasset::VGaussianSplatLodData lod {};

        // Accept common research/exporter aliases. The main engine only needs
        // importance for Ordered CLOD, so a PLY with just score/weight/error can
        // already drive the renderer. Additional streams are kept as metadata.
        lod.importance = copyPerPointFloatExtra(
            cloud,
            pointCount,
            {"importance", "lod_importance", "lodScore", "lod_score", "lodscore", "score", "weight", "error"});
        lod.lodLevel = copyPerPointUintExtra(
            cloud,
            pointCount,
            {"lodLevel", "lod_level", "lod", "level", "depth", "clod_level"});
        lod.clusterId = copyPerPointUintExtra(
            cloud,
            pointCount,
            {"clusterId", "cluster_id", "cluster", "clusterIndex", "cluster_index", "nodeId", "node_id"});

        if (!lod.hasAnyData())
            return lod;

        lod.type = vasset::VGaussianSplatLodType::eFlatImportance;
        return lod;
    }

    size_t gaussianRestShFloatCountPerPoint(const int32_t shDegree)
    {
        const int32_t degree = std::clamp(shDegree, 0, 4);
        if (degree <= 0)
            return 0;
        return static_cast<size_t>(((degree + 1) * (degree + 1)) - 1) * 3ull;
    }

    template <typename T>
    void reorderGaussianPerPointVector(std::vector<T>& values, const std::vector<uint32_t>& order)
    {
        if (values.size() != order.size())
            return;

        std::vector<T> reordered;
        reordered.reserve(values.size());
        for (const uint32_t sourceIndex : order)
            reordered.push_back(values[sourceIndex]);
        values = std::move(reordered);
    }

    void physicallySortGaussianSplatByImportance(vasset::VGaussianSplat& splat)
    {
        const size_t pointCount = splat.splats.size();
        if (pointCount == 0 || splat.lod.importance.size() != pointCount)
            return;

        std::vector<uint32_t> order(pointCount);
        std::iota(order.begin(), order.end(), 0u);

        // Larger importance means earlier in the CLOD prefix. Stable ties keep
        // the original exporter order deterministic.
        std::stable_sort(order.begin(), order.end(), [&](const uint32_t a, const uint32_t b) {
            const float ia = splat.lod.importance[a];
            const float ib = splat.lod.importance[b];
            if (ia != ib)
                return ia > ib;
            return a < b;
        });

        if (std::is_sorted(order.begin(), order.end()))
            return;

        reorderGaussianPerPointVector(splat.splats, order);
        reorderGaussianPerPointVector(splat.lod.importance, order);
        reorderGaussianPerPointVector(splat.lod.lodLevel, order);
        reorderGaussianPerPointVector(splat.lod.clusterId, order);

        size_t shStride = gaussianRestShFloatCountPerPoint(splat.shDegree);
        if ((shStride == 0 || splat.sh.size() != pointCount * shStride) && !splat.sh.empty() &&
            splat.sh.size() % pointCount == 0)
        {
            shStride = splat.sh.size() / pointCount;
        }
        if (shStride > 0 && splat.sh.size() == pointCount * shStride)
        {
            std::vector<float> reorderedSh(splat.sh.size());
            for (size_t rank = 0; rank < pointCount; ++rank)
            {
                const size_t srcOffset = static_cast<size_t>(order[rank]) * shStride;
                const size_t dstOffset = rank * shStride;
                std::copy_n(splat.sh.data() + srcOffset, shStride, reorderedSh.data() + dstOffset);
            }
            splat.sh = std::move(reorderedSh);
        }
    }

    std::string importedAssetKeyFromRelativeSource(vbase::StringView relativeSourcePath, bool keepExtension = false)
    {
        std::filesystem::path keyPath {std::string(relativeSourcePath)};
        if (!keepExtension)
            keyPath.replace_extension();
        return keyPath.generic_string();
    }

    std::string stableShortHash(vbase::StringView value)
    {
        // FNV-1a keeps collision suffixes stable across platforms and toolchains.
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < value.size(); ++i)
        {
            hash ^= static_cast<uint8_t>(value[i]);
            hash *= 16777619u;
        }

        std::ostringstream out;
        out << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << hash;
        return out.str();
    }

    std::string importedAssetBaseName(vbase::StringView relativeSourcePath)
    {
        std::filesystem::path keyPath {std::string(relativeSourcePath)};
        return keyPath.stem().generic_string();
    }

    std::string importedAssetKeyForCookedOutput(const vasset::VAssetRegistry& registry,
                                                vasset::VAssetType            type,
                                                vbase::StringView             relativeSourcePath,
                                                vbase::StringView             assetBaseName = {})
    {
        const std::string relativeSourcePathStr(relativeSourcePath);
        const std::string baseName =
            assetBaseName.size() == 0 ? importedAssetBaseName(relativeSourcePath) : std::string(assetBaseName);

        auto makeImportedPath = [&](const std::string& key) {
            return registry.getImportedAssetPath(type, key, true);
        };

        const std::string candidatePath = makeImportedPath(baseName);
        const auto        candidateUUID = vbase::uuid_from_string_key(candidatePath);
        const auto        existingEntry = registry.lookup(candidateUUID);
        if (existingEntry.type == vasset::VAssetType::eUnknown || existingEntry.sourcePath == relativeSourcePathStr)
            return baseName;

        return baseName + "_" + stableShortHash(relativeSourcePath);
    }

    vasset::VAssetType inferSourceTextAssetType(const std::string& ext)
    {
        if (ext == ".vscn")
            return vasset::VAssetType::eScene;
        if (ext == ".vmanifest")
            return vasset::VAssetType::eSceneManifest;
        if (ext == ".lua")
            return vasset::VAssetType::eScriptLua;
        return vasset::VAssetType::eUnknown;
    }

    vbase::Result<vbase::UUID, vasset::AssetError> importSourceTextAsset(vasset::VAssetRegistry& registry,
                                                                         vbase::StringView       filePath,
                                                                         bool                    forceReimport,
                                                                         vasset::VAssetType      type)
    {
        namespace fs = std::filesystem;

        const std::string filePathStr(filePath);
        fs::path          osPath(filePathStr);
        if (!fs::exists(osPath) || fs::is_directory(osPath))
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eImportFailed);

        const std::string relativeSrcPath      = registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string relativeImportedPath =
            registry.getImportedAssetPath(type, importedAssetKeyFromRelativeSource(relativeSrcPath, true), true);

        auto lookupUUID = vbase::uuid_from_string_key(relativeImportedPath);
        auto entry      = registry.lookup(lookupUUID);
        if (entry.type != vasset::VAssetType::eUnknown && !forceReimport)
            return vbase::Result<vbase::UUID, vasset::AssetError>::ok(lookupUUID);

        const auto srcBytes = readAll(osPath);
        if (srcBytes.empty() && !fs::exists(osPath))
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eImportFailed);

        const fs::path importedPath = fs::path(registry.getAssetRootPath()) / relativeImportedPath;
        if (!writeAll(importedPath, srcBytes))
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eIOError);

        vasset::VImport vimport {};
        vimport.importer = vasset::toString(type);
        vimport.uid      = lookupUUID;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;

        fs::path importSidecarPath = fs::path(registry.getAssetRootPath()) / relativeImportedPath;
        importSidecarPath.replace_extension(".vimport");
        auto sr_import = saveVImport(vimport, importSidecarPath.generic_string());
        if (!sr_import)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(sr_import.error());

        auto rr = registry.registerAsset(lookupUUID, relativeSrcPath, relativeImportedPath, type);
        if (!rr)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(rr.error());

        return vbase::Result<vbase::UUID, vasset::AssetError>::ok(lookupUUID);
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
                    std::cout << "Warning: Unsupported material property type: " << prop->mType
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

    vbase::Result<vbase::UUID, AssetError>
    VTextureImporter::importTexture(vbase::StringView filePath, VTexture& outTexture, bool forceReimport) const
    {
        // ------------------------------------------------------------
        // RAII guards
        // ------------------------------------------------------------
        struct PixelGuard
        {
            void* p = nullptr;
            ~PixelGuard()
            {
                if (p)
                    stbi_image_free(p);
            }
        } pixelGuard;

        struct KtxGuard
        {
            ktxTexture2* p = nullptr;
            ~KtxGuard()
            {
                if (p)
                    ktxTexture_Destroy(ktxTexture(p));
            }
        } ktxGuard;

        // ------------------------------------------------------------
        // Check path
        // ------------------------------------------------------------
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        // ------------------------------------------------------------
        // Check registry
        // ------------------------------------------------------------
        const std::string relativeSrcPath = m_Registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string relativeImportedPath =
            m_Registry.getImportedAssetPath(
                VAssetType::eTexture,
                importedAssetKeyForCookedOutput(m_Registry, VAssetType::eTexture, relativeSrcPath),
                true);

        const auto lookupUUID = vbase::uuid_from_string_key(relativeImportedPath);
        auto       entry      = m_Registry.lookup(lookupUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport)
        {
            // Load existing texture
            std::cout << "Texture already imported: " << entry.sourcePath << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
        }

        // Set UUID
        outTexture.uuid = vbase::uuid_from_string_key(relativeImportedPath);

        // Default target format
        auto targetFormat = m_Options.targetTextureFileFormat;

        // ------------------------------------------------------------
        // Extension dispatch
        // ------------------------------------------------------------
        std::string ext = osPath.extension().generic_string();

        // ======================== KTX / DDS ========================
        if (isKTXDDS(ext))
        {
            auto pathStr = osPath.generic_string();

#ifndef _WIN32
            // Ensure the path is in the correct format for KTX/DDS parsing
            // \\ -> /
            std::replace(pathStr.begin(), pathStr.end(), '\\', '/');
#endif

            auto path = std::filesystem::path {pathStr};

            auto fileBytes = readAll(path);
            if (fileBytes.empty())
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

            ddsktx_texture_info tc {0};

            if (!ddsktx_parse(&tc, fileBytes.data(), static_cast<int>(fileBytes.size()), nullptr))
            {
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
            }

            // Fill outTexture
            outTexture.width       = static_cast<uint32_t>(tc.width);
            outTexture.height      = static_cast<uint32_t>(tc.height);
            outTexture.depth       = static_cast<uint32_t>(tc.depth);
            outTexture.mipLevels   = tc.num_mips;
            outTexture.arrayLayers = tc.num_layers;
            outTexture.isCubemap   = (tc.flags & DDSKTX_TEXTURE_FLAG_CUBEMAP) != 0;
            outTexture.type        = VTextureDimension::e2D;
            outTexture.format      = toVTextureFormat(tc.format);
            outTexture.fileFormat  = (ext == ".ktx") ? VTextureFileFormat::eKTX : VTextureFileFormat::eDDS;
            outTexture.compressedBasisU = false;

            outTexture.data.assign(fileBytes.begin(), fileBytes.end());

            goto SUCCESS;
        }

        // ======================== KTX2 ========================
        if (isKTX2(ext))
        {
            std::ifstream file(std::string(filePath), std::ios::binary | std::ios::ate);
            if (!file)
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);

            std::vector<char> bytes(size);
            if (!file.read(bytes.data(), size))
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

            if (ktxTexture2_CreateFromMemory(reinterpret_cast<const ktx_uint8_t*>(bytes.data()),
                                             size,
                                             KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
                                             &ktxGuard.p) != KTX_SUCCESS)
            {
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
            }

            auto* kTexture = ktxGuard.p;

            // Fill outTexture
            outTexture.width       = kTexture->baseWidth;
            outTexture.height      = kTexture->baseHeight;
            outTexture.depth       = kTexture->baseDepth;
            outTexture.mipLevels   = kTexture->numLevels;
            outTexture.arrayLayers = kTexture->numLayers;
            outTexture.isCubemap   = kTexture->numFaces == 6;
            outTexture.type        = static_cast<VTextureDimension>(kTexture->numDimensions);

            outTexture.format           = static_cast<VTextureFormat>(kTexture->vkFormat);
            outTexture.fileFormat       = VTextureFileFormat::eKTX2;
            outTexture.compressedBasisU = ktxTexture2_NeedsTranscoding(kTexture) != 0;

            outTexture.data.assign(bytes.begin(), bytes.end());

            goto SUCCESS;
        }

        // ======================== RAW IMAGE ========================
        {
            int  width = 0, height = 0, channels = 0;
            bool hdr = false;

            // ---------- EXR ----------
            if (isEXR(ext))
            {
                const char* err = nullptr;
                float*      img = nullptr;

                if (LoadEXR(&img, &width, &height, osPath.generic_string().c_str(), &err) != TINYEXR_SUCCESS)
                {
                    if (err)
                    {
                        std::cerr << "Failed to load EXR image: " << err << std::endl;
                        FreeEXRErrorMessage(err);
                    }
                    return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                }

                pixelGuard.p = img;

                targetFormat = VTextureFileFormat::eEXR;
                channels     = 4; // RGBA
                hdr          = true;
            }

            // ---------- STB ----------
            else if (isSTB(ext))
            {
                // NOLINTBEGIN
                auto* file = fopen(filePath.data(), "rb");
                // NOLINTEND
                if (!file)
                    return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

                hdr = stbi_is_hdr_from_file(file);

                stbi_set_flip_vertically_on_load(m_Options.flipY ? 1 : 0);

                if (hdr)
                {
                    pixelGuard.p = stbi_loadf_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
                    targetFormat = VTextureFileFormat::eHDR;
                }
                else
                {
                    pixelGuard.p = stbi_load_from_file(file, &width, &height, &channels, STBI_rgb_alpha);
                }

                fclose(file);

                if (!pixelGuard.p)
                    return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
            }
            else
            {
                std::cerr << "Unsupported image format: " << ext << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
            }

            // Treat everything as RGBA
            channels = 4;

            auto srcSize = hdr ? width * height * channels * sizeof(float) : width * height * channels;

            auto targetTextureFormat = hdr ? VTextureFormat::eRGBA32F : VTextureFormat::eRGBA8;
            std::error_code fileSizeEc;
            const auto      sourceFileBytes = std::filesystem::file_size(osPath, fileSizeEc);

            if (targetFormat == VTextureFileFormat::eKTX || targetFormat == VTextureFileFormat::eDDS)
            {
                std::cout << "Warning: Target texture file format KTX or DDS "
                             "is not supported for image files. Defaulting to KTX2."
                          << std::endl;

                targetFormat = VTextureFileFormat::eKTX2;
            }

            bool shouldCompressWithBasisU = false;
            if (targetFormat == VTextureFileFormat::eKTX2 && !hdr)
            {
                shouldCompressWithBasisU = true;
                if (m_Options.compressOnlyLargeTextures)
                {
                    const uint32_t longestEdge = static_cast<uint32_t>(std::max(width, height));
                    const bool     largeEnoughResolution = longestEdge >= m_Options.basisUCompressMinDimension;
                    const bool     largeEnoughSourceSize =
                        !fileSizeEc && sourceFileBytes >= m_Options.basisUCompressMinSourceBytes;
                    shouldCompressWithBasisU = largeEnoughResolution && largeEnoughSourceSize;

                    if (!shouldCompressWithBasisU)
                    {
                        std::cout << "Keeping original encoded payload for small texture ("
                                  << osPath.filename().generic_string() << ", longestEdge=" << longestEdge
                                  << ", sourceBytes=";
                        if (fileSizeEc)
                            std::cout << "unknown";
                        else
                            std::cout << sourceFileBytes;
                        std::cout << ", thresholds=" << m_Options.basisUCompressMinDimension << "px/"
                                  << m_Options.basisUCompressMinSourceBytes << "B)" << std::endl;
                    }
                }

                if (!shouldCompressWithBasisU)
                    targetFormat = textureFileFormatFromExtension(ext);
            }

            // ---------- Not KTX2: direct store ----------
            if (targetFormat != VTextureFileFormat::eKTX2)
            {
                auto fileBytes = readAll(osPath);

                outTexture.width       = width;
                outTexture.height      = height;
                outTexture.depth       = 1;
                outTexture.mipLevels   = 1;
                outTexture.arrayLayers = 1;
                outTexture.isCubemap   = false;
                outTexture.type        = VTextureDimension::e2D;
                outTexture.format      = targetTextureFormat;
                outTexture.fileFormat  = targetFormat;
                outTexture.compressedBasisU = false;

                outTexture.data.assign(fileBytes.begin(), fileBytes.end());

                goto SUCCESS;
            }

            // ---------- Convert to KTX2 ----------
            ktxTextureCreateInfo ci {};
            ci.baseWidth       = width;
            ci.baseHeight      = height;
            ci.baseDepth       = 1;
            ci.numLevels       = m_Options.generateMipmaps ? calculateMipLevels(width, height) : 1;
            ci.numLayers       = 1;
            ci.numFaces        = 1;
            ci.numDimensions   = 2;
            ci.isArray         = KTX_FALSE;
            ci.generateMipmaps = KTX_FALSE; // We generate mipmaps manually
            ci.vkFormat        = static_cast<uint32_t>(targetTextureFormat);

            if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktxGuard.p) != KTX_SUCCESS)
            {
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
            }

            // Set image data for base level
            if (ktxTexture_SetImageFromMemory(
                    ktxTexture(ktxGuard.p), 0, 0, 0, reinterpret_cast<const ktx_uint8_t*>(pixelGuard.p), srcSize) !=
                KTX_SUCCESS)
            {
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
            }

            // Generate mipmaps if needed
            if (ci.numLevels > 1)
            {
                uint32_t             mipW    = static_cast<uint32_t>(width);
                uint32_t             mipH    = static_cast<uint32_t>(height);
                const void*          prevPtr = pixelGuard.p;
                std::vector<uint8_t> prev8;
                std::vector<float>   prevf;

                for (uint32_t level = 1; level < ci.numLevels; ++level)
                {
                    uint32_t nextW = mipW > 1 ? mipW / 2 : 1;
                    uint32_t nextH = mipH > 1 ? mipH / 2 : 1;

                    if (hdr)
                    {
                        std::vector<float> nextf(static_cast<size_t>(nextW) * nextH * channels);
                        if (!stbir_resize_float_linear(reinterpret_cast<const float*>(prevPtr),
                                                       static_cast<int>(mipW),
                                                       static_cast<int>(mipH),
                                                       0,
                                                       nextf.data(),
                                                       static_cast<int>(nextW),
                                                       static_cast<int>(nextH),
                                                       0,
                                                       static_cast<stbir_pixel_layout>(STBIR_4CHANNEL)))
                        {
                            std::cerr << "Failed to resize float image for mip generation." << std::endl;
                            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                        }

                        ktx_size_t nextSize = static_cast<ktx_size_t>(nextf.size() * sizeof(float));
                        if (ktxTexture_SetImageFromMemory(ktxTexture(ktxGuard.p),
                                                          level,
                                                          0,
                                                          0,
                                                          reinterpret_cast<const ktx_uint8_t*>(nextf.data()),
                                                          nextSize) != KTX_SUCCESS)
                        {
                            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                        }

                        prevf   = std::move(nextf);
                        prevPtr = prevf.data();
                    }
                    else
                    {
                        std::vector<uint8_t> nextu(static_cast<size_t>(nextW) * nextH * channels);
                        if (!stbir_resize_uint8_srgb(reinterpret_cast<const unsigned char*>(prevPtr),
                                                     static_cast<int>(mipW),
                                                     static_cast<int>(mipH),
                                                     0,
                                                     nextu.data(),
                                                     static_cast<int>(nextW),
                                                     static_cast<int>(nextH),
                                                     0,
                                                     static_cast<stbir_pixel_layout>(STBIR_4CHANNEL)))
                        {
                            std::cerr << "Failed to resize uint8 image for mip generation." << std::endl;
                            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                        }

                        ktx_size_t nextSize = static_cast<ktx_size_t>(nextu.size());
                        if (ktxTexture_SetImageFromMemory(ktxTexture(ktxGuard.p),
                                                          level,
                                                          0,
                                                          0,
                                                          reinterpret_cast<const ktx_uint8_t*>(nextu.data()),
                                                          nextSize) != KTX_SUCCESS)
                        {
                            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                        }

                        prev8   = std::move(nextu);
                        prevPtr = prev8.data();
                    }

                    mipW = nextW;
                    mipH = nextH;
                }
            }

            // ---------- BasisU Compression ----------
            if (shouldCompressWithBasisU)
            {
                ktxBasisParams params {};
                params.structSize       = sizeof(ktxBasisParams);
                params.uastc            = m_Options.uastc;
                params.noSSE            = m_Options.noSSE;
                params.qualityLevel     = m_Options.qualityLevel;
                params.compressionLevel = m_Options.compressionLevel;
                params.threadCount      = m_Options.basisUThreadCount;

                // Diagnostic: log vkFormat and mip count
                std::cout << "BasisU: vkFormat=" << ci.vkFormat << " numLevels=" << ci.numLevels << "\n";

                KTX_error_code res = ktxTexture2_CompressBasisEx(ktxGuard.p, &params);
                if (res != KTX_SUCCESS)
                {
                    std::cerr << "Warning: Failed to compress texture with BasisU. ktx result=" << res << std::endl;

                    // Print expected level sizes to help track mismatches
                    ktx_uint32_t lvlW = ci.baseWidth;
                    ktx_uint32_t lvlH = ci.baseHeight;
                    for (ktx_uint32_t lvl = 0; lvl < ci.numLevels; ++lvl)
                    {
                        ktx_size_t expectedSize =
                            static_cast<ktx_size_t>(lvlW) * lvlH * 4 * (hdr ? sizeof(float) : sizeof(uint8_t));
                        std::cerr << "  level " << lvl << " expected bytes=" << expectedSize << " w=" << lvlW
                                  << " h=" << lvlH << std::endl;
                        lvlW = lvlW > 1 ? lvlW / 2 : 1;
                        lvlH = lvlH > 1 ? lvlH / 2 : 1;
                    }
                }
            }
            else
            {
                if (hdr)
                {
                    std::cout << "Skipping BasisU compression for HDR/float texture (vkFormat=" << ci.vkFormat << ")"
                              << std::endl;
                }
            }

            // ---------- Write to memory ----------
            ktx_size_t   size = 0;
            ktx_uint8_t* mem  = nullptr;
            if (ktxTexture_WriteToMemory(ktxTexture(ktxGuard.p), &mem, &size) != KTX_SUCCESS)
            {
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
            }

            // ---------- Fill outTexture ----------
            outTexture.width       = width;
            outTexture.height      = height;
            outTexture.depth       = ci.baseDepth;
            outTexture.mipLevels   = ci.numLevels;
            outTexture.arrayLayers = ci.numLayers;
            outTexture.isCubemap   = ci.numFaces == 6;
            outTexture.type        = static_cast<VTextureDimension>(ci.numDimensions);
            outTexture.format      = targetTextureFormat;
            outTexture.fileFormat  = VTextureFileFormat::eKTX2;
            outTexture.compressedBasisU = ktxTexture2_NeedsTranscoding(ktxGuard.p) != 0;

            outTexture.data.assign(mem, mem + size);
        }

    SUCCESS:
        const std::string importedPath =
            (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeImportedPath).generic_string();

        auto sr_tex = saveTexture(outTexture, importedPath);
        if (!sr_tex)
        {
            std::cerr << "Failed to save texture." << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::err(sr_tex.error());
        }

        // Save VImport
        VImport vimport {};
        vimport.importer = toString(VAssetType::eTexture);
        vimport.uid      = outTexture.uuid;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;
        // TODO: params
        auto sr_import = saveVImport(vimport, osPath.replace_extension(".vimport").generic_string());
        if (!sr_import)
            return vbase::Result<vbase::UUID, AssetError>::err(sr_import.error());

        auto rr =
            m_Registry.registerAsset(outTexture.uuid, relativeSrcPath, relativeImportedPath, VAssetType::eTexture);
        if (!rr)
            return vbase::Result<vbase::UUID, AssetError>::err(rr.error());
        return vbase::Result<vbase::UUID, AssetError>::ok(outTexture.uuid);
    }

    VMeshImporter::VMeshImporter(VAssetRegistry& registry) : m_Registry(registry), m_TextureImporter(registry) {}

    VMeshImporter& VMeshImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    vbase::Result<vbase::UUID, AssetError>
    VMeshImporter::importMesh(vbase::StringView filePath, VMesh& outMesh, bool forceReimport)
    {
        // Check path
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        // Check registry
        const std::string relativeSrcPath = m_Registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string relativeImportedPath =
            m_Registry.getImportedAssetPath(VAssetType::eMesh,
                                            importedAssetKeyForCookedOutput(m_Registry, VAssetType::eMesh, relativeSrcPath),
                                            true);

        auto lookupUUID = vbase::uuid_from_string_key(relativeImportedPath);
        auto entry      = m_Registry.lookup(lookupUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport)
        {
            // Load existing mesh
            std::cout << "Mesh already imported: " << entry.sourcePath << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
        }

        // Set UUID
        outMesh.uuid = vbase::uuid_from_string_key(relativeImportedPath);

        m_FilePath = filePath;

        // Set Assimp process flags
        // Ignore scene graph, as we flatten everything into a single VMesh
        unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_PreTransformVertices |
                             aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals | aiProcess_GenUVCoords;

        Assimp::Importer importer {};
        const aiScene*   scene = importer.ReadFile(filePath.data(), flags);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode || !scene->HasMeshes())
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        outMesh.name           = scene->mRootNode->mName.C_Str();
        outMesh.sourceFileName = osPath.stem().generic_string();

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
            (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeImportedPath).generic_string();

        auto sr_mesh = saveMesh(outMesh, importedPath, 3);
        if (!sr_mesh)
        {
            std::cerr << "Failed to save mesh." << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::err(sr_mesh.error());
        }

        // Save VImport
        VImport vimport {};
        vimport.importer = toString(VAssetType::eMesh);
        vimport.uid      = outMesh.uuid;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;
        // TODO: params
        auto sr_import = saveVImport(vimport, osPath.replace_extension(".vimport").generic_string());
        if (!sr_import)
            return vbase::Result<vbase::UUID, AssetError>::err(sr_import.error());

        auto rr = m_Registry.registerAsset(outMesh.uuid, relativeSrcPath, relativeImportedPath, VAssetType::eMesh);
        if (!rr)
            return vbase::Result<vbase::UUID, AssetError>::err(rr.error());
        return vbase::Result<vbase::UUID, AssetError>::ok(outMesh.uuid);
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
                std::cout << "Warning: Non-triangulated face found in mesh: " << mesh->mName.C_Str() << std::endl;
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

            VMaterial vMat {};
            processMaterial(aiMat, vMat);
            outMesh.materials.push_back(vMat);
        }

        outMesh.subMeshes.push_back(subMesh);
        outMesh.vertexCount += mesh->mNumVertices;
    }

    void VMeshImporter::processMaterial(const aiMaterial* material, VMaterial& outMaterial) const
    {
        if (!material)
            return;

        outMaterial = {}; // reset

        // ------------------------------------------------------------
        // Lossless capture: Assimp properties
        // ------------------------------------------------------------
        outMaterial.properties.clear();
        outMaterial.properties.reserve(material->mNumProperties);

        auto toPropType = [](aiPropertyTypeInfo t) -> VMaterialPropertyType {
            switch (t)
            {
                case aiPTI_Float:
                    return VMaterialPropertyType::eFloat;
                case aiPTI_Double:
                    return VMaterialPropertyType::eDouble;
                case aiPTI_String:
                    return VMaterialPropertyType::eString;
                case aiPTI_Integer:
                    return VMaterialPropertyType::eInteger;
                case aiPTI_Buffer:
                    return VMaterialPropertyType::eBuffer;
                default:
                    return VMaterialPropertyType::eUnknown;
            }
        };

        for (unsigned i = 0; i < material->mNumProperties; ++i)
        {
            const aiMaterialProperty* prop = material->mProperties[i];
            if (!prop)
                continue;

            VMaterialProperty p {};
            p.key      = prop->mKey.C_Str();
            p.semantic = prop->mSemantic;
            p.index    = prop->mIndex;
            p.type     = toPropType(prop->mType);

            if (prop->mType == aiPTI_String)
            {
                aiString value;
                if (material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, value) == aiReturn_SUCCESS)
                {
                    const char*  cstr = value.C_Str();
                    const size_t len  = std::strlen(cstr);
                    p.data.assign(reinterpret_cast<const uint8_t*>(cstr), reinterpret_cast<const uint8_t*>(cstr) + len);
                }
            }
            else
            {
                if (prop->mData && prop->mDataLength)
                {
                    p.data.resize(prop->mDataLength);
                    std::memcpy(p.data.data(), prop->mData, prop->mDataLength);
                }
            }

            outMaterial.properties.push_back(std::move(p));
        }

        auto hasKeySubstring = [&](const char* sub) {
            for (const auto& p : outMaterial.properties)
            {
                if (p.key.find(sub) != std::string::npos)
                    return true;
            }
            return false;
        };

        // ------------------------------------------------------------
        // Lossless capture: texture bindings
        // ------------------------------------------------------------
        outMaterial.textures.clear();
        for (int tt = static_cast<int>(aiTextureType_NONE) + 1; tt <= static_cast<int>(aiTextureType_UNKNOWN); ++tt)
        {
            const auto     type  = static_cast<aiTextureType>(tt);
            const unsigned count = material->GetTextureCount(type);
            if (count == 0)
                continue;

            for (unsigned ti = 0; ti < count; ++ti)
            {
                aiString         path;
                aiTextureMapping mapping    = aiTextureMapping_UV;
                unsigned         uvIndex    = 0;
                float            blend      = 1.0f;
                aiTextureOp      op         = aiTextureOp_Multiply;
                aiTextureMapMode mapMode[3] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};

                if (material->GetTexture(type, ti, &path, &mapping, &uvIndex, &blend, &op, mapMode) != aiReturn_SUCCESS)
                    continue;

                VMaterialTextureBinding tb {};
                tb.type     = static_cast<uint16_t>(tt);
                tb.index    = static_cast<uint16_t>(ti);
                tb.uvIndex  = static_cast<uint8_t>(uvIndex);
                tb.mapping  = static_cast<uint8_t>(mapping);
                tb.op       = static_cast<uint8_t>(op);
                tb.mapModeU = static_cast<uint8_t>(mapMode[0]);
                tb.mapModeV = static_cast<uint8_t>(mapMode[1]);
                tb.blend    = blend;

                // Import texture file into vasset pipeline and store UUID.
                if (path.length > 0 && path.data[0] == '*')
                {
                    // Embedded texture - keep empty ref; raw properties still preserve the info.
                    tb.texture = {};
                }
                else
                {
                    tb.texture = loadTexture(material, type, ti);
                }

                outMaterial.textures.push_back(tb);
            }
        }

        // ------------------------------------------------------------
        // Model + feature flags (hints)
        // ------------------------------------------------------------
        const bool isUnlit      = hasKeySubstring("unlit") || hasKeySubstring("Unlit");
        const bool hasSpecGloss = hasKeySubstring("SpecularGlossiness") || hasKeySubstring("specularGloss") ||
                                  hasKeySubstring("pbrSpecularGlossiness");

        outMaterial.features = VMaterialFeatureFlags::eFeature_None;
        if (hasKeySubstring("clearcoat"))
            outMaterial.features |= VMaterialFeatureFlags::eFeature_ClearCoat;
        if (hasKeySubstring("transmission"))
            outMaterial.features |= VMaterialFeatureFlags::eFeature_Transmission;
        if (hasKeySubstring("sheen"))
            outMaterial.features |= VMaterialFeatureFlags::eFeature_Sheen;
        if (hasKeySubstring("volume"))
            outMaterial.features |= VMaterialFeatureFlags::eFeature_Volume;
        if (hasKeySubstring("specular"))
            outMaterial.features |= VMaterialFeatureFlags::eFeature_Specular;
        if (hasKeySubstring("iridescence"))
            outMaterial.features |= VMaterialFeatureFlags::eFeature_Iridescence;
        if (hasKeySubstring("anisotropy"))
            outMaterial.features |= VMaterialFeatureFlags::eFeature_Anisotropy;

        // ------------------------------------------------------------
        // Core extraction (fast path)
        // ------------------------------------------------------------
        auto props = parseMaterialProperties(material);

        // Name
        aiString name;
        if (tryGet(props, AI_MATKEY_NAME, name))
            outMaterial.name = name.C_Str();

        if (isUnlit)
        {
            outMaterial.model      = VMaterialModel::eUnlit;
            outMaterial.core.unlit = {};

            aiColor4D baseColor(1, 1, 1, 1);
            if (tryGet(props, AI_MATKEY_BASE_COLOR, baseColor))
                outMaterial.core.unlit.color = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
            else
            {
                aiColor3D kd(1, 1, 1);
                if (tryGet(props, AI_MATKEY_COLOR_DIFFUSE, kd))
                    outMaterial.core.unlit.color = glm::vec4(kd.r, kd.g, kd.b, 1.0f);
            }

            outMaterial.core.unlit.colorTexture = loadTexture(material, aiTextureType_DIFFUSE);
        }
        else if (hasSpecGloss)
        {
            outMaterial.model      = VMaterialModel::ePBRSpecularGlossiness;
            outMaterial.core.pbrSG = {};

            aiColor4D diff(1, 1, 1, 1);
            aiColor3D spec(1, 1, 1);
            float     gloss = 1.0f;

            tryGet(props, AI_MATKEY_COLOR_DIFFUSE, diff);
            tryGet(props, AI_MATKEY_COLOR_SPECULAR, spec);

            // Assimp doesn't have a stable key for glossiness; best-effort from shininess.
            float Ns = 0.0f;
            if (tryGet(props, AI_MATKEY_SHININESS, Ns))
                gloss = glm::clamp(Ns / 1000.0f, 0.0f, 1.0f);

            outMaterial.core.pbrSG.diffuseColor     = glm::vec4(diff.r, diff.g, diff.b, diff.a);
            outMaterial.core.pbrSG.specularFactor   = glm::vec3(spec.r, spec.g, spec.b);
            outMaterial.core.pbrSG.glossinessFactor = glm::clamp(gloss, 0.0f, 1.0f);

            outMaterial.core.pbrSG.diffuseTexture            = loadTexture(material, aiTextureType_DIFFUSE);
            outMaterial.core.pbrSG.specularGlossinessTexture = loadTexture(material, aiTextureType_SPECULAR);
        }
        else
        {
            outMaterial.model      = VMaterialModel::ePBRMetallicRoughness;
            outMaterial.core.pbrMR = {};

            // Read colors
            aiColor3D kd(1, 1, 1), ks(0, 0, 0), ke(0, 0, 0), ka(0, 0, 0);
            tryGet(props, AI_MATKEY_COLOR_DIFFUSE, kd);
            tryGet(props, AI_MATKEY_COLOR_SPECULAR, ks);
            tryGet(props, AI_MATKEY_COLOR_EMISSIVE, ke);
            tryGet(props, AI_MATKEY_COLOR_AMBIENT, ka);

            // Scalars
            float Ns = 0.0f, d = 1.0f, Ni = 1.5f, emissiveIntensity = 1.0f;
            tryGet(props, AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity);
            tryGet(props, AI_MATKEY_SHININESS, Ns);
            tryGet(props, AI_MATKEY_OPACITY, d);
            tryGet(props, AI_MATKEY_REFRACTI, Ni);

            // Alpha mode / cutoff
            aiString alphaMode;
            if (tryGet(props, AI_MATKEY_GLTF_ALPHAMODE, alphaMode))
            {
                auto alphaModeStr = std::string(alphaMode.C_Str());
                if (alphaModeStr == "MASK")
                    outMaterial.core.pbrMR.alphaMode = VMaterialAlphaMode::eMask;
                else if (alphaModeStr == "BLEND")
                    outMaterial.core.pbrMR.alphaMode = VMaterialAlphaMode::eBlend;
                else
                    outMaterial.core.pbrMR.alphaMode = VMaterialAlphaMode::eOpaque;
            }
            float alphaCutoff = 0.5f;
            if (tryGet(props, AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff))
                outMaterial.core.pbrMR.alphaCutoff = alphaCutoff;

            // Blend mode
            aiBlendMode blendMode = aiBlendMode_Default;
            if (tryGet(props, AI_MATKEY_BLEND_FUNC, blendMode))
                outMaterial.core.pbrMR.blendMode = toBlendMode(blendMode);
            else
                outMaterial.core.pbrMR.blendMode =
                    d < 1.0f ? toBlendMode(aiBlendMode::aiBlendMode_Default) : VMaterialBlendMode::eNone;

            // Defaults from classic shading
            outMaterial.core.pbrMR.baseColor = glm::vec4(kd.r, kd.g, kd.b, 1.0f);
            outMaterial.core.pbrMR.opacity   = d;
            outMaterial.core.pbrMR.metallicFactor =
                std::clamp((0.2126f * ks.r + 0.7152f * ks.g + 0.0722f * ks.b - 0.04f) / (1.0f - 0.04f), 0.0f, 1.0f);
            outMaterial.core.pbrMR.roughnessFactor        = glm::clamp(std::sqrt(2.0f / (Ns + 2.0f)), 0.04f, 1.0f);
            outMaterial.core.pbrMR.emissiveColorIntensity = glm::vec4(ke.r, ke.g, ke.b, emissiveIntensity);
            outMaterial.core.pbrMR.ior                    = Ni;
            outMaterial.core.pbrMR.ambientColor           = glm::vec4(ka.r, ka.g, ka.b, 1.0f);

            // glTF overrides
            aiColor4D baseColorFactorPBR(1, 1, 1, 1);
            float     metallicFactorPBR  = 1.0f;
            float     roughnessFactorPBR = 1.0f;
            if (tryGet(props, AI_MATKEY_BASE_COLOR, baseColorFactorPBR))
                outMaterial.core.pbrMR.baseColor =
                    glm::vec4(baseColorFactorPBR.r, baseColorFactorPBR.g, baseColorFactorPBR.b, baseColorFactorPBR.a);
            if (tryGet(props, AI_MATKEY_METALLIC_FACTOR, metallicFactorPBR))
                outMaterial.core.pbrMR.metallicFactor = metallicFactorPBR;
            if (tryGet(props, AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactorPBR))
                outMaterial.core.pbrMR.roughnessFactor = roughnessFactorPBR;

            // Double sided
            bool doubleSided = false;
            if (tryGet(props, AI_MATKEY_TWOSIDED, doubleSided))
                outMaterial.core.pbrMR.doubleSided = doubleSided;

            // Core textures
            outMaterial.core.pbrMR.baseColorTexture        = loadTexture(material, aiTextureType_DIFFUSE);
            outMaterial.core.pbrMR.alphaTexture            = loadTexture(material, aiTextureType_OPACITY);
            outMaterial.core.pbrMR.metallicTexture         = loadTexture(material, aiTextureType_METALNESS);
            outMaterial.core.pbrMR.roughnessTexture        = loadTexture(material, aiTextureType_DIFFUSE_ROUGHNESS);
            outMaterial.core.pbrMR.specularTexture         = loadTexture(material, aiTextureType_SPECULAR);
            outMaterial.core.pbrMR.normalTexture           = loadTexture(material, aiTextureType_NORMALS);
            outMaterial.core.pbrMR.ambientOcclusionTexture = loadTexture(material, aiTextureType_LIGHTMAP);
            outMaterial.core.pbrMR.emissiveTexture         = loadTexture(material, aiTextureType_EMISSIVE);
            outMaterial.core.pbrMR.metallicRoughnessTexture =
                loadTexture(material, aiTextureType_GLTF_METALLIC_ROUGHNESS);
        }

        if (outMaterial.name.empty())
            outMaterial.name = "Material";

        std::cout << "Imported material: " << outMaterial.name << " (model=" << static_cast<int>(outMaterial.model)
                  << ", props=" << outMaterial.properties.size() << ", textures=" << outMaterial.textures.size() << ")"
                  << std::endl;
    }

    VTextureRef VMeshImporter::loadTexture(const aiMaterial* material, aiTextureType type) const
    {
        return loadTexture(material, type, 0);
    }

    VTextureRef VMeshImporter::loadTexture(const aiMaterial* material, aiTextureType type, unsigned index) const
    {
        if (!material)
            return {};

        if (material->GetTextureCount(type) > index)
        {
            aiString str;
            if (material->GetTexture(type, index, &str) == aiReturn_SUCCESS)
            {
                VTexture              texture {};
                std::filesystem::path relativePath(str.C_Str());

                std::filesystem::path texPath = std::filesystem::path(m_FilePath).parent_path() / relativePath;

                auto tr = m_TextureImporter.importTexture(texPath.generic_string(), texture);
                if (!tr)
                {
                    std::cerr << "Failed to import texture: " << texPath << std::endl;
                    return {};
                }

                const auto uuid = tr.value();

                // Optional verbose log
                std::cout << "  Loaded texture: " << texPath << ", uuid: " << vbase::to_string(uuid) << std::endl;

                return VTextureRef {uuid};
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
            size_t meshletCount =
                meshopt_buildMeshlets(meshletsTemp.data(),
                                      sub.meshletGroup.meshletVertices.data() + baseVertOffset,
                                      sub.meshletGroup.meshletTriangles.data() + baseTriOffset,
                                      subIndices,
                                      subCount,
                                      reinterpret_cast<const float*>(outMesh.positions.data() + sub.vertexOffset),
                                      sub.vertexCount,
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

                // Optimize meshlets
                meshopt_optimizeMeshlet(&sub.meshletGroup.meshletVertices[dst.vertexOffset],
                                        &sub.meshletGroup.meshletTriangles[dst.triangleOffset],
                                        dst.triangleCount,
                                        dst.vertexCount);

                // Compute bounding sphere and cone for culling
                auto bounds = meshopt_computeMeshletBounds(
                    &sub.meshletGroup.meshletVertices[dst.vertexOffset],
                    &sub.meshletGroup.meshletTriangles[dst.triangleOffset],
                    dst.triangleCount,
                    reinterpret_cast<const float*>(outMesh.positions.data() + sub.vertexOffset),
                    sub.vertexCount,
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

    // ─── VGaussianSplatImporter ──────────────────────────────────────────────────

    VGaussianSplatImporter::VGaussianSplatImporter(VAssetRegistry& registry) : m_Registry(registry) {}

    VGaussianSplatImporter& VGaussianSplatImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    vbase::Result<vbase::UUID, AssetError> VGaussianSplatImporter::importGaussianSplat(vbase::StringView filePath,
                                                                                       VGaussianSplat&   outSplat,
                                                                                       bool forceReimport) const
    {
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        const std::string assetBaseName   = gaussianSplatAssetBaseName(osPath);
        const std::string relativeSrcPath = m_Registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string relativeImportedPath =
            m_Registry.getImportedAssetPath(
                VAssetType::eGaussianSplat,
                importedAssetKeyForCookedOutput(
                    m_Registry, VAssetType::eGaussianSplat, relativeSrcPath, assetBaseName),
                true);

        auto lookupUUID = vbase::uuid_from_string_key(relativeImportedPath);
        auto entry      = m_Registry.lookup(lookupUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport)
        {
            std::cout << "GaussianSplat already imported: " << entry.sourcePath << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
        }

        outSplat                = {};
        outSplat.uuid           = lookupUUID;
        outSplat.name           = assetBaseName;
        outSplat.sourceFileName = osPath.filename().generic_string();

        const std::string formatKey = gaussianSplatRegistryKey(osPath);
        if (formatKey.empty())
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eNotSupported);

        std::vector<uint8_t> rawData = readAll(osPath);
        if (rawData.empty())
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        static const gf::IORegistry kGaussForgeRegistry {};
        auto*                       reader = kGaussForgeRegistry.ReaderForExt(formatKey);
        if (reader == nullptr)
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eNotSupported);

        gf::ReadOptions readOptions {};
        auto            irResult = reader->Read(rawData.data(), rawData.size(), readOptions);
        if (!irResult)
        {
            std::cerr << "GaussianSplat import failed: " << irResult.error().message << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
        }

        const gf::GaussianCloudIR& cloud = irResult.value();
        if (cloud.numPoints <= 0)
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        const bool convertPlyAxes = gaussianSplatNeedsPlyAxisConversion(formatKey);

        // Fill per-splat data from GaussForge IR.
        const int32_t N      = cloud.numPoints;
        outSplat.numPoints   = N;
        outSplat.shDegree    = cloud.meta.shDegree;
        outSplat.antialiased = cloud.meta.antialiased;
        outSplat.sh          = cloud.sh;
        outSplat.lod         = extractGaussianSplatLodData(cloud, N);

        outSplat.splats.resize(N);
        for (int32_t i = 0; i < N; ++i)
        {
            VGaussianSplatPoint& p = outSplat.splats[i];
            const glm::vec3 sourcePosition(
                cloud.positions[i * 3 + 0],
                cloud.positions[i * 3 + 1],
                cloud.positions[i * 3 + 2]);
            p.position = convertPlyAxes ? convertGaussianPlyPositionToEngine(sourcePosition) : sourcePosition;
            p.opacity  = cloud.alphas[i];
            p.scale    = glm::vec3(cloud.scales[i * 3 + 0], cloud.scales[i * 3 + 1], cloud.scales[i * 3 + 2]);
            p.pad0     = 0.0f;
            glm::quat sourceRotation(
                cloud.rotations[i * 4 + 0],
                cloud.rotations[i * 4 + 1],
                cloud.rotations[i * 4 + 2],
                cloud.rotations[i * 4 + 3]);
            if (convertPlyAxes)
                sourceRotation = convertGaussianPlyRotationToEngine(sourceRotation);
            p.rotation = glm::vec4(sourceRotation.x, sourceRotation.y, sourceRotation.z, sourceRotation.w);
            p.shDC     = glm::vec3(cloud.colors[i * 3 + 0], cloud.colors[i * 3 + 1], cloud.colors[i * 3 + 2]);
            p.pad1     = 0.0f;
        }

        // Cook learned/imported CLOD assets as physical importance prefixes.
        // Runtime can still read the importance sidecar, but the packed splat
        // arrays themselves now make the first N points the highest-ranked N.
        physicallySortGaussianSplatByImportance(outSplat);

        // Save cooked asset.
        const std::string importedPath =
            (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeImportedPath).generic_string();

        auto sr = saveGaussianSplat(outSplat, importedPath, m_Options.zstdLevel);
        if (!sr)
            return vbase::Result<vbase::UUID, AssetError>::err(sr.error());

        // ── Save .vimport sidecar ────────────────────────────────────────────────
        VImport vimport {};
        vimport.importer = toString(VAssetType::eGaussianSplat);
        vimport.uid      = lookupUUID;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;

        auto importSidecarPath = osPath;
        importSidecarPath.replace_extension(".vimport");
        auto sr_import = saveVImport(vimport, importSidecarPath.generic_string());
        if (!sr_import)
            return vbase::Result<vbase::UUID, AssetError>::err(sr_import.error());

        auto rr =
            m_Registry.registerAsset(lookupUUID, relativeSrcPath, relativeImportedPath, VAssetType::eGaussianSplat);
        if (!rr)
            return vbase::Result<vbase::UUID, AssetError>::err(rr.error());

        return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
    }

    VAssetImporter::VAssetImporter(VAssetRegistry& registry) :
        m_Registry(registry), m_TextureImporter(registry), m_MeshImporter(registry), m_GaussianSplatImporter(registry)
    {}

    vbase::Result<void, AssetError> VAssetImporter::importOrReimportAssetFolder(vbase::StringView folderPath,
                                                                                bool              reimport)
    {
        std::filesystem::path osPath(folderPath);
        if (!std::filesystem::exists(osPath) || !std::filesystem::is_directory(osPath))
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        size_t scannedRegularFiles = 0;
        size_t skippedFiles        = 0;
        size_t importedTextures    = 0;
        size_t importedMeshes      = 0;
        size_t importedSplats      = 0;
        size_t importedTextAssets  = 0;

        std::cout << "[vasset] scanning asset folder: " << osPath.generic_string() << std::endl;

        for (auto it = std::filesystem::recursive_directory_iterator(osPath);
             it != std::filesystem::recursive_directory_iterator();
             ++it)
        {
            const auto&       entry   = *it;
            const std::string relPath = std::filesystem::relative(entry.path(), osPath).generic_string();
            if (entry.is_directory())
            {
                if (isIgnoredAssetImportDirectory(relPath))
                {
                    if (relPath == "training")
                        std::cout << "[vasset] skip dir : " << relPath << std::endl;
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!entry.is_regular_file())
                continue;
            const std::string filePath = entry.path().generic_string();
            if (isIgnoredAssetImportPath(relPath))
                continue;
            const std::string ext = entry.path().extension().generic_string();
            if (ext == ".vimport")
                continue;
            ++scannedRegularFiles;

            if (isValidTexture(ext))
            {
                std::cout << "[vasset] texture  : " << relPath << std::endl;
                VTexture texture;
                auto     tr = m_TextureImporter.importTexture(filePath, texture, reimport);
                if (!tr)
                {
                    std::cerr << "[vasset] failed texture  : " << relPath << " (" << assetErrorLabel(tr.error())
                              << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(tr.error());
                }
                ++importedTextures;
            }
            else if (isValidModel(ext))
            {
                std::cout << "[vasset] mesh     : " << relPath << std::endl;
                VMesh mesh;
                auto  mr = m_MeshImporter.importMesh(filePath, mesh, reimport);
                if (!mr)
                {
                    std::cerr << "[vasset] failed mesh     : " << relPath << " (" << assetErrorLabel(mr.error())
                              << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(mr.error());
                }
                ++importedMeshes;
            }
            else if (isValidGaussianSplat(ext))
            {
                std::cout << "[vasset] splat    : " << relPath << std::endl;
                VGaussianSplat splat;
                auto           gr = m_GaussianSplatImporter.importGaussianSplat(filePath, splat, reimport);
                if (!gr)
                {
                    std::cerr << "[vasset] failed splat    : " << relPath << " (" << assetErrorLabel(gr.error())
                              << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(gr.error());
                }
                ++importedSplats;
            }
            else if (isValidSourceTextAsset(ext))
            {
                std::cout << "[vasset] text     : " << relPath << std::endl;
                auto type = inferSourceTextAssetType(ext);
                auto rr   = importSourceTextAsset(m_Registry, filePath, reimport, type);
                if (!rr)
                {
                    std::cerr << "[vasset] failed text     : " << relPath << " (" << assetErrorLabel(rr.error())
                              << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(rr.error());
                }
                ++importedTextAssets;
            }
            else
            {
                ++skippedFiles;
            }
        }

        std::cout << "[vasset] summary  : scanned=" << scannedRegularFiles << ", textures=" << importedTextures
                  << ", meshes=" << importedMeshes << ", splats=" << importedSplats
                  << ", text_assets=" << importedTextAssets << ", skipped=" << skippedFiles << std::endl;

        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetImporter::importOrReimportAsset(vbase::StringView filePath, bool reimport)
    {
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath) || std::filesystem::is_directory(osPath))
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        const std::string relPath =
            std::filesystem::relative(osPath, std::filesystem::path(m_Registry.getAssetRootPath())).generic_string();
        if (isIgnoredAssetImportPath(relPath))
            return vbase::Result<void, AssetError>::err(AssetError::eNotSupported);

        const std::string ext = osPath.extension().generic_string();

        if (isValidTexture(ext))
        {
            VTexture texture;
            auto     tr = m_TextureImporter.importTexture(filePath, texture, reimport);
            if (!tr)
                return vbase::Result<void, AssetError>::err(tr.error());
            return vbase::Result<void, AssetError>::ok();
        }

        if (isValidModel(ext))
        {
            VMesh mesh;
            auto  mr = m_MeshImporter.importMesh(filePath, mesh, reimport);
            if (!mr)
                return vbase::Result<void, AssetError>::err(mr.error());
            return vbase::Result<void, AssetError>::ok();
        }

        if (isValidGaussianSplat(ext))
        {
            VGaussianSplat splat;
            auto           gr = m_GaussianSplatImporter.importGaussianSplat(filePath, splat, reimport);
            if (!gr)
                return vbase::Result<void, AssetError>::err(gr.error());
            return vbase::Result<void, AssetError>::ok();
        }

        if (isValidSourceTextAsset(ext))
        {
            auto type = inferSourceTextAssetType(ext);
            auto rr   = importSourceTextAsset(m_Registry, filePath, reimport, type);
            if (!rr)
                return vbase::Result<void, AssetError>::err(rr.error());
            return vbase::Result<void, AssetError>::ok();
        }

        return vbase::Result<void, AssetError>::err(AssetError::eNotSupported);
    }

} // namespace vasset
