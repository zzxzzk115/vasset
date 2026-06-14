#include "vasset/vasset_importers.hpp"
#include "vasset/vasset_import_database.hpp"
#include "vasset/vasset_registry.hpp"
#include "vasset/vasset_type.hpp"
#include "vasset/audio_import_params.hpp"
#include "vasset/mesh_import_params.hpp"
#include "vasset/texture_import_params.hpp"
#include "vasset/vanimation.hpp"
#include "vasset/vaudio.hpp"
#include "vasset/vgaussiansplat.hpp"
#include "vasset/vimport.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/scope_exit.hpp>

#include <ktx.h>

#include <vshadersystem/binary.hpp>
#include <vshadersystem/engine_keywords.hpp>
#include <vshadersystem/hash.hpp>
#include <vshadersystem/library.hpp>
#include <vshadersystem/shader_id.hpp>
#include <vshadersystem/system.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

// Declarations only; the implementation TU is src/stb_vorbis_impl.cpp.
#define STB_VORBIS_HEADER_ONLY
#include <stb/stb_vorbis.c>
#undef STB_VORBIS_HEADER_ONLY

#include <miniaudio.h>

#define DDSKTX_IMPLEMENT
#include <dds-ktx.h>
#include <xxhash.h>

#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <gf/core/gauss_ir.h>
#include <gf/io/registry.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <meshoptimizer.h>

#include <ozz/animation/offline/animation_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/skeleton_utils.h>
#include <ozz/base/io/archive.h>
#include <ozz/base/io/stream.h>

#include <algorithm>
#include <array>
#include <cctype>
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
#include <optional>
#include <span>
#include <numeric>
#include <ranges>
#include <regex>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace
{
    uint64_t hashString(const std::string& value, uint64_t seed = 0);
    uint64_t hashU64(uint64_t value, uint64_t seed);
    uint64_t hashFile(const std::filesystem::path& path, uint64_t seed = 0);

    std::string dependencyTargetPath(const vasset::VAssetRegistry& registry, const vbase::UUID& uuid)
    {
        const auto entry = registry.lookup(uuid);
        if (!entry.sourcePath.empty())
            return entry.sourcePath;
        return entry.importedPath;
    }

    std::vector<vasset::VAssetDependency>
    collectMeshDependencies(const vasset::VAssetRegistry& registry, const vasset::VMesh& mesh)
    {
        std::vector<vasset::VAssetDependency> out;
        std::unordered_set<std::string>       seen;

        auto add = [&](const vasset::VAssetDependencyKind kind,
                       const vbase::UUID&                 target,
                       std::string                        targetPath,
                       std::string                        context) {
            if (!target.valid() && targetPath.empty())
                return;

            const auto key = std::string(vasset::toString(kind)) + "|" +
                             (target.valid() ? vbase::to_string(target) : std::string {}) + "|" + targetPath +
                             "|" + context;
            if (!seen.insert(key).second)
                return;

            vasset::VAssetDependency dep;
            dep.kind       = kind;
            dep.targetUuid = target;
            dep.targetPath = std::move(targetPath);
            dep.context    = std::move(context);
            out.push_back(std::move(dep));
        };

        auto addTexture = [&](const vasset::VTextureRef& texture, const std::string& context) {
            if (!texture.uuid.valid())
                return;
            add(vasset::VAssetDependencyKind::eMaterialTexture,
                texture.uuid,
                dependencyTargetPath(registry, texture.uuid),
                context);
        };

        if (mesh.skeleton.valid())
            add(vasset::VAssetDependencyKind::eSkeleton,
                mesh.skeleton,
                !mesh.skeletonPath.empty() ? mesh.skeletonPath : dependencyTargetPath(registry, mesh.skeleton),
                "mesh.skeleton");

        for (size_t materialIndex = 0; materialIndex < mesh.materials.size(); ++materialIndex)
        {
            const auto& material = mesh.materials[materialIndex];
            const auto  prefix = "material[" + std::to_string(materialIndex) + "]" +
                                (material.name.empty() ? std::string {} : (":" + material.name)) + ".";

            for (const auto& binding : material.textures)
                addTexture(binding.texture,
                           prefix + "textureBinding(type=" + std::to_string(binding.type) + ",index=" +
                               std::to_string(binding.index) + ")");

            switch (material.model)
            {
                case vasset::VMaterialModel::ePBRMetallicRoughness:
                    addTexture(material.core.pbrMR.baseColorTexture, prefix + "baseColorTexture");
                    addTexture(material.core.pbrMR.alphaTexture, prefix + "alphaTexture");
                    addTexture(material.core.pbrMR.metallicTexture, prefix + "metallicTexture");
                    addTexture(material.core.pbrMR.roughnessTexture, prefix + "roughnessTexture");
                    addTexture(material.core.pbrMR.metallicRoughnessTexture, prefix + "metallicRoughnessTexture");
                    addTexture(material.core.pbrMR.specularTexture, prefix + "specularTexture");
                    addTexture(material.core.pbrMR.normalTexture, prefix + "normalTexture");
                    addTexture(material.core.pbrMR.ambientOcclusionTexture, prefix + "ambientOcclusionTexture");
                    addTexture(material.core.pbrMR.emissiveTexture, prefix + "emissiveTexture");
                    break;
                case vasset::VMaterialModel::ePBRSpecularGlossiness:
                    addTexture(material.core.pbrSG.diffuseTexture, prefix + "diffuseTexture");
                    addTexture(material.core.pbrSG.specularGlossinessTexture, prefix + "specularGlossinessTexture");
                    break;
                case vasset::VMaterialModel::eUnlit:
                    addTexture(material.core.unlit.colorTexture, prefix + "colorTexture");
                    break;
                case vasset::VMaterialModel::ePhong:
                    addTexture(material.core.phong.diffuseTexture, prefix + "diffuseTexture");
                    addTexture(material.core.phong.specularTexture, prefix + "specularTexture");
                    addTexture(material.core.phong.normalTexture, prefix + "normalTexture");
                    addTexture(material.core.phong.opacityTexture, prefix + "opacityTexture");
                    addTexture(material.core.phong.emissiveTexture, prefix + "emissiveTexture");
                    break;
                case vasset::VMaterialModel::eUnknown:
                case vasset::VMaterialModel::eCustom:
                    break;
            }
        }

        return out;
    }

    std::string normalizeAssetReferencePath(std::string path)
    {
        constexpr std::string_view resPrefix {"res://"};
        if (path.starts_with(resPrefix))
            path.erase(0, resPrefix.size());
        for (char& ch : path)
        {
            if (ch == '\\')
                ch = '/';
        }
        while (!path.empty() && path.front() == '/')
            path.erase(path.begin());
        return path;
    }

    std::optional<vbase::UUID> findRegistryUuidByPath(const vasset::VAssetRegistry& registry, const std::string& path)
    {
        const auto normalized = normalizeAssetReferencePath(path);
        for (const auto& [uuidStr, entry] : registry.getRegistry())
        {
            if (normalizeAssetReferencePath(entry.sourcePath) != normalized &&
                normalizeAssetReferencePath(entry.importedPath) != normalized)
            {
                continue;
            }

            vbase::UUID uuid {};
            if (vbase::try_parse_uuid(uuidStr.c_str(), uuid))
                return uuid;
        }
        return std::nullopt;
    }

    vasset::VAssetDependencyKind sourceTextDependencyKind(const vasset::VAssetType ownerType)
    {
        switch (ownerType)
        {
            case vasset::VAssetType::eScene:
            case vasset::VAssetType::eSceneManifest:
            case vasset::VAssetType::ePrefab:
                return vasset::VAssetDependencyKind::eSceneComponent;
            case vasset::VAssetType::eRenderGraphJson:
                return vasset::VAssetDependencyKind::eRenderGraphFeature;
            case vasset::VAssetType::eShaderLibraryManifest:
            case vasset::VAssetType::eShaderLibrary:
                return vasset::VAssetDependencyKind::eShaderLibrary;
            default:
                return vasset::VAssetDependencyKind::eRuntimePayload;
        }
    }

    std::vector<vasset::VAssetDependency> collectSourceTextDependencies(const vasset::VAssetRegistry& registry,
                                                                        const std::string&           text,
                                                                        const vasset::VAssetType     ownerType)
    {
        std::vector<vasset::VAssetDependency> out;
        std::unordered_set<std::string>       seen;

        auto add = [&](vbase::UUID targetUuid, std::string targetPath, std::string context) {
            if (!targetUuid.valid() && targetPath.empty())
                return;

            const auto normalizedPath = normalizeAssetReferencePath(targetPath);
            const auto key = (targetUuid.valid() ? vbase::to_string(targetUuid) : std::string {}) + "|" +
                             normalizedPath + "|" + context;
            if (!seen.insert(key).second)
                return;

            vasset::VAssetDependency dep;
            dep.kind       = sourceTextDependencyKind(ownerType);
            dep.targetUuid = targetUuid;
            dep.targetPath = normalizedPath;
            dep.context    = std::move(context);
            out.push_back(std::move(dep));
        };

        const std::regex resRefPattern(R"(res://[^"'\s\),\]]+)");
        for (std::sregex_iterator it(text.begin(), text.end(), resRefPattern), end; it != end; ++it)
        {
            const auto rawPath = (*it).str();
            auto       uuid = findRegistryUuidByPath(registry, rawPath).value_or(vbase::UUID {});
            add(uuid, rawPath, "res-uri");
        }

        const std::regex uuidPattern(R"(["']?([0-9a-fA-F]{32}|[0-9a-fA-F-]{36})["']?)");
        for (std::sregex_iterator it(text.begin(), text.end(), uuidPattern), end; it != end; ++it)
        {
            vbase::UUID uuid {};
            if (!vbase::try_parse_uuid((*it)[1].str().c_str(), uuid))
                continue;
            if (registry.lookup(uuid).type == vasset::VAssetType::eUnknown)
                continue;
            add(uuid, dependencyTargetPath(registry, uuid), "uuid");
        }

        return out;
    }

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

    uint32_t resolveBasisUThreadCount(uint32_t requestedThreadCount)
    {
        if (requestedThreadCount != 0)
            return requestedThreadCount;

        const auto hardwareThreadCount = std::thread::hardware_concurrency();
        return hardwareThreadCount > 0 ? hardwareThreadCount : 1;
    }

    bool hasSuffix(const std::string& value, const std::string& suffix)
    {
        return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::string toLowerAscii(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool isFbxSource(const std::filesystem::path& sourcePath)
    {
        return toLowerAscii(sourcePath.extension().generic_string()) == ".fbx";
    }

    void configureAssimpImporterForSource(Assimp::Importer& importer, const std::filesystem::path& sourcePath)
    {
        if (isFbxSource(sourcePath))
        {
            importer.SetPropertyBool(AI_CONFIG_FBX_CONVERT_TO_M, true);
        }
    }

    unsigned int assimpImportFlagsForSource(unsigned int flags, const std::filesystem::path& sourcePath)
    {
        if (isFbxSource(sourcePath))
            flags |= aiProcess_GlobalScale;
        return flags;
    }

    bool isLikelyNormalMapPath(const std::filesystem::path& path)
    {
        const auto filename = toLowerAscii(path.filename().generic_string());
        return filename.find("normal") != std::string::npos || filename.find("_n.") != std::string::npos ||
               filename.find("-n.") != std::string::npos;
    }

    std::filesystem::path findImportHintReadme(const std::filesystem::path& path)
    {
        std::error_code ec;
        auto            dir = path.parent_path();
        for (uint32_t i = 0; i < 4u && !dir.empty(); ++i)
        {
            auto candidate = dir / "README.txt";
            if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
                return candidate;

            const auto parent = dir.parent_path();
            if (parent == dir)
                break;
            dir = parent;
        }
        return {};
    }

    bool readmeDeclaresDirectXNormal(const std::filesystem::path& readmePath)
    {
        if (readmePath.empty())
            return false;

        std::ifstream file(readmePath);
        if (!file)
            return false;

        std::ostringstream content;
        content << file.rdbuf();
        const auto text = toLowerAscii(content.str());
        return text.find("normal (directx)") != std::string::npos ||
               text.find("normal: directx") != std::string::npos ||
               text.find("directx normal") != std::string::npos;
    }

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
        return ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".dae";
    }

    bool isValidGaussianSplat(vbase::StringView ext)
    {
        return ext == ".ply" || ext == ".spz" || ext == ".splat" || ext == ".ksplat";
    }

    bool isValidAudio(vbase::StringView ext)
    {
        return ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg";
    }

    bool isValidFont(vbase::StringView ext) { return ext == ".ttf" || ext == ".otf"; }

    bool isValidScene(vbase::StringView ext) { return ext == ".vscn"; }

    bool isValidSceneManifest(vbase::StringView ext) { return ext == ".vmanifest"; }

    bool isValidPrefab(vbase::StringView ext) { return ext == ".vprefab"; }

    class OzzMemoryStream final : public ozz::io::Stream
    {
    public:
        bool opened() const override { return true; }

        size_t Read(void* buffer, size_t size) override
        {
            const size_t available = m_Bytes.size() > m_Offset ? m_Bytes.size() - m_Offset : 0u;
            const size_t count     = std::min(size, available);
            if (count != 0u)
            {
                std::memcpy(buffer, m_Bytes.data() + m_Offset, count);
                m_Offset += count;
            }
            return count;
        }

        size_t Write(const void* buffer, size_t size) override
        {
            if (m_Offset + size > m_Bytes.size())
                m_Bytes.resize(m_Offset + size);
            std::memcpy(m_Bytes.data() + m_Offset, buffer, size);
            m_Offset += size;
            return size;
        }

        int Seek(int offset, Origin origin) override
        {
            size_t base = 0;
            if (origin == kCurrent)
                base = m_Offset;
            else if (origin == kEnd)
                base = m_Bytes.size();

            const auto next = static_cast<int64_t>(base) + static_cast<int64_t>(offset);
            if (next < 0)
                return -1;
            m_Offset = static_cast<size_t>(next);
            if (m_Offset > m_Bytes.size())
                m_Bytes.resize(m_Offset);
            return 0;
        }

        int Tell() const override { return static_cast<int>(m_Offset); }
        size_t Size() const override { return m_Bytes.size(); }

        std::vector<std::byte> bytes() const
        {
            std::vector<std::byte> out(m_Bytes.size());
            if (!m_Bytes.empty())
                std::memcpy(out.data(), m_Bytes.data(), m_Bytes.size());
            return out;
        }

    private:
        std::vector<uint8_t> m_Bytes;
        size_t               m_Offset {0};
    };

    ozz::math::Float3 toOzz(const aiVector3D& value) { return {value.x, value.y, value.z}; }

    ozz::math::Quaternion toOzz(const aiQuaternion& value) { return {value.x, value.y, value.z, value.w}; }

    ozz::math::Transform toOzz(const aiMatrix4x4& matrix)
    {
        aiVector3D   scaling(1.0f, 1.0f, 1.0f);
        aiQuaternion rotation;
        aiVector3D   translation(0.0f, 0.0f, 0.0f);
        matrix.Decompose(scaling, rotation, translation);
        return ozz::math::Transform {
            .translation = toOzz(translation),
            .rotation    = toOzz(rotation),
            .scale       = toOzz(scaling),
        };
    }

    glm::mat4 toGlm(const aiMatrix4x4& matrix)
    {
        return glm::transpose(glm::make_mat4(&matrix.a1));
    }

    std::unordered_set<std::string> collectBoneNames(const aiScene* scene)
    {
        std::unordered_set<std::string> bones;
        if (!scene)
            return bones;
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            const aiMesh* mesh = scene->mMeshes[meshIndex];
            if (!mesh)
                continue;
            for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
            {
                if (const aiBone* bone = mesh->mBones[boneIndex])
                    bones.insert(bone->mName.C_Str());
            }
        }
        return bones;
    }

    bool nodeHasBoneDescendant(const aiNode* node, const std::unordered_set<std::string>& bones)
    {
        if (!node)
            return false;
        if (bones.contains(node->mName.C_Str()))
            return true;
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            if (nodeHasBoneDescendant(node->mChildren[i], bones))
                return true;
        }
        return false;
    }

    const aiNode* findSkeletonRoot(const aiScene* scene, const std::unordered_set<std::string>& bones)
    {
        if (!scene || !scene->mRootNode || bones.empty())
            return nullptr;

        const aiNode* root = scene->mRootNode;
        for (;;)
        {
            const aiNode* onlyChildWithBones = nullptr;
            uint32_t      childrenWithBones  = 0;
            for (unsigned int i = 0; i < root->mNumChildren; ++i)
            {
                if (nodeHasBoneDescendant(root->mChildren[i], bones))
                {
                    onlyChildWithBones = root->mChildren[i];
                    ++childrenWithBones;
                }
            }

            if (childrenWithBones != 1u)
                break;
            root = onlyChildWithBones;
        }

        return nodeHasBoneDescendant(root, bones) ? root : nullptr;
    }

    void appendRawSkeletonJoint(const aiNode*                                      node,
                                ozz::animation::offline::RawSkeleton::Joint&       joint,
                                std::unordered_map<std::string, ozz::math::Transform>& restPoseByName)
    {
        joint.name      = node->mName.C_Str();
        joint.transform = toOzz(node->mTransformation);
        restPoseByName[std::string(joint.name.c_str())] = joint.transform;
        joint.children.resize(node->mNumChildren);
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            appendRawSkeletonJoint(node->mChildren[i], joint.children[i], restPoseByName);
    }

    std::vector<std::byte> serializeOzzSkeleton(const ozz::animation::Skeleton& skeleton)
    {
        OzzMemoryStream stream;
        ozz::io::OArchive archive(&stream);
        archive << skeleton;
        return stream.bytes();
    }

    std::vector<std::byte> serializeOzzAnimation(const ozz::animation::Animation& animation)
    {
        OzzMemoryStream stream;
        ozz::io::OArchive archive(&stream);
        archive << animation;
        return stream.bytes();
    }

    struct ImportedSkeletonData
    {
        vasset::VSkeleton skeleton;
        std::unordered_map<std::string, uint32_t> jointIndexByName;
        std::vector<ozz::math::Transform> restLocalTransforms;
    };

    vbase::Result<ImportedSkeletonData, vasset::AssetError> buildImportedSkeleton(const aiScene* scene,
                                                                                  const vbase::UUID& uuid,
                                                                                  const std::string& name)
    {
        const auto boneNames = collectBoneNames(scene);
        const aiNode* rootBone = findSkeletonRoot(scene, boneNames);
        if (!rootBone)
            return vbase::Result<ImportedSkeletonData, vasset::AssetError>::err(vasset::AssetError::eInvalidFormat);

        ozz::animation::offline::RawSkeleton rawSkeleton;
        std::unordered_map<std::string, ozz::math::Transform> restPoseByName;
        rawSkeleton.roots.resize(1);
        appendRawSkeletonJoint(rootBone, rawSkeleton.roots[0], restPoseByName);
        if (!rawSkeleton.Validate())
            return vbase::Result<ImportedSkeletonData, vasset::AssetError>::err(vasset::AssetError::eInvalidFormat);

        ozz::animation::offline::SkeletonBuilder builder;
        auto runtimeSkeleton = builder(rawSkeleton);
        if (!runtimeSkeleton)
            return vbase::Result<ImportedSkeletonData, vasset::AssetError>::err(vasset::AssetError::eImportFailed);

        ImportedSkeletonData out;
        out.skeleton.uuid      = uuid;
        out.skeleton.name      = name;
        out.skeleton.ozzData   = serializeOzzSkeleton(*runtimeSkeleton);

        const auto jointNames = runtimeSkeleton->joint_names();
        const auto parents    = runtimeSkeleton->joint_parents();
        out.skeleton.jointNames.reserve(jointNames.size());
        out.skeleton.jointParents.reserve(parents.size());
        out.restLocalTransforms.reserve(jointNames.size());
        for (int i = 0; i < runtimeSkeleton->num_joints(); ++i)
        {
            const std::string jointName = jointNames[i];
            out.jointIndexByName.emplace(jointName, static_cast<uint32_t>(i));
            out.skeleton.jointNames.push_back(jointName);
            out.skeleton.jointParents.push_back(parents[i]);
            if (const auto it = restPoseByName.find(jointName); it != restPoseByName.end())
                out.restLocalTransforms.push_back(it->second);
            else
                out.restLocalTransforms.push_back(ozz::math::Transform::identity());
        }
        return vbase::Result<ImportedSkeletonData, vasset::AssetError>::ok(std::move(out));
    }

    bool applySkinningData(const aiMesh*                                           mesh,
                           const ImportedSkeletonData&                             skeleton,
                           vasset::VMesh&                                         outMesh,
                           const std::unordered_map<std::string, glm::mat4>&       inverseBindPoseByName)
    {
        if (!mesh || !mesh->HasBones())
            return false;

        outMesh.hasSkin = true;
        outMesh.jointNames = skeleton.skeleton.jointNames;
        outMesh.jointParents = skeleton.skeleton.jointParents;
        outMesh.inverseBindPoses.assign(outMesh.jointNames.size(), glm::mat4(1.0f));
        for (size_t i = 0; i < outMesh.jointNames.size(); ++i)
        {
            if (const auto it = inverseBindPoseByName.find(outMesh.jointNames[i]); it != inverseBindPoseByName.end())
                outMesh.inverseBindPoses[i] = it->second;
        }

        outMesh.jointIndices.resize(outMesh.vertexCount, glm::ivec4(0));
        outMesh.jointWeights.resize(outMesh.vertexCount, glm::vec4(0.0f));

        struct Influence
        {
            uint32_t joint {0};
            float    weight {0.0f};
        };
        std::vector<std::vector<Influence>> influences(mesh->mNumVertices);
        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
        {
            const aiBone* bone = mesh->mBones[boneIndex];
            if (!bone)
                continue;

            const auto jointIt = skeleton.jointIndexByName.find(bone->mName.C_Str());
            if (jointIt == skeleton.jointIndexByName.end())
            {
                std::cerr << "[vasset] skinned mesh references bone outside imported skeleton: "
                          << bone->mName.C_Str() << std::endl;
                return false;
            }

            for (unsigned int weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex)
            {
                const aiVertexWeight& weight = bone->mWeights[weightIndex];
                if (weight.mVertexId >= mesh->mNumVertices || weight.mWeight <= 0.0f)
                    continue;
                influences[weight.mVertexId].push_back(Influence {
                    .joint  = jointIt->second,
                    .weight = weight.mWeight,
                });
            }
        }

        const uint32_t vertexOffset = outMesh.vertexCount - mesh->mNumVertices;
        for (uint32_t vertex = 0; vertex < mesh->mNumVertices; ++vertex)
        {
            auto& vertexInfluences = influences[vertex];
            std::ranges::sort(vertexInfluences, [](const Influence& a, const Influence& b) {
                return a.weight > b.weight;
            });
            if (vertexInfluences.size() > 4)
                vertexInfluences.resize(4);

            float totalWeight = 0.0f;
            for (const auto& influence : vertexInfluences)
                totalWeight += influence.weight;
            if (totalWeight <= 0.0f)
            {
                outMesh.jointIndices[vertexOffset + vertex] = glm::ivec4(0);
                outMesh.jointWeights[vertexOffset + vertex] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                continue;
            }

            glm::ivec4 joints(0);
            glm::vec4  weights(0.0f);
            for (size_t i = 0; i < vertexInfluences.size(); ++i)
            {
                joints[static_cast<int>(i)] = static_cast<int>(vertexInfluences[i].joint);
                weights[static_cast<int>(i)] = vertexInfluences[i].weight / totalWeight;
            }
            outMesh.jointIndices[vertexOffset + vertex] = joints;
            outMesh.jointWeights[vertexOffset + vertex] = weights;
        }
        return true;
    }

    std::unordered_map<std::string, glm::mat4> collectInverseBindPoses(const aiScene* scene)
    {
        std::unordered_map<std::string, glm::mat4> out;
        if (!scene)
            return out;
        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
        {
            const aiMesh* mesh = scene->mMeshes[meshIndex];
            if (!mesh)
                continue;
            for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
            {
                if (const aiBone* bone = mesh->mBones[boneIndex])
                    out.emplace(bone->mName.C_Str(), toGlm(bone->mOffsetMatrix));
            }
        }
        return out;
    }

    vbase::Result<std::vector<vasset::VAnimation>, vasset::AssetError> buildImportedAnimations(
        const aiScene* scene,
        const ImportedSkeletonData& skeleton,
        const std::string& sourceStem,
        const std::function<vbase::UUID(size_t, std::string_view)>& uuidForAnimation)
    {
        std::vector<vasset::VAnimation> out;
        if (!scene || scene->mNumAnimations == 0u)
            return vbase::Result<std::vector<vasset::VAnimation>, vasset::AssetError>::ok(std::move(out));

        ozz::animation::offline::AnimationBuilder builder;
        out.reserve(scene->mNumAnimations);
        for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
        {
            const aiAnimation* animation = scene->mAnimations[animationIndex];
            if (!animation)
                continue;

            const double ticksPerSecond = animation->mTicksPerSecond != 0.0 ? animation->mTicksPerSecond : 25.0;
            ozz::animation::offline::RawAnimation rawAnimation;
            rawAnimation.name = animation->mName.length > 0 ? animation->mName.C_Str()
                                                            : std::format("{}_{}", sourceStem, animationIndex);
            rawAnimation.duration = static_cast<float>(animation->mDuration / ticksPerSecond);
            rawAnimation.tracks.resize(skeleton.skeleton.jointNames.size());

            for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
            {
                const aiNodeAnim* channel = animation->mChannels[channelIndex];
                if (!channel)
                    continue;

                const auto jointIt = skeleton.jointIndexByName.find(channel->mNodeName.C_Str());
                if (jointIt == skeleton.jointIndexByName.end())
                    continue;

                auto& track = rawAnimation.tracks[jointIt->second];
                track.translations.reserve(channel->mNumPositionKeys);
                for (unsigned int keyIndex = 0; keyIndex < channel->mNumPositionKeys; ++keyIndex)
                {
                    const auto& key = channel->mPositionKeys[keyIndex];
                    track.translations.push_back({
                        .time  = static_cast<float>(key.mTime / ticksPerSecond),
                        .value = toOzz(key.mValue),
                    });
                }

                track.rotations.reserve(channel->mNumRotationKeys);
                for (unsigned int keyIndex = 0; keyIndex < channel->mNumRotationKeys; ++keyIndex)
                {
                    const auto& key = channel->mRotationKeys[keyIndex];
                    track.rotations.push_back({
                        .time  = static_cast<float>(key.mTime / ticksPerSecond),
                        .value = toOzz(key.mValue),
                    });
                }

                track.scales.reserve(channel->mNumScalingKeys);
                for (unsigned int keyIndex = 0; keyIndex < channel->mNumScalingKeys; ++keyIndex)
                {
                    const auto& key = channel->mScalingKeys[keyIndex];
                    track.scales.push_back({
                        .time  = static_cast<float>(key.mTime / ticksPerSecond),
                        .value = toOzz(key.mValue),
                    });
                }
            }

            for (size_t trackIndex = 0; trackIndex < rawAnimation.tracks.size(); ++trackIndex)
            {
                const auto rest = trackIndex < skeleton.restLocalTransforms.size() ?
                                      skeleton.restLocalTransforms[trackIndex] :
                                      ozz::math::Transform::identity();
                auto& track = rawAnimation.tracks[trackIndex];
                if (track.translations.empty())
                    track.translations.push_back({.time = 0.0f, .value = rest.translation});
                if (track.rotations.empty())
                    track.rotations.push_back({.time = 0.0f, .value = rest.rotation});
                if (track.scales.empty())
                    track.scales.push_back({.time = 0.0f, .value = rest.scale});
            }

            if (!rawAnimation.Validate())
                return vbase::Result<std::vector<vasset::VAnimation>, vasset::AssetError>::err(
                    vasset::AssetError::eInvalidFormat);

            auto runtimeAnimation = builder(rawAnimation);
            if (!runtimeAnimation)
                return vbase::Result<std::vector<vasset::VAnimation>, vasset::AssetError>::err(
                    vasset::AssetError::eImportFailed);

            vasset::VAnimation imported;
            imported.name     = rawAnimation.name;
            imported.uuid     = uuidForAnimation(animationIndex, imported.name);
            imported.duration = runtimeAnimation->duration();
            imported.ozzData  = serializeOzzAnimation(*runtimeAnimation);
            out.push_back(std::move(imported));
        }

        return vbase::Result<std::vector<vasset::VAnimation>, vasset::AssetError>::ok(std::move(out));
    }

    bool isValidScriptLua(vbase::StringView ext) { return ext == ".lua"; }

    bool isValidScriptableObjectLua(const std::filesystem::path& path)
    {
        const auto filename = path.filename().generic_string();
        return hasSuffix(filename, ".vso.lua") || hasSuffix(filename, ".vsrp.lua") ||
               hasSuffix(filename, ".vfeature.lua");
    }

    bool isValidShaderLibraryManifest(const std::filesystem::path& path)
    {
        return hasSuffix(path.filename().generic_string(), ".vshaderlib.lua");
    }

    bool isValidRenderGraphJson(const std::filesystem::path& path)
    {
        return hasSuffix(path.filename().generic_string(), ".vrg.json");
    }

    bool isValidMaterialGraphJson(const std::filesystem::path& path)
    {
        return path.extension().generic_string() == ".vmatgraph" ||
               hasSuffix(path.filename().generic_string(), ".vmatgraph.json");
    }

    bool isValidAnimatorGraphJson(const std::filesystem::path& path)
    {
        return path.extension().generic_string() == ".vanimgraph" ||
               hasSuffix(path.filename().generic_string(), ".vanimgraph.json");
    }

    bool isValidMaterialJson(const std::filesystem::path& path)
    {
        return hasSuffix(path.filename().generic_string(), ".vmat.json");
    }

    bool isValidSourceTextAsset(const std::filesystem::path& path)
    {
        const auto ext = path.extension().generic_string();
        return isValidScene(ext) || isValidSceneManifest(ext) || isValidPrefab(ext) || isValidScriptLua(ext) ||
               isValidRenderGraphJson(path) || isValidMaterialGraphJson(path) || isValidAnimatorGraphJson(path) ||
               isValidMaterialJson(path);
    }

    bool isPathUnderDirectory(const std::string& relPath, const std::string& dir)
    {
        return relPath == dir || relPath.rfind(dir + "/", 0) == 0;
    }

    std::string normalizeIgnoredDirectory(std::string dir)
    {
        std::ranges::replace(dir, '\\', '/');
        while (!dir.empty() && dir.back() == '/')
            dir.pop_back();
        return dir;
    }

    std::vector<std::string>
    makeIgnoredAssetImportDirectories(const vasset::VAssetRegistry&               registry,
                                      const vasset::VAssetImporter::ImportOptions& options)
    {
        std::vector<std::string> ignoredDirectories;
        ignoredDirectories.reserve(options.ignoredDirectories.size() + 2);
        auto appendIgnoredDirectory = [&ignoredDirectories](std::string dir) {
            auto normalized = normalizeIgnoredDirectory(std::move(dir));
            if (!normalized.empty() && !std::ranges::contains(ignoredDirectories, normalized))
                ignoredDirectories.push_back(std::move(normalized));
        };

        appendIgnoredDirectory(registry.getImportedFolderName());
        appendIgnoredDirectory("training");
        for (const auto& ignoredDirectory : options.ignoredDirectories)
        {
            appendIgnoredDirectory(ignoredDirectory);
        }
        return ignoredDirectories;
    }

    bool isIgnoredAssetImportDirectory(const std::string& relPath, std::span<const std::string> ignoredDirectories)
    {
        return std::ranges::any_of(ignoredDirectories,
                                   [&](const std::string& ignoredDir) { return relPath == ignoredDir; });
    }

    bool isIgnoredAssetImportPath(const std::string& relPath, std::span<const std::string> ignoredDirectories)
    {
        return std::ranges::any_of(ignoredDirectories, [&](const std::string& ignoredDir) {
            return isPathUnderDirectory(relPath, ignoredDir);
        });
    }

    std::filesystem::path importDatabasePath(const vasset::VAssetRegistry& registry)
    {
        return std::filesystem::path(registry.getAssetRootPath()) / registry.getImportedFolderName() /
               "asset_database.tsv";
    }

    vasset::VAssetImportDatabase loadImportDatabase(const vasset::VAssetRegistry& registry)
    {
        vasset::VAssetImportDatabase db;
        (void)db.load(importDatabasePath(registry).generic_string());
        return db;
    }

    bool outputFilesExist(const vasset::VAssetRegistry& registry, std::initializer_list<std::string> outputs)
    {
        const auto assetRoot = std::filesystem::path(registry.getAssetRootPath());
        for (const auto& output : outputs)
        {
            std::error_code ec;
            if (output.empty() || !std::filesystem::exists(assetRoot / output, ec))
                return false;
        }
        return true;
    }

    bool outputFilesAreNewerThanSource(const vasset::VAssetRegistry& registry,
                                       const std::filesystem::path&   source,
                                       std::initializer_list<std::string> outputs)
    {
        const auto assetRoot = std::filesystem::path(registry.getAssetRootPath());
        std::error_code ec;
        if (!std::filesystem::exists(source, ec))
            return false;
        const auto sourceTime = std::filesystem::last_write_time(source, ec);
        if (ec)
            return false;

        for (const auto& output : outputs)
        {
            const auto outputPath = assetRoot / output;
            if (output.empty() || !std::filesystem::exists(outputPath, ec))
                return false;
            const auto outputTime = std::filesystem::last_write_time(outputPath, ec);
            if (ec || outputTime < sourceTime)
                return false;
        }
        return true;
    }

    bool importDatabaseRecordIsCurrent(const vasset::VAssetRegistry& registry,
                                       const vbase::UUID&            uid,
                                       const std::string&            importer,
                                       const std::string&            source,
                                       const std::string&            output,
                                       const std::string&            importerVersion,
                                       const std::string&            outputSchema,
                                       uint64_t                      sourceHash,
                                       uint64_t                      dependencyHash,
                                       uint64_t                      paramsHash,
                                       std::initializer_list<std::string> outputs)
    {
        if (!outputFilesExist(registry, outputs))
            return false;

        const auto db = loadImportDatabase(registry);
        const auto* record = db.findBySource(source);
        if (!record)
            return false;

        return record->uid == vbase::to_string(uid) && record->importer == importer && record->source == source &&
               record->output == output && record->importerVersion == importerVersion &&
               record->outputSchema == outputSchema && record->sourceHash == sourceHash &&
               record->dependencyHash == dependencyHash && record->paramsHash == paramsHash;
    }

    vbase::Result<void, vasset::AssetError> updateImportDatabaseRecord(const vasset::VAssetRegistry& registry,
                                                                       const vbase::UUID&            uid,
                                                                       const std::string&            importer,
                                                                       const std::string&            source,
                                                                       const std::string&            output,
                                                                       const std::string&            importerVersion,
                                                                       const std::string&            outputSchema,
                                                                       uint64_t                      sourceHash,
                                                                       uint64_t                      dependencyHash,
                                                                       uint64_t                      paramsHash)
    {
        auto db = loadImportDatabase(registry);

        vasset::VAssetImportDatabase::Record record {};
        record.uid             = vbase::to_string(uid);
        record.importer        = importer;
        record.source          = source;
        record.output          = output;
        record.importerVersion = importerVersion;
        record.outputSchema    = outputSchema;
        record.sourceHash      = sourceHash;
        record.dependencyHash  = dependencyHash;
        record.paramsHash      = paramsHash;

        db.upsert(std::move(record));
        return db.save(importDatabasePath(registry).generic_string());
    }

    uint64_t textureImportParamsHash(const vasset::VTextureImporter::ImportOptions& options)
    {
        uint64_t h = hashString("texture-params");
        h = hashU64(options.generateMipmaps ? 1u : 0u, h);
        h = hashU64(options.flipY ? 1u : 0u, h);
        h = hashU64(static_cast<uint64_t>(options.targetTextureFileFormat), h);
        h = hashU64(options.uastc ? 1u : 0u, h);
        h = hashU64(options.noSSE ? 1u : 0u, h);
        h = hashU64(options.qualityLevel, h);
        h = hashU64(options.compressionLevel, h);
        h = hashU64(options.basisUThreadCount, h);
        h = hashU64(options.compressOnlyLargeTextures ? 1u : 0u, h);
        h = hashU64(options.basisUCompressMinDimension, h);
        h = hashU64(options.basisUCompressMinSourceBytes, h);
        h = hashU64(options.downscaleLargeTextures ? 1u : 0u, h);
        h = hashU64(options.downscaleMinDimension, h);
        h = hashU64(options.downscaleTargetDimension, h);
        h = hashU64(options.bakeNormalMap ? 1u : 0u, h);
        h = hashU64(options.directXNormalMap ? 1u : 0u, h);
        return h;
    }

    uint64_t meshImportParamsHash(const vasset::VMeshImporter::ImportOptions& options)
    {
        uint64_t h = hashString("mesh-params");
        h = hashU64(options.calcTangentSpace ? 1u : 0u, h);
        h = hashU64(options.genSmoothNormals ? 1u : 0u, h);
        h = hashU64(options.genUVCoords ? 1u : 0u, h);
        h = hashU64(options.flipUVs ? 1u : 0u, h);
        h = hashU64(options.preTransformVertices ? 1u : 0u, h);
        h = hashU64(options.generateMeshlets ? 1u : 0u, h);
        h = hashU64(options.optimizeVertexCache ? 1u : 0u, h);
        h = hashU64(options.optimizeOverdraw ? 1u : 0u, h);
        h = hashU64(options.optimizeVertexFetch ? 1u : 0u, h);
        return h;
    }

    uint64_t audioImportParamsHash(const vasset::VAudioImporter::ImportOptions& options)
    {
        uint64_t h = hashString("audio-params");
        h = hashU64(static_cast<uint64_t>(options.storage), h);
        h = hashU64(options.targetSampleRate, h);
        h = hashU64(options.forceMono ? 1u : 0u, h);
        h = hashU64(options.normalize ? 1u : 0u, h);
        h = hashU64(options.bitrateKbps, h);
        h = hashU64(options.quality, h);
        return h;
    }

    // Build the assimp post-process flag set from per-asset mesh options. Triangulate is always
    // applied (the renderer requires triangles); pre-transform is added by the single-mesh path.
    unsigned int meshAssimpBaseFlags(const vasset::VMeshImporter::ImportOptions& o)
    {
        unsigned int f = aiProcess_Triangulate;
        if (o.flipUVs)
            f |= aiProcess_FlipUVs;
        if (o.calcTangentSpace)
            f |= aiProcess_CalcTangentSpace;
        if (o.genSmoothNormals)
            f |= aiProcess_GenSmoothNormals;
        if (o.genUVCoords)
            f |= aiProcess_GenUVCoords;
        return f;
    }

    std::string sanitizeAssetSegment(std::string value)
    {
        if (value.empty())
            return "node";
        for (char& c : value)
        {
            const auto ch = static_cast<unsigned char>(c);
            if (!std::isalnum(ch) && c != '_' && c != '-')
                c = '_';
        }
        while (!value.empty() && value.front() == '_')
            value.erase(value.begin());
        while (!value.empty() && value.back() == '_')
            value.pop_back();
        return value.empty() ? "node" : value;
    }

    std::string escapeSceneString(std::string_view value)
    {
        std::string out;
        out.reserve(value.size());
        for (const char c : value)
        {
            if (c == '"' || c == '\\')
                out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    void finalizeMeshVertexFlags(vasset::VMesh& mesh)
    {
        mesh.vertexFlags = vasset::VVertexFlags::ePosition;
        if (mesh.normals.size() == mesh.vertexCount)
            mesh.vertexFlags |= vasset::VVertexFlags::eNormal;
        if (mesh.colors.size() == mesh.vertexCount)
            mesh.vertexFlags |= vasset::VVertexFlags::eColor;
        if (mesh.texCoords0.size() == mesh.vertexCount)
            mesh.vertexFlags |= vasset::VVertexFlags::eTexCoord0;
        if (mesh.texCoords1.size() == mesh.vertexCount)
            mesh.vertexFlags |= vasset::VVertexFlags::eTexCoord1;
        if (mesh.tangents.size() == mesh.vertexCount)
            mesh.vertexFlags |= vasset::VVertexFlags::eTangent;
        if (mesh.jointIndices.size() == mesh.vertexCount)
            mesh.vertexFlags |= vasset::VVertexFlags::eJointIndices;
        if (mesh.jointWeights.size() == mesh.vertexCount)
            mesh.vertexFlags |= vasset::VVertexFlags::eJointWeights;
    }

    void updateMeshLocalBounds(vasset::VMesh& mesh)
    {
        mesh.hasLocalBounds = false;
        mesh.localBoundsMin = glm::vec3 {0.0f};
        mesh.localBoundsMax = glm::vec3 {0.0f};
        if (mesh.positions.empty())
            return;

        glm::vec3 minP(std::numeric_limits<float>::infinity());
        glm::vec3 maxP(-std::numeric_limits<float>::infinity());
        for (const auto& position : mesh.positions)
        {
            minP = glm::min(minP, position);
            maxP = glm::max(maxP, position);
        }

        if (!std::isfinite(minP.x) || !std::isfinite(minP.y) || !std::isfinite(minP.z) ||
            !std::isfinite(maxP.x) || !std::isfinite(maxP.y) || !std::isfinite(maxP.z))
        {
            return;
        }

        mesh.hasLocalBounds = true;
        mesh.localBoundsMin = minP;
        mesh.localBoundsMax = maxP;
    }

    void writeSceneVec3(std::ostringstream& out, const char* component, const char* field, const aiVector3D& value)
    {
        out << component << "/" << field << " = (" << value.x << ", " << value.y << ", " << value.z << ")\n";
    }

    void writeSceneQuat(std::ostringstream& out, const char* component, const char* field, const aiQuaternion& value)
    {
        out << component << "/" << field << " = (" << value.x << ", " << value.y << ", " << value.z << ", "
            << value.w << ")\n";
    }

    bool isAssimpGeneratedNodeName(std::string_view name)
    {
        return name.empty() || name.starts_with("nodes[") || name.starts_with("$AssimpFbx$");
    }

    std::string displayNameForNode(const aiNode* node, const aiScene* scene, const std::filesystem::path& sourcePath, int nodeId)
    {
        std::string nodeName = node ? node->mName.C_Str() : std::string {};
        if (!isAssimpGeneratedNodeName(nodeName))
            return nodeName;

        if (node && node->mNumMeshes == 1 && node->mMeshes[0] < scene->mNumMeshes)
        {
            std::string meshName = scene->mMeshes[node->mMeshes[0]]->mName.C_Str();
            if (!isAssimpGeneratedNodeName(meshName))
                return meshName;
        }

        if (nodeId == 1)
            return sourcePath.stem().generic_string();
        if (node && node->mNumMeshes > 0)
            return "Geometry";
        return std::format("Node_{}", nodeId);
    }

    std::string displayNameForMesh(const aiMesh* mesh, const aiScene* scene, const std::filesystem::path& sourcePath, uint32_t meshOrdinal)
    {
        std::string meshName = mesh ? mesh->mName.C_Str() : std::string {};
        if (!isAssimpGeneratedNodeName(meshName))
            return meshName;

        if (mesh && mesh->mMaterialIndex < scene->mNumMaterials)
        {
            std::string materialName = scene->mMaterials[mesh->mMaterialIndex]->GetName().C_Str();
            if (!materialName.empty())
                return materialName;
        }

        return std::format("{}_Mesh_{}", sourcePath.stem().generic_string(), meshOrdinal);
    }

    uint64_t gaussianSplatImportParamsHash(const vasset::VGaussianSplatImporter::ImportOptions& options)
    {
        return hashU64(static_cast<uint64_t>(options.zstdLevel), hashString("gaussian-splat-params"));
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

    std::array<uint8_t, 16> decodeBC4Block(const uint8_t* block)
    {
        std::array<uint8_t, 8> palette {};
        palette[0] = block[0];
        palette[1] = block[1];
        if (palette[0] > palette[1])
        {
            for (uint32_t i = 1; i < 7; ++i)
            {
                palette[i + 1] = static_cast<uint8_t>(
                    ((7u - i) * static_cast<uint32_t>(palette[0]) + i * static_cast<uint32_t>(palette[1]) + 3u) /
                    7u);
            }
        }
        else
        {
            for (uint32_t i = 1; i < 5; ++i)
            {
                palette[i + 1] = static_cast<uint8_t>(
                    ((5u - i) * static_cast<uint32_t>(palette[0]) + i * static_cast<uint32_t>(palette[1]) + 2u) /
                    5u);
            }
            palette[6] = 0u;
            palette[7] = 255u;
        }

        uint64_t indices = 0u;
        for (uint32_t i = 0; i < 6; ++i)
            indices |= static_cast<uint64_t>(block[2u + i]) << (8u * i);

        std::array<uint8_t, 16> out {};
        for (uint32_t i = 0; i < 16; ++i)
            out[i] = palette[static_cast<uint32_t>((indices >> (3u * i)) & 0x7u)];
        return out;
    }

    uint8_t encodeNormalChannel(float value)
    {
        value = std::clamp(value * 0.5f + 0.5f, 0.0f, 1.0f);
        return static_cast<uint8_t>(std::round(value * 255.0f));
    }

    bool bakeBC5NormalToRGBA8(const ddsktx_texture_info& tc,
                              const uint8_t*            sourceBytes,
                              const size_t              sourceSize,
                              const bool                directXNormalMap,
                              std::vector<uint8_t>&     outPixels)
    {
        if (tc.format != DDSKTX_FORMAT_BC5 || tc.width <= 0 || tc.height <= 0)
            return false;

        ddsktx_sub_data subData {};
        ddsktx_get_sub(&tc, &subData, sourceBytes, static_cast<int>(sourceSize), 0, 0, 0);
        if (!subData.buff)
            return false;

        const uint32_t width       = static_cast<uint32_t>(tc.width);
        const uint32_t height      = static_cast<uint32_t>(tc.height);
        const uint32_t blockWidth  = (width + 3u) / 4u;
        const uint32_t blockHeight = (height + 3u) / 4u;
        outPixels.assign(static_cast<size_t>(width) * height * 4u, 255u);

        const auto* blocks = static_cast<const uint8_t*>(subData.buff);
        for (uint32_t by = 0; by < blockHeight; ++by)
        {
            for (uint32_t bx = 0; bx < blockWidth; ++bx)
            {
                const uint8_t* block = blocks + (static_cast<size_t>(by) * blockWidth + bx) * 16u;
                const auto     xs    = decodeBC4Block(block);
                const auto     ys    = decodeBC4Block(block + 8u);
                for (uint32_t py = 0; py < 4u; ++py)
                {
                    const uint32_t y = by * 4u + py;
                    if (y >= height)
                        continue;
                    for (uint32_t px = 0; px < 4u; ++px)
                    {
                        const uint32_t x = bx * 4u + px;
                        if (x >= width)
                            continue;

                        const uint32_t local = py * 4u + px;
                        glm::vec3      normal {
                            static_cast<float>(xs[local]) / 127.5f - 1.0f,
                            static_cast<float>(ys[local]) / 127.5f - 1.0f,
                            0.0f,
                        };
                        if (directXNormalMap)
                            normal.y = -normal.y;
                        normal.z = std::sqrt(std::max(1.0f - (normal.x * normal.x + normal.y * normal.y), 0.0f));
                        normal   = glm::normalize(normal);

                        const size_t dst = (static_cast<size_t>(y) * width + x) * 4u;
                        outPixels[dst + 0u] = encodeNormalChannel(normal.x);
                        outPixels[dst + 1u] = encodeNormalChannel(normal.y);
                        outPixels[dst + 2u] = encodeNormalChannel(normal.z);
                        outPixels[dst + 3u] = 255u;
                    }
                }
            }
        }
        return true;
    }

    bool writeRGBA8ToKTX2(const uint32_t                                width,
                          const uint32_t                                height,
                          const std::vector<uint8_t>&                   pixels,
                          const vasset::VTextureImporter::ImportOptions& options,
                          const bool                                    compressWithBasisU,
                          vasset::VTexture&                             outTexture)
    {
        if (width == 0 || height == 0 || pixels.size() != static_cast<size_t>(width) * height * 4u)
            return false;

        ktxTexture2* ktx = nullptr;
        ktxTextureCreateInfo ci {};
        ci.baseWidth       = width;
        ci.baseHeight      = height;
        ci.baseDepth       = 1;
        ci.numLevels       = 1;
        ci.numLayers       = 1;
        ci.numFaces        = 1;
        ci.numDimensions   = 2;
        ci.isArray         = KTX_FALSE;
        ci.generateMipmaps = KTX_FALSE;
        ci.vkFormat        = static_cast<uint32_t>(vasset::VTextureFormat::eRGBA8);

        if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &ktx) != KTX_SUCCESS)
            return false;
        auto destroy = vbase::ScopeExit([&] { ktxTexture_Destroy(ktxTexture(ktx)); });

        if (ktxTexture_SetImageFromMemory(ktxTexture(ktx), 0, 0, 0, pixels.data(), pixels.size()) != KTX_SUCCESS)
            return false;

        if (compressWithBasisU)
        {
            ktxBasisParams params {};
            params.structSize       = sizeof(ktxBasisParams);
            params.uastc            = options.uastc;
            params.noSSE            = options.noSSE;
            params.qualityLevel     = options.qualityLevel;
            params.compressionLevel = options.compressionLevel;
            params.threadCount      = resolveBasisUThreadCount(options.basisUThreadCount);

            const KTX_error_code res = ktxTexture2_CompressBasisEx(ktx, &params);
            if (res != KTX_SUCCESS)
                return false;
        }

        ktx_size_t   size = 0;
        ktx_uint8_t* mem  = nullptr;
        if (ktxTexture_WriteToMemory(ktxTexture(ktx), &mem, &size) != KTX_SUCCESS)
            return false;
        auto freeMem = vbase::ScopeExit([&] { std::free(mem); });

        outTexture.width       = width;
        outTexture.height      = height;
        outTexture.depth       = 1;
        outTexture.mipLevels   = 1;
        outTexture.arrayLayers = 1;
        outTexture.isCubemap   = false;
        outTexture.type        = vasset::VTextureDimension::e2D;
        outTexture.format      = vasset::VTextureFormat::eRGBA8;
        outTexture.fileFormat  = vasset::VTextureFileFormat::eKTX2;
        outTexture.compressedBasisU = ktxTexture2_NeedsTranscoding(ktx) != 0;
        outTexture.data.assign(mem, mem + size);
        return true;
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

    enum class TextureAlphaContent
    {
        eUnknown,
        eOpaque,
        eHasNonOpaque,
    };

    TextureAlphaContent inspectByteAlpha(std::span<const uint8_t> bytes, size_t alphaOffset, size_t stride)
    {
        if (bytes.empty() || stride == 0 || alphaOffset >= stride)
            return TextureAlphaContent::eUnknown;

        constexpr uint8_t alphaMaskThreshold = 250u;
        const size_t      pixelCount         = bytes.size() / stride;
        const size_t      step               = std::max<size_t>(1u, pixelCount / 4096u);
        for (size_t pixel = 0; pixel < pixelCount; pixel += step)
        {
            if (bytes[pixel * stride + alphaOffset] < alphaMaskThreshold)
                return TextureAlphaContent::eHasNonOpaque;
        }
        return TextureAlphaContent::eOpaque;
    }

    TextureAlphaContent inspectSTBTextureAlpha(const std::filesystem::path& path)
    {
        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.generic_string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels)
            return TextureAlphaContent::eUnknown;

        auto freePixels = vbase::ScopeExit([&] { stbi_image_free(pixels); });
        if (width <= 0 || height <= 0)
            return TextureAlphaContent::eUnknown;

        const auto byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        return inspectByteAlpha(std::span<const uint8_t>(pixels, byteCount), 3u, 4u);
    }

    TextureAlphaContent inspectBC2Alpha(const ddsktx_sub_data& subData)
    {
        if (!subData.buff || subData.size_bytes <= 0)
            return TextureAlphaContent::eUnknown;

        const auto*  blocks     = static_cast<const uint8_t*>(subData.buff);
        const size_t blockCount = static_cast<size_t>(subData.size_bytes) / 16u;
        const size_t step       = std::max<size_t>(1u, blockCount / 4096u);
        for (size_t blockIndex = 0; blockIndex < blockCount; blockIndex += step)
        {
            const uint8_t* block = blocks + blockIndex * 16u;
            for (uint32_t byteIndex = 0; byteIndex < 8u; ++byteIndex)
            {
                const uint8_t packed = block[byteIndex];
                if ((packed & 0x0fu) < 0x0fu || ((packed >> 4u) & 0x0fu) < 0x0fu)
                    return TextureAlphaContent::eHasNonOpaque;
            }
        }
        return TextureAlphaContent::eOpaque;
    }

    TextureAlphaContent inspectBC3Alpha(const ddsktx_sub_data& subData)
    {
        if (!subData.buff || subData.size_bytes <= 0)
            return TextureAlphaContent::eUnknown;

        constexpr uint8_t alphaMaskThreshold = 250u;
        const auto*       blocks             = static_cast<const uint8_t*>(subData.buff);
        const size_t      blockCount         = static_cast<size_t>(subData.size_bytes) / 16u;
        const size_t      step               = std::max<size_t>(1u, blockCount / 4096u);
        for (size_t blockIndex = 0; blockIndex < blockCount; blockIndex += step)
        {
            const auto alphas = decodeBC4Block(blocks + blockIndex * 16u);
            for (const uint8_t alpha : alphas)
            {
                if (alpha < alphaMaskThreshold)
                    return TextureAlphaContent::eHasNonOpaque;
            }
        }
        return TextureAlphaContent::eOpaque;
    }

    TextureAlphaContent inspectDDSKTXTextureAlpha(const std::filesystem::path& path)
    {
        const auto bytes = readAll(path);
        if (bytes.empty())
            return TextureAlphaContent::eUnknown;

        ddsktx_texture_info tc {};
        if (!ddsktx_parse(&tc, bytes.data(), static_cast<int>(bytes.size()), nullptr))
            return TextureAlphaContent::eUnknown;

        if ((tc.flags & DDSKTX_TEXTURE_FLAG_ALPHA) == 0 && tc.format != DDSKTX_FORMAT_BC2 &&
            tc.format != DDSKTX_FORMAT_BC3)
        {
            return TextureAlphaContent::eOpaque;
        }

        ddsktx_sub_data subData {};
        ddsktx_get_sub(&tc, &subData, bytes.data(), static_cast<int>(bytes.size()), 0, 0, 0);
        if (!subData.buff || subData.width <= 0 || subData.height <= 0)
            return TextureAlphaContent::eUnknown;

        if (tc.format == DDSKTX_FORMAT_BC2)
            return inspectBC2Alpha(subData);
        if (tc.format == DDSKTX_FORMAT_BC3)
            return inspectBC3Alpha(subData);
        if (tc.format == DDSKTX_FORMAT_RGBA8 || tc.format == DDSKTX_FORMAT_BGRA8)
        {
            std::vector<uint8_t> alphaSample;
            alphaSample.reserve(static_cast<size_t>(subData.width) * static_cast<size_t>(subData.height) * 4u);
            const auto* rows = static_cast<const uint8_t*>(subData.buff);
            for (int y = 0; y < subData.height; ++y)
            {
                const uint8_t* row = rows + static_cast<size_t>(y) * static_cast<size_t>(subData.row_pitch_bytes);
                alphaSample.insert(alphaSample.end(), row, row + static_cast<size_t>(subData.width) * 4u);
            }
            return inspectByteAlpha(alphaSample, 3u, 4u);
        }

        return TextureAlphaContent::eUnknown;
    }

    TextureAlphaContent inspectTextureAlpha(const std::filesystem::path& path)
    {
        const auto ext = toLowerAscii(path.extension().generic_string());
        if (isSTB(ext))
            return inspectSTBTextureAlpha(path);
        if (isKTXDDS(ext))
            return inspectDDSKTXTextureAlpha(path);
        return TextureAlphaContent::eUnknown;
    }

    uint64_t hashBytes(const void* data, size_t size, uint64_t seed = 0)
    {
        return XXH3_64bits_withSeed(data, size, seed);
    }

    uint64_t hashString(const std::string& value, uint64_t seed)
    {
        return hashBytes(value.data(), value.size(), seed);
    }

    uint64_t hashFile(const std::filesystem::path& path, uint64_t seed)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || std::filesystem::is_directory(path, ec))
            return hashString("<missing>:" + path.generic_string(), seed);

        const auto bytes = readAll(path);
        return hashBytes(bytes.data(), bytes.size(), seed);
    }

    uint64_t hashU64(uint64_t value, uint64_t seed)
    {
        return hashBytes(&value, sizeof(value), seed);
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
        for (char i : value)
        {
            hash ^= static_cast<uint8_t>(i);
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
        std::string baseName =
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

    std::string shortenImportedAssetKey(std::string key, const size_t maxLength = 160)
    {
        if (key.size() <= maxLength)
            return key;

        const std::string suffix = "_" + stableShortHash(key);
        const size_t      headLength = maxLength > suffix.size() ? maxLength - suffix.size() : 0u;
        key.resize(headLength);
        while (!key.empty() && (key.back() == '_' || key.back() == '-' || key.back() == '.'))
            key.pop_back();
        return key + suffix;
    }

    vasset::VAssetType inferSourceTextAssetType(const std::filesystem::path& path)
    {
        if (isValidShaderLibraryManifest(path))
            return vasset::VAssetType::eShaderLibraryManifest;
        if (isValidScriptableObjectLua(path))
            return vasset::VAssetType::eScriptableObjectLua;
        if (isValidRenderGraphJson(path))
            return vasset::VAssetType::eRenderGraphJson;
        if (isValidMaterialGraphJson(path))
            return vasset::VAssetType::eMaterialGraphJson;
        if (isValidAnimatorGraphJson(path))
            return vasset::VAssetType::eAnimatorGraphJson;
        if (isValidMaterialJson(path))
            return vasset::VAssetType::eMaterial;
        const auto ext = path.extension().generic_string();
        if (ext == ".vscn")
            return vasset::VAssetType::eScene;
        if (ext == ".vmanifest")
            return vasset::VAssetType::eSceneManifest;
        if (ext == ".vprefab")
            return vasset::VAssetType::ePrefab;
        if (ext == ".lua")
            return vasset::VAssetType::eScriptLua;
        return vasset::VAssetType::eUnknown;
    }

    struct ShaderLibraryManifest
    {
        std::string              name;
        std::string              root;
        std::string              generatedRoot;
        std::vector<std::string> generatedRoots;
        std::string              generatedPrefix;
        std::string              keywords;
        std::vector<std::string> shaders;
    };

    struct ShaderManifestRoot
    {
        std::filesystem::path root;
        std::string           virtualPrefix;
    };

    struct ShaderManifestSource
    {
        std::filesystem::path root;
        std::string           relativePath;
        std::string           virtualPath;
    };

    std::string bytesToString(const std::vector<uint8_t>& bytes)
    {
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    std::optional<std::string> parseManifestStringField(const std::string& text, const char* field)
    {
        const std::regex pattern(std::string(field) + R"(\s*=\s*["']([^"']+)["'])");
        std::smatch      match;
        if (std::regex_search(text, match, pattern) && match.size() >= 2)
            return match[1].str();
        return std::nullopt;
    }

    std::vector<std::string> parseManifestStringArrayField(const std::string& text, const char* field)
    {
        const std::regex fieldPattern(std::string(field) + R"(\s*=\s*\{([\s\S]*?)\})");
        std::smatch      fieldMatch;
        if (!std::regex_search(text, fieldMatch, fieldPattern) || fieldMatch.size() < 2)
            return {};

        std::vector<std::string> out;
        const std::string        body = fieldMatch[1].str();
        const std::regex         itemPattern(R"(["']([^"']+)["'])");
        for (std::sregex_iterator it(body.begin(), body.end(), itemPattern), end; it != end; ++it)
            out.push_back((*it)[1].str());
        return out;
    }

    std::optional<ShaderLibraryManifest> parseShaderLibraryManifest(const std::filesystem::path& path)
    {
        const auto bytes = readAll(path);
        if (bytes.empty() && !std::filesystem::exists(path))
            return std::nullopt;

        const auto text = bytesToString(bytes);
        ShaderLibraryManifest manifest;
        manifest.name = parseManifestStringField(text, "name").value_or(path.stem().stem().generic_string());
        manifest.root =
            parseManifestStringField(text, "root").value_or(path.parent_path().filename().generic_string());
        manifest.generatedRoot  = parseManifestStringField(text, "generatedRoot").value_or(std::string {});
        manifest.generatedRoots = parseManifestStringArrayField(text, "generatedRoots");
        manifest.generatedPrefix = parseManifestStringField(text, "generatedPrefix").value_or(std::string {});
        manifest.keywords       = parseManifestStringField(text, "keywords").value_or(std::string {});
        manifest.shaders        = parseManifestStringArrayField(text, "shaders");
        if (manifest.shaders.empty())
            manifest.shaders.push_back("**/*.vshader");

        if (manifest.name.empty() || manifest.root.empty())
            return std::nullopt;
        return manifest;
    }

    std::string globToRegex(std::string glob)
    {
        std::string out = "^";
        for (size_t i = 0; i < glob.size(); ++i)
        {
            const char ch = glob[i];
            if (ch == '*')
            {
                if (i + 1 < glob.size() && glob[i + 1] == '*')
                {
                    if (i + 2 < glob.size() && (glob[i + 2] == '/' || glob[i + 2] == '\\'))
                    {
                        out += "(?:.*/)?";
                        i += 2;
                    }
                    else
                    {
                        out += ".*";
                        ++i;
                    }
                }
                else
                {
                    out += "[^/]*";
                }
            }
            else if (ch == '?')
            {
                out += '.';
            }
            else
            {
                if (std::string(".^$+()[]{}|\\").find(ch) != std::string::npos)
                    out += '\\';
                out += ch == '\\' ? '/' : ch;
            }
        }
        out += "$";
        return out;
    }

    bool matchesGlob(const std::string& rel, const std::string& glob)
    {
        return std::regex_match(rel, std::regex(globToRegex(glob)));
    }

    std::vector<std::string> collectShaderManifestFiles(const std::filesystem::path& shaderRoot,
                                                        const std::vector<std::string>& patterns)
    {
        std::vector<std::string> out;
        if (!std::filesystem::exists(shaderRoot) || !std::filesystem::is_directory(shaderRoot))
            return out;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderRoot))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".vshader")
                continue;

            auto rel = std::filesystem::relative(entry.path(), shaderRoot).generic_string();
            for (const auto& pattern : patterns)
            {
                if (matchesGlob(rel, pattern))
                {
                    out.push_back(std::move(rel));
                    break;
                }
            }
        }
        std::ranges::sort(out);
        return out;
    }

    std::string normalizeShaderVirtualPrefix(std::string prefix)
    {
        std::ranges::replace(prefix, '\\', '/');
        while (!prefix.empty() && prefix.front() == '/')
            prefix.erase(prefix.begin());
        while (!prefix.empty() && prefix.back() == '/')
            prefix.pop_back();
        return prefix;
    }

    std::vector<ShaderManifestRoot>
    shaderLibrarySourceRoots(const std::filesystem::path& assetRoot, const ShaderLibraryManifest& manifest)
    {
        std::vector<ShaderManifestRoot> roots;
        roots.push_back(ShaderManifestRoot {
            .root          = (assetRoot / manifest.root).lexically_normal(),
            .virtualPrefix = {},
        });

        const auto implicitGeneratedRoot = "../.vultra/generated/shaders";
        const auto generatedPrefix =
            normalizeShaderVirtualPrefix(manifest.generatedPrefix.empty() ? "generated" : manifest.generatedPrefix);
        auto appendRoot = [&](const std::string& root) {
            if (!root.empty())
            {
                roots.push_back(ShaderManifestRoot {
                    .root          = (assetRoot / root).lexically_normal(),
                    .virtualPrefix = generatedPrefix,
                });
            }
        };
        appendRoot(implicitGeneratedRoot);
        appendRoot(manifest.generatedRoot);
        for (const auto& root : manifest.generatedRoots)
            appendRoot(root);

        return roots;
    }

    std::vector<ShaderManifestSource>
    collectShaderLibrarySources(const std::filesystem::path& assetRoot, const ShaderLibraryManifest& manifest)
    {
        std::vector<ShaderManifestSource> out;
        std::unordered_set<std::string>   seenVirtualPaths;

        for (const auto& root : shaderLibrarySourceRoots(assetRoot, manifest))
        {
            for (const auto& shader : collectShaderManifestFiles(root.root, manifest.shaders))
            {
                const auto virtualPath = root.virtualPrefix.empty() ? shader : root.virtualPrefix + "/" + shader;
                if (!seenVirtualPaths.insert(virtualPath).second)
                    continue;
                out.push_back(ShaderManifestSource {
                    .root         = root.root,
                    .relativePath = shader,
                    .virtualPath  = virtualPath,
                });
            }
        }
        return out;
    }

    uint64_t shaderLibraryDependencyHash(
        const std::filesystem::path& assetRoot,
        const ShaderLibraryManifest&  manifest,
        const std::vector<vasset::VAssetImporter::ImportOptions::ShaderVirtualIncludeFile>& virtualIncludes)
    {
        uint64_t h = hashString("shader-library-deps");

        if (!manifest.keywords.empty())
            h = hashFile(assetRoot / manifest.keywords, h);

        for (const auto& source : collectShaderLibrarySources(assetRoot, manifest))
        {
            h = hashString(source.virtualPath, h);
            h = hashFile(source.root / source.relativePath, h);
        }

        for (const auto& include : virtualIncludes)
        {
            h = hashString(include.virtualPath, h);
            h = hashString(include.sourceText, h);
        }

        return h;
    }

    void emitShaderDiagnostic(const std::function<void(const vasset::VAssetImporter::ImportOptions::Diagnostic&)>& emit,
                              const std::string&                                                                path,
                              const std::string&                                                                message)
    {
        if (!emit)
            return;

        size_t      line {0};
        size_t      column {0};
        std::smatch match;
        const std::regex patterns[] = {
            std::regex(R"((?:line|:)\s*([0-9]+)\s*(?::|,|\))\s*([0-9]+)?)", std::regex::icase),
            std::regex(R"(([0-9]+)\s*:\s*([0-9]+))"),
        };
        for (const auto& pattern : patterns)
        {
            if (!std::regex_search(message, match, pattern) || match.size() < 2)
                continue;
            line = static_cast<size_t>(std::stoull(match[1].str()));
            if (match.size() > 2 && match[2].matched)
                column = static_cast<size_t>(std::stoull(match[2].str()));
            break;
        }

        emit(vasset::VAssetImporter::ImportOptions::Diagnostic {
            .path    = path,
            .line    = line,
            .column  = column,
            .message = message,
        });
    }

    bool runShaderCompiler(
        const std::filesystem::path& assetRoot,
        const ShaderLibraryManifest&  manifest,
        const std::filesystem::path&  output,
        const bool                    webgpu,
        const std::vector<vasset::VAssetImporter::ImportOptions::ShaderVirtualIncludeFile>& virtualIncludes,
        const std::function<void(const vasset::VAssetImporter::ImportOptions::Diagnostic&)>& diagnostics)
    {
        const auto sources = collectShaderLibrarySources(assetRoot, manifest);
        if (sources.empty())
            return false;

        std::vector<uint8_t> engineKeywordsBytes;
        if (!manifest.keywords.empty())
            engineKeywordsBytes = readAll(assetRoot / manifest.keywords);

        std::optional<vshadersystem::EngineKeywordsFile> engineKeywords;
        if (!engineKeywordsBytes.empty())
        {
            const auto text = bytesToString(engineKeywordsBytes);
            auto       parsed = vshadersystem::parse_engine_keywords_vkw(text);
            if (!parsed.isOk())
            {
                const auto message = parsed.error().message;
                std::cerr << "[vasset] failed to parse shader keywords: " << manifest.keywords << " (" << message
                          << ")" << std::endl;
                emitShaderDiagnostic(diagnostics, manifest.keywords, message);
                return false;
            }
            engineKeywords = std::move(parsed.value());
        }

        std::vector<vshadersystem::ShaderLibraryEntry> entries;
        const auto sourceRoots = shaderLibrarySourceRoots(assetRoot, manifest);

        // Builtin GLSL includes as a single root VFS mount: they resolve by
        // absolute path (e.g. "include/vultra/mesh_material.glsl") from any
        // shader directory, including generated/material-graph shaders.
        vshadersystem::VfsMount builtinMount;
        builtinMount.files.reserve(virtualIncludes.size());
        for (const auto& include : virtualIncludes)
            builtinMount.files.push_back({include.virtualPath, include.sourceText});

        for (const auto& source : sources)
        {
            const auto shaderPath = source.root / source.relativePath;
            const auto sourceBytes = readAll(shaderPath);
            if (sourceBytes.empty())
            {
                std::cerr << "[vasset] shader source is empty or unreadable: " << shaderPath.generic_string()
                          << std::endl;
                return false;
            }

            const auto sourceText = bytesToString(sourceBytes);
            const bool isVultraMeshMaterialShader =
                sourceText.find("vultra/mesh_material.glsl") != std::string::npos;
            const bool hasVshaderProperties = sourceText.find("[properties]") != std::string::npos;

            vshadersystem::BuildRequest request;
            request.source.virtualPath = source.virtualPath;
            request.source.sourceText = sourceText;
            request.options.language = vshadersystem::ShaderLanguage::eGLSL;
            request.options.webgpuProfile = webgpu;
            request.options.materialAccessMode = vshadersystem::MaterialAccessMode::eSSBO;
            if (isVultraMeshMaterialShader && hasVshaderProperties)
            {
                request.options.materialInjection = vshadersystem::CompileOptions::MaterialAccessInjection {
                    .postMaterialDecl =
                        "layout(set = 1, binding = 1, std430) readonly buffer VultraMaterialBlock\n"
                        "{\n"
                        "    Material vshader_Material;\n"
                        "};\n"
                        "Material vshader_LoadMaterial() { return vshader_Material; }\n",
                    .bindlessTextureArrayName = "u_BindlessTextures",
                    .macroPrefix              = "VULTRA_",
                };
            }
            for (const auto& root : sourceRoots)
                request.options.includeDirs.push_back(root.root.generic_string());
            request.options.vfsMounts.push_back(builtinMount);
            request.enableCache = false;
            if (engineKeywords)
            {
                request.hasEngineKeywords = true;
                request.engineKeywords = *engineKeywords;
            }

            auto built = vshadersystem::build_multiple_shaders(request);
            if (!built.isOk())
            {
                const auto message = built.error().message;
                std::cerr << "[vasset] shader compile failed: " << source.virtualPath << " (" << message << ")"
                          << std::endl;
                emitShaderDiagnostic(diagnostics, source.virtualPath, message);
                return false;
            }

            for (auto& [stage, result] : built.value())
            {
                auto blob = vshadersystem::write_vshbin(result.binary);
                if (!blob.isOk())
                {
                    std::cerr << "[vasset] failed to serialize shader binary: " << source.virtualPath << " ("
                              << blob.error().message << ")" << std::endl;
                    emitShaderDiagnostic(diagnostics, source.virtualPath, blob.error().message);
                    return false;
                }

                entries.push_back(vshadersystem::ShaderLibraryEntry {
                    .keyHash = result.binary.variantHash,
                    .stage   = stage,
                    .blob    = std::move(blob.value()),
                });
            }
        }

        if (entries.empty())
            return false;
        std::filesystem::create_directories(output.parent_path());
        auto result = vshadersystem::write_vslib(output.generic_string(),
                                                 entries,
                                                 engineKeywordsBytes.empty() ? nullptr : &engineKeywordsBytes);
        if (!result.isOk())
        {
            const auto message = result.error().message;
            std::cerr << "[vasset] failed to write shader library: " << output.generic_string() << " (" << message
                      << ")" << std::endl;
            emitShaderDiagnostic(diagnostics, manifest.name, message);
        }
        return result.isOk();
    }

    bool shaderLibraryOutputsAreCurrent(const std::filesystem::path& assetRoot,
                                        const std::filesystem::path& manifestPath,
                                        const ShaderLibraryManifest& manifest,
                                        const std::filesystem::path& outVulkan,
                                        const std::filesystem::path& outWebGpu)
    {
        namespace fs = std::filesystem;

        std::error_code ec;
        if (!fs::exists(outVulkan, ec) || !fs::exists(outWebGpu, ec))
            return false;

        const auto outTime = std::min(fs::last_write_time(outVulkan, ec), fs::last_write_time(outWebGpu, ec));
        if (ec)
            return false;

        auto dependencyIsNewer = [&](const fs::path& path) {
            std::error_code depEc;
            if (!fs::exists(path, depEc))
                return false;
            const auto depTime = fs::last_write_time(path, depEc);
            return !depEc && depTime > outTime;
        };

        if (dependencyIsNewer(manifestPath))
            return false;
        if (!manifest.keywords.empty() && dependencyIsNewer(assetRoot / manifest.keywords))
            return false;

        for (const auto& source : collectShaderLibrarySources(assetRoot, manifest))
        {
            if (dependencyIsNewer(source.root / source.relativePath))
                return false;
        }

        return true;
    }

    vbase::Result<void, vasset::AssetError> saveSourceVImport(const std::filesystem::path& assetRoot,
                                                              const std::string&           relativeSrcPath,
                                                              const vasset::VImport&       vimport)
    {
        auto sourceSidecarPath = assetRoot / relativeSrcPath;
        sourceSidecarPath.replace_extension(".vimport");
        return saveVImport(vimport, sourceSidecarPath.generic_string());
    }

    vbase::Result<vbase::UUID, vasset::AssetError>
    importShaderLibraryManifest(
        vasset::VAssetRegistry& registry,
        vbase::StringView       filePath,
        bool                    forceReimport,
        const std::vector<vasset::VAssetImporter::ImportOptions::ShaderVirtualIncludeFile>& virtualIncludes,
        const std::function<void(const vasset::VAssetImporter::ImportOptions::Diagnostic&)>& diagnostics)
    {
        namespace fs = std::filesystem;

        fs::path osPath {std::string(filePath)};
        if (!fs::exists(osPath) || fs::is_directory(osPath))
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eImportFailed);

        const auto manifest = parseShaderLibraryManifest(osPath);
        if (!manifest)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eInvalidFormat);

        const std::string relativeSrcPath = registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string importedBase =
            registry.getImportedAssetPath(vasset::VAssetType::eShaderLibrary, manifest->name, true);
        const std::string relativeImportedPath = fs::path(importedBase).replace_extension(".vshlib").generic_string();

        const auto assetRoot = fs::path(registry.getAssetRootPath());
        const auto outVulkan = assetRoot / relativeImportedPath;
        const auto outWebGpu = fs::path(outVulkan).replace_extension(".vshweblib");
        const auto relativeWebGpuPath = fs::relative(outWebGpu, assetRoot).generic_string();

        const auto lookupUUID = vbase::uuid_from_string_key(relativeImportedPath);
        vasset::VImport vimport {};
        vimport.importer = vasset::toString(vasset::VAssetType::eShaderLibrary);
        vimport.uid      = lookupUUID;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;

        const uint64_t sourceHash     = hashFile(osPath);
        const uint64_t dependencyHash = shaderLibraryDependencyHash(assetRoot, *manifest, virtualIncludes);
        constexpr uint64_t paramsHash = 0;
        constexpr auto importerVersion = "shader_library:2";
        constexpr auto outputSchema = "vshlib:v4+vshweblib:1";

        auto       entry      = registry.lookup(lookupUUID);
        if (entry.type != vasset::VAssetType::eUnknown && !forceReimport &&
            shaderLibraryOutputsAreCurrent(assetRoot, osPath, *manifest, outVulkan, outWebGpu))
        {
            if (importDatabaseRecordIsCurrent(registry,
                                              lookupUUID,
                                              vasset::toString(vasset::VAssetType::eShaderLibrary),
                                              relativeSrcPath,
                                              relativeImportedPath,
                                              importerVersion,
                                              outputSchema,
                                              sourceHash,
                                              dependencyHash,
                                              paramsHash,
                                              {relativeImportedPath, relativeWebGpuPath}))
            {
                (void)updateImportDatabaseRecord(registry,
                                                 lookupUUID,
                                                 vasset::toString(vasset::VAssetType::eShaderLibrary),
                                                 relativeSrcPath,
                                                 relativeImportedPath,
                                                 importerVersion,
                                                 outputSchema,
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);
                // Editor sidecars live next to source files; imported/ contains cooked outputs only.
                auto sr_import = saveSourceVImport(assetRoot, relativeSrcPath, vimport);
                if (!sr_import)
                    return vbase::Result<vbase::UUID, vasset::AssetError>::err(sr_import.error());
                return vbase::Result<vbase::UUID, vasset::AssetError>::ok(lookupUUID);
            }
        }

        if (!runShaderCompiler(assetRoot, *manifest, outVulkan, false, virtualIncludes, diagnostics))
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eImportFailed);
        if (!runShaderCompiler(assetRoot, *manifest, outWebGpu, true, virtualIncludes, diagnostics))
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eImportFailed);

        auto sr_import = saveSourceVImport(assetRoot, relativeSrcPath, vimport);
        if (!sr_import)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(sr_import.error());

        auto rr = registry.registerAsset(lookupUUID, relativeSrcPath, relativeImportedPath, vasset::VAssetType::eShaderLibrary);
        if (!rr)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(rr.error());

        auto dr = updateImportDatabaseRecord(registry,
                                             lookupUUID,
                                             vasset::toString(vasset::VAssetType::eShaderLibrary),
                                             relativeSrcPath,
                                             relativeImportedPath,
                                             importerVersion,
                                             outputSchema,
                                             sourceHash,
                                             dependencyHash,
                                             paramsHash);
        if (!dr)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(dr.error());

        return vbase::Result<vbase::UUID, vasset::AssetError>::ok(lookupUUID);
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
        vasset::VImport vimport {};
        vimport.importer = vasset::toString(type);
        vimport.uid      = lookupUUID;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;

        const uint64_t sourceHash     = hashFile(osPath);
        constexpr uint64_t dependencyHash = 0;
        constexpr uint64_t paramsHash = 0;
        constexpr auto importerVersion = "source_text:1";
        constexpr auto outputSchema = "copy:1";

        const auto srcBytes = readAll(osPath);
        if (srcBytes.empty() && !fs::exists(osPath))
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eImportFailed);
        const auto sourceText = bytesToString(srcBytes);

        auto entry      = registry.lookup(lookupUUID);
        if (entry.type != vasset::VAssetType::eUnknown && !forceReimport &&
            outputFilesAreNewerThanSource(registry, osPath, {relativeImportedPath}))
        {
            if (importDatabaseRecordIsCurrent(registry,
                                              lookupUUID,
                                              vasset::toString(type),
                                              relativeSrcPath,
                                              relativeImportedPath,
                                              importerVersion,
                                              outputSchema,
                                              sourceHash,
                                              dependencyHash,
                                              paramsHash,
                                              {relativeImportedPath}))
            {
                (void)updateImportDatabaseRecord(registry,
                                                 lookupUUID,
                                                 vasset::toString(type),
                                                 relativeSrcPath,
                                                 relativeImportedPath,
                                                 importerVersion,
                                                 outputSchema,
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);
                // Keep source sidecars available even when the cooked copy is already current.
                auto sr_import = saveSourceVImport(fs::path(registry.getAssetRootPath()), relativeSrcPath, vimport);
                if (!sr_import)
                    return vbase::Result<vbase::UUID, vasset::AssetError>::err(sr_import.error());
                registry.setDependencies(lookupUUID, collectSourceTextDependencies(registry, sourceText, type));
                return vbase::Result<vbase::UUID, vasset::AssetError>::ok(lookupUUID);
            }
        }

        const fs::path importedPath = fs::path(registry.getAssetRootPath()) / relativeImportedPath;
        if (!writeAll(importedPath, srcBytes))
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(vasset::AssetError::eIOError);

        auto sr_import = saveSourceVImport(fs::path(registry.getAssetRootPath()), relativeSrcPath, vimport);
        if (!sr_import)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(sr_import.error());

        auto rr = registry.registerAsset(lookupUUID, relativeSrcPath, relativeImportedPath, type);
        if (!rr)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(rr.error());
        registry.setDependencies(lookupUUID, collectSourceTextDependencies(registry, sourceText, type));

        auto dr = updateImportDatabaseRecord(registry,
                                             lookupUUID,
                                             vasset::toString(type),
                                             relativeSrcPath,
                                             relativeImportedPath,
                                             importerVersion,
                                             outputSchema,
                                             sourceHash,
                                             dependencyHash,
                                             paramsHash);
        if (!dr)
            return vbase::Result<vbase::UUID, vasset::AssetError>::err(dr.error());

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
                    if (prop->mData && prop->mDataLength >= sizeof(uint32_t))
                    {
                        if (prop->mDataLength >= sizeof(aiString))
                        {
                            std::memcpy(&value, prop->mData, sizeof(aiString));
                            value.length = std::min(value.length, static_cast<ai_uint32>(AI_MAXLEN - 1u));
                            value.data[value.length] = '\0';
                            entry.value = value;
                        }
                        else
                        {
                            uint32_t len = 0;
                            std::memcpy(&len, prop->mData, sizeof(len));
                            len = std::min<uint32_t>(len, prop->mDataLength - sizeof(len));
                            len = std::min<uint32_t>(len, static_cast<uint32_t>(AI_MAXLEN - 1u));
                            if (len > 0)
                                std::memcpy(value.data, prop->mData + sizeof(len), len);
                            value.length     = len;
                            value.data[len]  = '\0';
                            entry.value      = value;
                        }
                    }
                    break;
                }
                case aiPTI_Float: {
                    if (!prop->mData || prop->mDataLength < sizeof(float))
                        break;

                    const auto floatCount = prop->mDataLength / sizeof(float);
                    if (floatCount == 3)
                    {
                        aiColor3D value;
                        std::memcpy(&value, prop->mData, sizeof(value));
                        entry.value = value;
                    }
                    else if (floatCount == 4)
                    {
                        aiColor4D value;
                        std::memcpy(&value, prop->mData, sizeof(value));
                        entry.value = value;
                    }
                    else
                    {
                        float value = 0.0f;
                        std::memcpy(&value, prop->mData, sizeof(value));
                        entry.value = value;
                    }
                    break;
                }
                case aiPTI_Double: {
                    if (prop->mData && prop->mDataLength >= sizeof(double))
                    {
                        double value = 0.0;
                        std::memcpy(&value, prop->mData, sizeof(value));
                        entry.value = value;
                    }
                    break;
                }
                case aiPTI_Integer: {
                    if (prop->mData && prop->mDataLength >= sizeof(int))
                    {
                        int value = 0;
                        std::memcpy(&value, prop->mData, sizeof(value));
                        entry.value = value;
                    }
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

            if constexpr (std::is_same_v<T, float>)
            {
                if (auto p = std::get_if<double>(&it->second.value))
                {
                    out = static_cast<float>(*p);
                    return true;
                }
                if (auto p = std::get_if<int>(&it->second.value))
                {
                    out = static_cast<float>(*p);
                    return true;
                }
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                if (auto p = std::get_if<float>(&it->second.value))
                {
                    out = static_cast<double>(*p);
                    return true;
                }
                if (auto p = std::get_if<int>(&it->second.value))
                {
                    out = static_cast<double>(*p);
                    return true;
                }
            }
            else if constexpr (std::is_same_v<T, bool>)
            {
                if (auto p = std::get_if<int>(&it->second.value))
                {
                    out = *p != 0;
                    return true;
                }
            }
            else if constexpr (std::is_same_v<T, aiBlendMode>)
            {
                if (auto p = std::get_if<int>(&it->second.value))
                {
                    out = static_cast<aiBlendMode>(*p);
                    return true;
                }
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

        VImport     sourceVImport {};
        auto        sourceSidecarPath = osPath;
        sourceSidecarPath.replace_extension(".vimport");
        if (auto loaded = loadVImport(sourceSidecarPath.generic_string()))
            sourceVImport = std::move(loaded.value());
        const auto resolvedParams = resolveTextureImportParams(sourceVImport.params, m_Options);
        const auto& options       = resolvedParams.options;

        const auto lookupUUID = vbase::uuid_from_string_key(relativeImportedPath);
        const uint64_t sourceHash     = hashFile(osPath);
        const bool likelyNormalMap = isLikelyNormalMapPath(osPath);
        const auto importHintReadme = likelyNormalMap ? findImportHintReadme(osPath) : std::filesystem::path {};
        const bool inferredDirectXNormalMap = readmeDeclaresDirectXNormal(importHintReadme);
        const uint64_t dependencyHash = likelyNormalMap && !importHintReadme.empty() ? hashFile(importHintReadme) : 0;
        const uint64_t paramsHash     = textureImportParamsHash(options);
        constexpr auto importerVersion = "texture:5";
        constexpr auto outputSchema = "vtexture:1";

        auto       entry      = m_Registry.lookup(lookupUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport &&
            outputFilesAreNewerThanSource(m_Registry, osPath, {relativeImportedPath}))
        {
            if (importDatabaseRecordIsCurrent(m_Registry,
                                              lookupUUID,
                                              toString(VAssetType::eTexture),
                                              relativeSrcPath,
                                              relativeImportedPath,
                                              importerVersion,
                                              outputSchema,
                                              sourceHash,
                                              dependencyHash,
                                              paramsHash,
                                              {relativeImportedPath}))
            {
                (void)updateImportDatabaseRecord(m_Registry,
                                                 lookupUUID,
                                                 toString(VAssetType::eTexture),
                                                 relativeSrcPath,
                                                 relativeImportedPath,
                                                 importerVersion,
                                                 outputSchema,
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);
                // Load existing texture
                std::cout << "Texture already imported: " << entry.sourcePath << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
            }
        }

        // Set UUID
        outTexture.uuid = vbase::uuid_from_string_key(relativeImportedPath);

        // Default target format
        auto targetFormat = options.targetTextureFileFormat;

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

            const bool bakeBC5NormalMap =
                tc.format == DDSKTX_FORMAT_BC5 && (options.bakeNormalMap || likelyNormalMap);
            if (bakeBC5NormalMap)
            {
                std::vector<uint8_t> bakedPixels;
                const bool compressBakedNormalWithBasisU =
                    options.targetTextureFileFormat == VTextureFileFormat::eKTX2;
                if (!bakeBC5NormalToRGBA8(tc,
                                          reinterpret_cast<const uint8_t*>(fileBytes.data()),
                                          fileBytes.size(),
                                          options.directXNormalMap || inferredDirectXNormalMap,
                                          bakedPixels) ||
                    !writeRGBA8ToKTX2(static_cast<uint32_t>(tc.width),
                                      static_cast<uint32_t>(tc.height),
                                      bakedPixels,
                                      m_Options,
                                      compressBakedNormalWithBasisU,
                                      outTexture))
                {
                    return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                }

                goto SUCCESS;
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

                stbi_set_flip_vertically_on_load(options.flipY ? 1 : 0);

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

            bool wasDownscaled = false;
            std::vector<uint8_t> downscaled8;
            std::vector<float>   downscaledf;
            void*                sourcePixels = pixelGuard.p;

            if (options.downscaleLargeTextures && options.downscaleTargetDimension > 0)
            {
                const uint32_t longestEdge = static_cast<uint32_t>(std::max(width, height));
                if (longestEdge > options.downscaleMinDimension)
                {
                    const double scale =
                        static_cast<double>(options.downscaleTargetDimension) / static_cast<double>(longestEdge);
                    const int nextWidth  = std::max(1, static_cast<int>(std::round(static_cast<double>(width) * scale)));
                    const int nextHeight =
                        std::max(1, static_cast<int>(std::round(static_cast<double>(height) * scale)));

                    if (hdr)
                    {
                        downscaledf.resize(static_cast<size_t>(nextWidth) * nextHeight * channels);
                        if (!stbir_resize_float_linear(reinterpret_cast<const float*>(pixelGuard.p),
                                                       width,
                                                       height,
                                                       0,
                                                       downscaledf.data(),
                                                       nextWidth,
                                                       nextHeight,
                                                       0,
                                                       static_cast<stbir_pixel_layout>(STBIR_4CHANNEL)))
                        {
                            std::cerr << "Failed to downscale large float texture." << std::endl;
                            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                        }
                        sourcePixels = downscaledf.data();
                    }
                    else
                    {
                        downscaled8.resize(static_cast<size_t>(nextWidth) * nextHeight * channels);
                        if (!stbir_resize_uint8_srgb(reinterpret_cast<const unsigned char*>(pixelGuard.p),
                                                     width,
                                                     height,
                                                     0,
                                                     downscaled8.data(),
                                                     nextWidth,
                                                     nextHeight,
                                                     0,
                                                     static_cast<stbir_pixel_layout>(STBIR_4CHANNEL)))
                        {
                            std::cerr << "Failed to downscale large texture." << std::endl;
                            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                        }
                        sourcePixels = downscaled8.data();
                    }

                    std::cout << "Downscaled large texture " << osPath.filename().generic_string() << " from "
                              << width << "x" << height << " to " << nextWidth << "x" << nextHeight << std::endl;
                    width         = nextWidth;
                    height        = nextHeight;
                    wasDownscaled = true;
                    targetFormat  = VTextureFileFormat::eKTX2;
                }
            }

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
                if (options.compressOnlyLargeTextures)
                {
                    const uint32_t longestEdge = static_cast<uint32_t>(std::max(width, height));
                    const bool     largeEnoughResolution = longestEdge >= options.basisUCompressMinDimension;
                    const bool     largeEnoughSourceSize =
                        !fileSizeEc && sourceFileBytes >= options.basisUCompressMinSourceBytes;
                    shouldCompressWithBasisU = wasDownscaled || (largeEnoughResolution && largeEnoughSourceSize);

                    if (!shouldCompressWithBasisU)
                    {
                        std::cout << "Keeping original encoded payload for small texture ("
                                  << osPath.filename().generic_string() << ", longestEdge=" << longestEdge
                                  << ", sourceBytes=";
                        if (fileSizeEc)
                            std::cout << "unknown";
                        else
                            std::cout << sourceFileBytes;
                        std::cout << ", thresholds=" << options.basisUCompressMinDimension << "px/"
                                  << options.basisUCompressMinSourceBytes << "B)" << std::endl;
                    }
                }

                if (!shouldCompressWithBasisU && !wasDownscaled)
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
            ci.numLevels       = options.generateMipmaps ? calculateMipLevels(width, height) : 1;
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
                    ktxTexture(ktxGuard.p), 0, 0, 0, reinterpret_cast<const ktx_uint8_t*>(sourcePixels), srcSize) !=
                KTX_SUCCESS)
            {
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
            }

            // Generate mipmaps if needed
            if (ci.numLevels > 1)
            {
                uint32_t             mipW    = static_cast<uint32_t>(width);
                uint32_t             mipH    = static_cast<uint32_t>(height);
                const void*          prevPtr = sourcePixels;
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
                params.uastc            = options.uastc;
                params.noSSE            = options.noSSE;
                params.qualityLevel     = options.qualityLevel;
                params.compressionLevel = options.compressionLevel;
                params.threadCount      = resolveBasisUThreadCount(options.basisUThreadCount);

                // Diagnostic: log vkFormat and mip count
                std::cout << "BasisU: vkFormat=" << ci.vkFormat << " numLevels=" << ci.numLevels
                          << " mode=" << (params.uastc ? "UASTC" : "ETC1S")
                          << " quality=" << params.qualityLevel
                          << " compressionLevel=" << params.compressionLevel
                          << " threads=" << params.threadCount << "\n";

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
        vimport.params =
            normalizedTextureImportParams(std::move(sourceVImport.params), resolvedParams.subtype, options);
        auto sr_import = saveVImport(vimport, sourceSidecarPath.generic_string());
        if (!sr_import)
            return vbase::Result<vbase::UUID, AssetError>::err(sr_import.error());

        auto rr =
            m_Registry.registerAsset(outTexture.uuid, relativeSrcPath, relativeImportedPath, VAssetType::eTexture);
        if (!rr)
            return vbase::Result<vbase::UUID, AssetError>::err(rr.error());

        auto dr = updateImportDatabaseRecord(m_Registry,
                                             outTexture.uuid,
                                             toString(VAssetType::eTexture),
                                             relativeSrcPath,
                                             relativeImportedPath,
                                             importerVersion,
                                             outputSchema,
                                             sourceHash,
                                             dependencyHash,
                                             paramsHash);
        if (!dr)
            return vbase::Result<vbase::UUID, AssetError>::err(dr.error());
        return vbase::Result<vbase::UUID, AssetError>::ok(outTexture.uuid);
    }

    VMeshImporter::VMeshImporter(VAssetRegistry& registry) : m_Registry(registry), m_TextureImporter(registry) {}

    VMeshImporter& VMeshImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    VMeshImporter& VMeshImporter::setProgressCallback(std::function<void(std::string, size_t, size_t)> callback)
    {
        m_ProgressCallback = std::move(callback);
        return *this;
    }

    void VMeshImporter::notifyProgress(std::string item, size_t processed, size_t total) const
    {
        if (m_ProgressCallback)
            m_ProgressCallback(std::move(item), processed, total);
    }

    vbase::Result<vbase::UUID, AssetError>
    VMeshImporter::importModelPrefab(vbase::StringView filePath, VMesh& outMesh, bool forceReimport)
    {
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        const std::string relativeSrcPath = m_Registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string baseKey =
            importedAssetKeyForCookedOutput(m_Registry, VAssetType::eSceneManifest, relativeSrcPath);
        const std::string relativeManifestPath =
            m_Registry.getImportedAssetPath(VAssetType::eSceneManifest, baseKey, true);
        const auto manifestUUID = vbase::uuid_from_string_key(relativeManifestPath);

        const uint64_t sourceHash = hashFile(osPath);
        constexpr uint64_t dependencyHash = 0;

        // Per-asset mesh import options from the model's source .vimport (sparse; resolve fills defaults).
        VImport modelSourceVImport {};
        {
            auto sidecar = osPath;
            sidecar.replace_extension(".vimport");
            if (auto loaded = loadVImport(sidecar.generic_string()))
                modelSourceVImport = std::move(loaded.value());
        }
        const VMeshImporter::ImportOptions opts = resolveMeshImportParams(modelSourceVImport.params, m_Options);
        const uint64_t paramsHash = meshImportParamsHash(opts);
        constexpr auto importerVersion = "model_prefab:1";
        constexpr auto outputSchema = "vmanifest:1+vmesh:5+vskel:1+vanim:1+default_transform:1+node_transform:1";

        auto entry = m_Registry.lookup(manifestUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport &&
            outputFilesAreNewerThanSource(m_Registry, osPath, {relativeManifestPath}))
        {
            if (importDatabaseRecordIsCurrent(m_Registry,
                                              manifestUUID,
                                              toString(VAssetType::eSceneManifest),
                                              relativeSrcPath,
                                              relativeManifestPath,
                                              importerVersion,
                                              outputSchema,
                                              sourceHash,
                                              dependencyHash,
                                              paramsHash,
                                              {relativeManifestPath}))
            {
                (void)updateImportDatabaseRecord(m_Registry,
                                                 manifestUUID,
                                                 toString(VAssetType::eSceneManifest),
                                                 relativeSrcPath,
                                                 relativeManifestPath,
                                                 importerVersion,
                                                 outputSchema,
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);

                // Self-heal the dependency graph even when the cooked model is up-to-date.
                // A skipped (unchanged) model never re-runs collectMeshDependencies, so any
                // stale/incomplete per-node mesh dependency edges (e.g. a material's textures
                // recorded as missing in a past import) would persist and silently drop those
                // assets from a packed VPK (reachability is computed from these edges). Re-derive
                // them from the cooked meshes — cheap: no assimp re-read, no texture recompress.
                const std::string meshSourcePrefix = relativeSrcPath + "#mesh/";
                std::vector<std::pair<vbase::UUID, std::string>> nodeMeshes; // (uuid, importedPath)
                for (const auto& [uuidStr, depEntry] : m_Registry.getRegistry())
                {
                    if (depEntry.type != VAssetType::eMesh)
                        continue;
                    if (depEntry.sourcePath.rfind(meshSourcePrefix, 0) != 0)
                        continue;
                    vbase::UUID meshUuid {};
                    if (vbase::try_parse_uuid(uuidStr.c_str(), meshUuid))
                        nodeMeshes.emplace_back(meshUuid, depEntry.importedPath);
                }
                for (const auto& [meshUuid, importedMeshPath] : nodeMeshes)
                {
                    VMesh      cooked {};
                    const auto cookedPath =
                        (std::filesystem::path(m_Registry.getAssetRootPath()) / importedMeshPath).generic_string();
                    if (loadMesh(cookedPath, cooked))
                        m_Registry.setDependencies(meshUuid, collectMeshDependencies(m_Registry, cooked));
                }

                std::cout << "Model prefab already imported: " << entry.sourcePath << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::ok(manifestUUID);
            }
        }

        m_FilePath = filePath;

        const unsigned int flags = assimpImportFlagsForSource(meshAssimpBaseFlags(opts), osPath);

        Assimp::Importer importer {};
        configureAssimpImporterForSource(importer, osPath);
        const aiScene* scene = importer.ReadFile(filePath.data(), flags);
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode || !scene->HasMeshes())
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        std::optional<ImportedSkeletonData> importedSkeleton;
        std::vector<VAnimation>             importedAnimations;
        std::string                         relativeSkeletonPath;
        std::string                         relativeSkeletonSourcePath;
        const auto inverseBindPoseByName = collectInverseBindPoses(scene);
        if (!inverseBindPoseByName.empty())
        {
            const std::string skeletonKey = shortenImportedAssetKey(baseKey + "_skeleton");
            relativeSkeletonPath = m_Registry.getImportedAssetPath(VAssetType::eSkeleton, skeletonKey, true);
            relativeSkeletonSourcePath = relativeSrcPath + "#skeleton";
            auto skeletonResult = buildImportedSkeleton(scene,
                                                        vbase::uuid_from_string_key(relativeSkeletonPath),
                                                        osPath.stem().generic_string());
            if (!skeletonResult)
            {
                std::cerr << "[vasset] failed skeleton: " << relativeSrcPath << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::err(skeletonResult.error());
            }
            importedSkeleton = std::move(skeletonResult.value());
            importedSkeleton->skeleton.sourceFileName = osPath.filename().generic_string();

            auto animationsResult = buildImportedAnimations(
                scene,
                *importedSkeleton,
                osPath.stem().generic_string(),
                [&](const size_t animationIndex, const std::string_view animationName) {
                    const std::string animationKey = shortenImportedAssetKey(
                        baseKey + "_anim_" + std::to_string(animationIndex) + "_" +
                        sanitizeAssetSegment(std::string(animationName)));
                    return vbase::uuid_from_string_key(
                        m_Registry.getImportedAssetPath(VAssetType::eAnimation, animationKey, true));
                });
            if (!animationsResult)
            {
                std::cerr << "[vasset] failed animation: " << relativeSrcPath << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::err(animationsResult.error());
            }
            importedAnimations = std::move(animationsResult.value());
            for (auto& animation : importedAnimations)
                animation.sourceFileName = osPath.filename().generic_string();
        }

        m_ModelTextureCache.clear();
        m_ModelProgressProcessed = 0;
        std::unordered_set<std::string> referencedTextures;
        for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
        {
            const auto* material = scene->mMaterials[materialIndex];
            if (!material)
                continue;

            for (int tt = static_cast<int>(aiTextureType_NONE) + 1; tt <= static_cast<int>(aiTextureType_UNKNOWN); ++tt)
            {
                const auto     type  = static_cast<aiTextureType>(tt);
                const unsigned count = material->GetTextureCount(type);
                for (unsigned ti = 0; ti < count; ++ti)
                {
                    aiString path;
                    if (material->GetTexture(type, ti, &path) != aiReturn_SUCCESS || path.length == 0 ||
                        path.data[0] == '*')
                    {
                        continue;
                    }

                    const auto texturePath =
                        (std::filesystem::path(m_FilePath).parent_path() / std::filesystem::path(path.C_Str()))
                            .lexically_normal()
                            .generic_string();
                    referencedTextures.insert(texturePath);
                }
            }
        }
        m_ModelProgressTotal =
            referencedTextures.size() + scene->mNumMeshes + 1 + (importedSkeleton ? 1u : 0u) + importedAnimations.size();
        notifyProgress(relativeSrcPath, m_ModelProgressProcessed, m_ModelProgressTotal);

        {
            const std::string cookedMeshPrefix =
                m_Registry.getImportedAssetPath(VAssetType::eMesh, baseKey + "_", true);
            std::vector<vbase::UUID> staleMeshEntries;
            for (const auto& [uuidStr, registryEntry] : m_Registry.getRegistry())
            {
                const bool staleGeneratedMesh =
                    registryEntry.type == VAssetType::eMesh && registryEntry.importedPath.starts_with(cookedMeshPrefix);
                const bool staleGeneratedSkeleton =
                    registryEntry.type == VAssetType::eSkeleton && registryEntry.sourcePath == relativeSkeletonSourcePath;
                const bool staleGeneratedAnimation =
                    registryEntry.type == VAssetType::eAnimation &&
                    registryEntry.sourcePath.starts_with(relativeSrcPath + "#animation/");
                if (!staleGeneratedMesh && !staleGeneratedSkeleton && !staleGeneratedAnimation)
                    continue;

                vbase::UUID uuid {};
                if (vbase::try_parse_uuid(uuidStr.c_str(), uuid))
                    staleMeshEntries.push_back(uuid);
            }

            for (const auto& uuid : staleMeshEntries)
                (void)m_Registry.unregisterAsset(uuid);
        }

        if (importedSkeleton)
        {
            const auto skeletonDiskPath =
                (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeSkeletonPath).generic_string();
            auto sr = saveSkeleton(importedSkeleton->skeleton, skeletonDiskPath);
            if (!sr)
                return vbase::Result<vbase::UUID, AssetError>::err(sr.error());
            auto rr = m_Registry.registerAsset(
                importedSkeleton->skeleton.uuid, relativeSkeletonSourcePath, relativeSkeletonPath, VAssetType::eSkeleton);
            if (!rr)
                return vbase::Result<vbase::UUID, AssetError>::err(rr.error());
            auto dr = updateImportDatabaseRecord(m_Registry,
                                                 importedSkeleton->skeleton.uuid,
                                                 toString(VAssetType::eSkeleton),
                                                 relativeSkeletonSourcePath,
                                                 relativeSkeletonPath,
                                                 "skeleton:1",
                                                 "vskel:1",
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);
            if (!dr)
                return vbase::Result<vbase::UUID, AssetError>::err(dr.error());
            ++m_ModelProgressProcessed;
            notifyProgress(relativeSkeletonSourcePath, m_ModelProgressProcessed, m_ModelProgressTotal);

            for (size_t animationIndex = 0; animationIndex < importedAnimations.size(); ++animationIndex)
            {
                auto& animation = importedAnimations[animationIndex];
                const std::string animationKey = shortenImportedAssetKey(
                    baseKey + "_anim_" + std::to_string(animationIndex) + "_" + sanitizeAssetSegment(animation.name));
                const std::string relativeAnimationPath =
                    m_Registry.getImportedAssetPath(VAssetType::eAnimation, animationKey, true);
                const std::string relativeAnimationSourcePath =
                    relativeSrcPath + "#animation/" + sanitizeAssetSegment(animation.name);
                const auto animationDiskPath =
                    (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeAnimationPath).generic_string();
                auto ar = saveAnimation(animation, animationDiskPath);
                if (!ar)
                    return vbase::Result<vbase::UUID, AssetError>::err(ar.error());
                auto arr =
                    m_Registry.registerAsset(animation.uuid, relativeAnimationSourcePath, relativeAnimationPath, VAssetType::eAnimation);
                if (!arr)
                    return vbase::Result<vbase::UUID, AssetError>::err(arr.error());
                auto adr = updateImportDatabaseRecord(m_Registry,
                                                      animation.uuid,
                                                      toString(VAssetType::eAnimation),
                                                      relativeAnimationSourcePath,
                                                      relativeAnimationPath,
                                                      "animation:1",
                                                      "vanim:1",
                                                      sourceHash,
                                                      dependencyHash,
                                                      paramsHash);
                if (!adr)
                    return vbase::Result<vbase::UUID, AssetError>::err(adr.error());
                ++m_ModelProgressProcessed;
                notifyProgress(relativeAnimationSourcePath, m_ModelProgressProcessed, m_ModelProgressTotal);
            }
        }

        std::ostringstream manifest;
        manifest << "[vmanifest]\n";
        manifest << "version = 1\n";
        manifest << "root = 1\n\n";

        const auto rootUUID = vbase::uuid_from_string_key(relativeManifestPath + "#root");
        manifest << "[node id=1 name=\"" << escapeSceneString(osPath.stem().generic_string())
                 << "\" parent=0 uuid=\"" << vbase::to_string(rootUUID) << "\"]\n";
        manifest << "TransformComponent/position = (0, 0, 0)\n";
        manifest << "TransformComponent/rotation = (0, 0, 0, 1)\n";
        manifest << "TransformComponent/scale = (1, 1, 1)\n";
        if (importedSkeleton && !importedAnimations.empty())
        {
            const auto& firstAnimation = importedAnimations.front();
            const std::string firstAnimationSourcePath =
                relativeSrcPath + "#animation/" + sanitizeAssetSegment(firstAnimation.name);
            manifest << "AnimatorComponent/skeleton = \"res://" << escapeSceneString(relativeSkeletonSourcePath) << "\"\n";
            manifest << "AnimatorComponent/animation = \"res://" << escapeSceneString(firstAnimationSourcePath) << "\"\n";
            manifest << "AnimatorComponent/playOnStart = true\n";
            manifest << "AnimatorComponent/playing = true\n";
            manifest << "AnimatorComponent/loop = true\n";
            manifest << "AnimatorComponent/speed = 1\n";
            manifest << "AnimatorComponent/time = 0\n";
        }
        manifest << "\n";

        int nextNodeId = 2;
        uint32_t nextMeshOrdinal = 0;
        bool assignedOutMesh = false;

        const auto isIdentityTransform = [](const aiMatrix4x4& m) {
            constexpr float eps = 1e-5f;
            const aiMatrix4x4 identity;
            return std::abs(m.a1 - identity.a1) < eps && std::abs(m.a2 - identity.a2) < eps &&
                   std::abs(m.a3 - identity.a3) < eps && std::abs(m.a4 - identity.a4) < eps &&
                   std::abs(m.b1 - identity.b1) < eps && std::abs(m.b2 - identity.b2) < eps &&
                   std::abs(m.b3 - identity.b3) < eps && std::abs(m.b4 - identity.b4) < eps &&
                   std::abs(m.c1 - identity.c1) < eps && std::abs(m.c2 - identity.c2) < eps &&
                   std::abs(m.c3 - identity.c3) < eps && std::abs(m.c4 - identity.c4) < eps &&
                   std::abs(m.d1 - identity.d1) < eps && std::abs(m.d2 - identity.d2) < eps &&
                   std::abs(m.d3 - identity.d3) < eps && std::abs(m.d4 - identity.d4) < eps;
        };

        struct DecomposedNodeTransform
        {
            glm::vec3 position {0.0f};
            glm::quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 scale {1.0f};
        };

        const auto decomposeTransform = [](const aiMatrix4x4& transform) {
            aiVector3D scaling(1.0f, 1.0f, 1.0f);
            aiQuaternion rotation;
            aiVector3D position(0.0f, 0.0f, 0.0f);
            transform.Decompose(scaling, rotation, position);

            return DecomposedNodeTransform {
                .position = glm::vec3 {position.x, position.y, position.z},
                .rotation = glm::quat {rotation.w, rotation.x, rotation.y, rotation.z},
                .scale    = glm::vec3 {scaling.x, scaling.y, scaling.z},
            };
        };

        const auto decomposeDefaultTransform = [&](const aiMatrix4x4& transform, VMesh& mesh) {
            const auto decomposed = decomposeTransform(transform);
            mesh.hasDefaultTransform = true;
            mesh.defaultPosition = decomposed.position;
            mesh.defaultRotation = decomposed.rotation;
            mesh.defaultScale    = decomposed.scale;
            return decomposed;
        };

        const auto recenterMeshAroundBoundsCenter = [](VMesh& mesh) {
            if (mesh.positions.empty())
                return glm::vec3 {0.0f};

            glm::vec3 minP(std::numeric_limits<float>::infinity());
            glm::vec3 maxP(-std::numeric_limits<float>::infinity());
            for (const auto& position : mesh.positions)
            {
                minP = glm::min(minP, position);
                maxP = glm::max(maxP, position);
            }

            const glm::vec3 center = (minP + maxP) * 0.5f;
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
                return glm::vec3 {0.0f};

            for (auto& position : mesh.positions)
                position -= center;

            return center;
        };

        const auto transformWithLocalPivot = [](aiMatrix4x4 transform, const glm::vec3& pivot) {
            aiMatrix4x4 pivotTransform;
            aiMatrix4x4::Translation(aiVector3D(pivot.x, pivot.y, pivot.z), pivotTransform);
            return transform * pivotTransform;
        };

        struct ImportedNodeMeshPaths
        {
            std::string             sourcePath;
            std::string             importedPath;
            DecomposedNodeTransform transform;
        };

        const auto importNodeMesh = [&](const aiMesh* aiMesh,
                                        const std::string& meshPathKey,
                                        const std::string& nodeName,
                                        const aiMatrix4x4& defaultTransform) -> ImportedNodeMeshPaths {
            VMesh nodeMesh {};
            const std::string meshKey = shortenImportedAssetKey(baseKey + "_" + sanitizeAssetSegment(meshPathKey));
            const std::string relativeMeshPath = m_Registry.getImportedAssetPath(VAssetType::eMesh, meshKey, true);
            const std::string relativeMeshSourcePath =
                relativeSrcPath + "#mesh/" + sanitizeAssetSegment(meshPathKey);
            nodeMesh.uuid = vbase::uuid_from_string_key(relativeMeshPath);
            nodeMesh.name = nodeName;
            nodeMesh.sourceFileName = osPath.filename().generic_string();

            processMesh(aiMesh, scene, nodeMesh);
            if (importedSkeleton && aiMesh->HasBones())
            {
                nodeMesh.skeleton     = importedSkeleton->skeleton.uuid;
                nodeMesh.skeletonPath = relativeSkeletonSourcePath;
                if (!applySkinningData(aiMesh, *importedSkeleton, nodeMesh, inverseBindPoseByName))
                    throw AssetError::eInvalidFormat;
            }
            const glm::vec3 localPivot = nodeMesh.hasSkin ? glm::vec3 {0.0f} : recenterMeshAroundBoundsCenter(nodeMesh);
            const auto      nodeTransform =
                decomposeDefaultTransform(transformWithLocalPivot(defaultTransform, localPivot), nodeMesh);

            optimizeMeshIndices(nodeMesh, opts);
            if (opts.generateMeshlets)
                generateMeshlets(nodeMesh);
            finalizeMeshVertexFlags(nodeMesh);
            updateMeshLocalBounds(nodeMesh);

            const auto meshDiskPath =
                (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeMeshPath).generic_string();
            auto sr = saveMesh(nodeMesh, meshDiskPath, 3);
            if (!sr)
                throw sr.error();

            auto rr = m_Registry.registerAsset(nodeMesh.uuid, relativeMeshSourcePath, relativeMeshPath, VAssetType::eMesh);
            if (!rr)
                throw rr.error();
            m_Registry.setDependencies(nodeMesh.uuid, collectMeshDependencies(m_Registry, nodeMesh));

            ++m_ModelProgressProcessed;
            notifyProgress(relativeMeshSourcePath, m_ModelProgressProcessed, m_ModelProgressTotal);

            if (!assignedOutMesh)
            {
                outMesh = nodeMesh;
                assignedOutMesh = true;
            }

            return ImportedNodeMeshPaths {
                .sourcePath   = relativeMeshSourcePath,
                .importedPath = relativeMeshPath,
                .transform    = nodeTransform,
            };
        };

        std::function<void(const aiNode*, std::string, aiMatrix4x4)> emitNodeMeshes;
        emitNodeMeshes = [&](const aiNode* node, const std::string nodePath, const aiMatrix4x4 parentTransform) {
            if (!node)
                return;

            const aiMatrix4x4 nodeWorldTransform = parentTransform * node->mTransformation;

            for (unsigned int i = 0; i < node->mNumMeshes; ++i)
            {
                const uint32_t meshOrdinal = nextMeshOrdinal++;
                const auto* aiMesh = scene->mMeshes[node->mMeshes[i]];
                const int meshNodeId = nextNodeId++;
                const std::string meshName = displayNameForMesh(aiMesh, scene, osPath, meshOrdinal);
                const std::string meshPathKey = nodePath + "_mesh_" + std::to_string(i) + "_" + sanitizeAssetSegment(meshName);
                const auto meshNodeUUID = vbase::uuid_from_string_key(relativeManifestPath + "#mesh_node/" + meshPathKey);
                const ImportedNodeMeshPaths meshPaths = importNodeMesh(aiMesh, meshPathKey, meshName, nodeWorldTransform);

                manifest << "[node id=" << meshNodeId << " name=\"" << escapeSceneString(meshName)
                         << "\" parent=1 uuid=\"" << vbase::to_string(meshNodeUUID) << "\"]\n";
                const auto& t = meshPaths.transform;
                manifest << "TransformComponent/position = (" << t.position.x << ", " << t.position.y << ", "
                         << t.position.z << ")\n";
                manifest << "TransformComponent/rotation = (" << t.rotation.x << ", " << t.rotation.y << ", "
                         << t.rotation.z << ", " << t.rotation.w << ")\n";
                manifest << "TransformComponent/scale = (" << t.scale.x << ", " << t.scale.y << ", " << t.scale.z
                         << ")\n";
                manifest << "MeshComponent/mesh = \"res://" << escapeSceneString(meshPaths.sourcePath) << "\"\n";
                manifest << "MeshComponent/builtinGeometry = 4294967295\n";
                manifest << "MeshComponent/materialColor = (1, 1, 1, 1)\n\n";
            }

            for (unsigned int i = 0; i < node->mNumChildren; ++i)
            {
                const std::string childPath = nodePath + "_" + std::to_string(i) + "_" +
                                              sanitizeAssetSegment(node->mChildren[i]->mName.C_Str());
                emitNodeMeshes(node->mChildren[i], childPath, nodeWorldTransform);
            }
        };

        try
        {
            const std::string rootPath = "0_" + sanitizeAssetSegment(scene->mRootNode->mName.C_Str());
            if (scene->mRootNode->mNumMeshes == 0 && isIdentityTransform(scene->mRootNode->mTransformation))
            {
                for (unsigned int i = 0; i < scene->mRootNode->mNumChildren; ++i)
                {
                    const std::string childPath = rootPath + "_" + std::to_string(i) + "_" +
                                                  sanitizeAssetSegment(scene->mRootNode->mChildren[i]->mName.C_Str());
                    emitNodeMeshes(scene->mRootNode->mChildren[i], childPath, aiMatrix4x4 {});
                }
            }
            else
            {
                emitNodeMeshes(scene->mRootNode, rootPath, aiMatrix4x4 {});
            }
        }
        catch (AssetError error)
        {
            return vbase::Result<vbase::UUID, AssetError>::err(error);
        }

        const auto manifestDiskPath =
            (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeManifestPath).generic_string();
        std::filesystem::create_directories(std::filesystem::path(manifestDiskPath).parent_path());
        {
            std::ofstream file(manifestDiskPath, std::ios::binary);
            if (!file)
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eIOError);
            const auto text = manifest.str();
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
        }

        VImport vimport {};
        vimport.importer = toString(VAssetType::eSceneManifest);
        vimport.uid = manifestUUID;
        vimport.source = relativeSrcPath;
        vimport.output = relativeManifestPath;
        // Persist only non-default mesh import options on the model's source sidecar (read/written
        // by the editor's mesh import inspector).
        vimport.params = normalizedMeshImportParams(std::move(modelSourceVImport.params), opts);
        auto importSidecarPath = osPath;
        auto srImport = saveVImport(vimport, importSidecarPath.replace_extension(".vimport").generic_string());
        if (!srImport)
            return vbase::Result<vbase::UUID, AssetError>::err(srImport.error());

        auto rr = m_Registry.registerAsset(manifestUUID, relativeSrcPath, relativeManifestPath, VAssetType::eSceneManifest);
        if (!rr)
            return vbase::Result<vbase::UUID, AssetError>::err(rr.error());

        ++m_ModelProgressProcessed;
        notifyProgress(relativeManifestPath, m_ModelProgressProcessed, m_ModelProgressTotal);

        auto dr = updateImportDatabaseRecord(m_Registry,
                                             manifestUUID,
                                             toString(VAssetType::eSceneManifest),
                                             relativeSrcPath,
                                             relativeManifestPath,
                                             importerVersion,
                                             outputSchema,
                                             sourceHash,
                                             dependencyHash,
                                             paramsHash);
        if (!dr)
            return vbase::Result<vbase::UUID, AssetError>::err(dr.error());

        return vbase::Result<vbase::UUID, AssetError>::ok(manifestUUID);
    }

    vbase::Result<vbase::UUID, AssetError>
    VMeshImporter::importMesh(vbase::StringView filePath, VMesh& outMesh, bool forceReimport)
    {
        // Check path
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        if (isValidModel(osPath.extension().generic_string()))
            return importModelPrefab(filePath, outMesh, forceReimport);

        // Check registry
        const std::string relativeSrcPath = m_Registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string relativeImportedPath =
            m_Registry.getImportedAssetPath(VAssetType::eMesh,
                                            importedAssetKeyForCookedOutput(m_Registry, VAssetType::eMesh, relativeSrcPath),
                                            true);

        auto lookupUUID = vbase::uuid_from_string_key(relativeImportedPath);
        const uint64_t sourceHash     = hashFile(osPath);
        constexpr uint64_t dependencyHash = 0;

        // Per-asset mesh import options from the source .vimport (sparse; resolve fills defaults).
        VImport meshSourceVImport {};
        {
            auto sidecar = osPath;
            sidecar.replace_extension(".vimport");
            if (auto loaded = loadVImport(sidecar.generic_string()))
                meshSourceVImport = std::move(loaded.value());
        }
        const VMeshImporter::ImportOptions opts = resolveMeshImportParams(meshSourceVImport.params, m_Options);
        const uint64_t paramsHash     = meshImportParamsHash(opts);
        constexpr auto importerVersion = "mesh:1";
        constexpr auto outputSchema = "vmesh:5";

        auto entry      = m_Registry.lookup(lookupUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport &&
            outputFilesAreNewerThanSource(m_Registry, osPath, {relativeImportedPath}))
        {
            if (importDatabaseRecordIsCurrent(m_Registry,
                                              lookupUUID,
                                              toString(VAssetType::eMesh),
                                              relativeSrcPath,
                                              relativeImportedPath,
                                              importerVersion,
                                              outputSchema,
                                              sourceHash,
                                              dependencyHash,
                                              paramsHash,
                                              {relativeImportedPath}))
            {
                (void)updateImportDatabaseRecord(m_Registry,
                                                 lookupUUID,
                                                 toString(VAssetType::eMesh),
                                                 relativeSrcPath,
                                                 relativeImportedPath,
                                                 importerVersion,
                                                 outputSchema,
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);
                // Load existing mesh
                std::cout << "Mesh already imported: " << entry.sourcePath << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
            }
        }

        // Set UUID
        outMesh.uuid = vbase::uuid_from_string_key(relativeImportedPath);

        m_FilePath = filePath;

        // Set Assimp process flags
        // Ignore scene graph, as we flatten everything into a single VMesh
        unsigned int flags = assimpImportFlagsForSource(
            meshAssimpBaseFlags(opts) | (opts.preTransformVertices ? aiProcess_PreTransformVertices : 0u), osPath);

        Assimp::Importer importer {};
        configureAssimpImporterForSource(importer, osPath);
        const aiScene*   scene = importer.ReadFile(filePath.data(), flags);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode || !scene->HasMeshes())
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        outMesh.name           = scene->mRootNode->mName.C_Str();
        outMesh.sourceFileName = osPath.stem().generic_string();

        // Process the scene
        processNode(scene->mRootNode, scene, outMesh);

        optimizeMeshIndices(outMesh, opts);

        // Generate meshlets if enabled
        if (opts.generateMeshlets)
        {
            generateMeshlets(outMesh);
        }

        finalizeMeshVertexFlags(outMesh);
        updateMeshLocalBounds(outMesh);
        // Note: Joint indices and weights would require additional processing, e.g., from bones

        const std::string importedPath =
            (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeImportedPath).generic_string();

        auto sr_mesh = saveMesh(outMesh, importedPath, 3);
        if (!sr_mesh)
        {
            std::cerr << "Failed to save mesh." << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::err(sr_mesh.error());
        }

        // Save VImport (persist only non-default mesh options).
        VImport vimport {};
        vimport.importer = toString(VAssetType::eMesh);
        vimport.uid      = outMesh.uuid;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;
        vimport.params   = normalizedMeshImportParams(std::move(meshSourceVImport.params), opts);
        auto sr_import = saveVImport(vimport, osPath.replace_extension(".vimport").generic_string());
        if (!sr_import)
            return vbase::Result<vbase::UUID, AssetError>::err(sr_import.error());

        auto rr = m_Registry.registerAsset(outMesh.uuid, relativeSrcPath, relativeImportedPath, VAssetType::eMesh);
        if (!rr)
            return vbase::Result<vbase::UUID, AssetError>::err(rr.error());
        m_Registry.setDependencies(outMesh.uuid, collectMeshDependencies(m_Registry, outMesh));

        auto dr = updateImportDatabaseRecord(m_Registry,
                                             outMesh.uuid,
                                             toString(VAssetType::eMesh),
                                             relativeSrcPath,
                                             relativeImportedPath,
                                             importerVersion,
                                             outputSchema,
                                             sourceHash,
                                             dependencyHash,
                                             paramsHash);
        if (!dr)
            return vbase::Result<vbase::UUID, AssetError>::err(dr.error());
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
        subMesh.materialIndex = static_cast<uint32_t>(outMesh.materials.size());
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
                const glm::vec3 normal = glm::normalize(outMesh.normals.back());
                tangent                = glm::normalize(tangent - normal * glm::dot(normal, tangent));
                bitangent              = glm::normalize(bitangent);
                const float handedness = glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                VTangent    tangentWithHandness = VTangent(tangent, handedness);
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
            processMaterial(aiMat, scene, vMat);
            if (vMat.name.empty())
                vMat.name = materialName;
            outMesh.materials.push_back(vMat);
        }
        else
        {
            VMaterial vMat {};
            vMat.name = std::format("{}_default", outMesh.sourceFileName);
            outMesh.materials.push_back(vMat);
        }

        outMesh.subMeshes.push_back(subMesh);
        outMesh.vertexCount += mesh->mNumVertices;
    }

    void VMeshImporter::processMaterial(const aiMaterial* material, const aiScene* scene, VMaterial& outMaterial) const
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
                    tb.texture = loadTexture(material, scene, type, ti);
                }

                outMaterial.textures.push_back(tb);
            }
        }

        // ------------------------------------------------------------
        // Model + feature flags (hints)
        // ------------------------------------------------------------
        const bool isUnlit         = hasKeySubstring("unlit") || hasKeySubstring("Unlit");
        const bool hasSpecGlossKey = hasKeySubstring("SpecularGlossiness") || hasKeySubstring("specularGloss") ||
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

        auto hasProp = [&](const char* key, unsigned semantic, unsigned index) {
            return props.find(MatPropKey {key, semantic, index}) != props.end();
        };
        auto texturePathContains = [&](const aiTextureType type, const std::string_view needle) {
            for (unsigned i = 0, count = material->GetTextureCount(type); i < count; ++i)
            {
                aiString path;
                if (material->GetTexture(type, i, &path) != aiReturn_SUCCESS)
                    continue;
                std::string lower = std::filesystem::path(path.C_Str()).filename().generic_string();
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (lower.find(needle) != std::string::npos)
                    return true;
            }
            return false;
        };
        auto findTexturePathContaining =
            [&](const aiTextureType type, const std::string_view needle, std::filesystem::path& outPath) {
                for (unsigned i = 0, count = material->GetTextureCount(type); i < count; ++i)
                {
                    aiString path;
                    if (material->GetTexture(type, i, &path) != aiReturn_SUCCESS)
                        continue;

                    std::filesystem::path relativePath(path.C_Str());
                    std::string           lower = relativePath.filename().generic_string();
                    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (lower.find(needle) == std::string::npos)
                        continue;

                    outPath = (std::filesystem::path(m_FilePath).parent_path() / relativePath).lexically_normal();
                    return true;
                }
                return false;
            };
        const bool hasSpecularTexture = material->GetTextureCount(aiTextureType_SPECULAR) > 0;
        const bool hasOrmSpecularTexture =
            hasSpecularTexture &&
            (texturePathContains(aiTextureType_SPECULAR, "specular") ||
             texturePathContains(aiTextureType_SPECULAR, "orm") ||
             texturePathContains(aiTextureType_SPECULAR, "occlusionroughnessmetallic"));
        std::filesystem::path baseColorTexturePath;
        const bool            hasPackedBaseColorOpacity =
            hasOrmSpecularTexture &&
            findTexturePathContaining(aiTextureType_DIFFUSE, "basecolor", baseColorTexturePath) &&
            inspectTextureAlpha(baseColorTexturePath) == TextureAlphaContent::eHasNonOpaque;
        // A dedicated glossiness map (shininess slot) alongside a specular map is the classic
        // specular-glossiness workflow (e.g. Mixamo FBX) even without a glTF SG key. SunTemple's
        // packed ORM uses the specular slot but has no separate glossiness map, so it stays MR.
        const bool hasGlossinessTexture = material->GetTextureCount(aiTextureType_SHININESS) > 0;
        const bool hasSpecGloss         = hasSpecGlossKey || (hasGlossinessTexture && hasSpecularTexture);

        const bool hasMetallicRoughnessHints =
            !hasSpecGloss &&
            (hasProp(AI_MATKEY_BASE_COLOR) || hasProp(AI_MATKEY_METALLIC_FACTOR) ||
             hasProp(AI_MATKEY_ROUGHNESS_FACTOR) ||
             material->GetTextureCount(aiTextureType_GLTF_METALLIC_ROUGHNESS) > 0 ||
             material->GetTextureCount(aiTextureType_METALNESS) > 0 ||
             material->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS) > 0 || hasOrmSpecularTexture);

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

            outMaterial.core.unlit.colorTexture = loadTexture(material, scene, aiTextureType_DIFFUSE);
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

            outMaterial.core.pbrSG.diffuseTexture            = loadTexture(material, scene, aiTextureType_DIFFUSE);
            outMaterial.core.pbrSG.specularGlossinessTexture = loadTexture(material, scene, aiTextureType_SPECULAR);
            // Some authoring tools (e.g. Mixamo FBX) export specular and glossiness as two
            // separate maps; the glossiness map lands in the shininess slot. When present it
            // drives roughness per-texel, so keep the glossiness factor as a unit multiplier.
            outMaterial.core.pbrSG.glossinessTexture = loadTexture(material, scene, aiTextureType_SHININESS);
            if (outMaterial.core.pbrSG.glossinessTexture.uuid != vbase::UUID {})
                outMaterial.core.pbrSG.glossinessFactor = 1.0f;
            outMaterial.core.pbrSG.normalTexture = loadTexture(material, scene, aiTextureType_NORMALS, 0, false);
        }
        else if (!hasMetallicRoughnessHints)
        {
            outMaterial.model      = VMaterialModel::ePhong;
            outMaterial.core.phong = {};

            aiColor3D kd(1, 1, 1), ks(0, 0, 0), ke(0, 0, 0);
            tryGet(props, AI_MATKEY_COLOR_DIFFUSE, kd);
            tryGet(props, AI_MATKEY_COLOR_SPECULAR, ks);
            tryGet(props, AI_MATKEY_COLOR_EMISSIVE, ke);

            float Ns = 32.0f, d = 1.0f, Ni = 1.5f;
            tryGet(props, AI_MATKEY_SHININESS, Ns);
            tryGet(props, AI_MATKEY_OPACITY, d);
            tryGet(props, AI_MATKEY_REFRACTI, Ni);

            outMaterial.core.phong.diffuse   = glm::vec4(kd.r, kd.g, kd.b, d);
            outMaterial.core.phong.specular  = glm::vec3(ks.r, ks.g, ks.b);
            outMaterial.core.phong.shininess = std::max(Ns, 1.0f);
            outMaterial.core.phong.opacity   = d;
            outMaterial.core.phong.ior       = Ni;
            outMaterial.core.phong.emissive  = glm::vec3(ke.r, ke.g, ke.b);

            outMaterial.core.phong.diffuseTexture  = loadTexture(material, scene, aiTextureType_DIFFUSE);
            outMaterial.core.phong.specularTexture = loadTexture(material, scene, aiTextureType_SPECULAR);
            outMaterial.core.phong.normalTexture   = loadTexture(material, scene, aiTextureType_NORMALS, 0, false);
            outMaterial.core.phong.opacityTexture  = loadTexture(material, scene, aiTextureType_OPACITY);
            outMaterial.core.phong.emissiveTexture = loadTexture(material, scene, aiTextureType_EMISSIVE);
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
            if (hasPackedBaseColorOpacity)
            {
                outMaterial.core.pbrMR.alphaMode   = VMaterialAlphaMode::eMask;
                outMaterial.core.pbrMR.alphaCutoff = 0.5f;
            }

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
            else if (std::filesystem::path(m_FilePath).extension() == ".obj")
                outMaterial.core.pbrMR.doubleSided = true;

            // Core textures
            outMaterial.core.pbrMR.baseColorTexture        = loadTexture(material, scene, aiTextureType_DIFFUSE);
            outMaterial.core.pbrMR.alphaTexture            = loadTexture(material, scene, aiTextureType_OPACITY);
            outMaterial.core.pbrMR.metallicTexture         = loadTexture(material, scene, aiTextureType_METALNESS);
            outMaterial.core.pbrMR.roughnessTexture        = loadTexture(material, scene, aiTextureType_DIFFUSE_ROUGHNESS);
            outMaterial.core.pbrMR.specularTexture         = loadTexture(material, scene, aiTextureType_SPECULAR);
            outMaterial.core.pbrMR.normalTexture           = loadTexture(material, scene, aiTextureType_NORMALS, 0, hasOrmSpecularTexture);
            outMaterial.core.pbrMR.ambientOcclusionTexture = loadTexture(material, scene, aiTextureType_LIGHTMAP);
            outMaterial.core.pbrMR.emissiveTexture         = loadTexture(material, scene, aiTextureType_EMISSIVE);
            outMaterial.core.pbrMR.metallicRoughnessTexture =
                loadTexture(material, scene, aiTextureType_GLTF_METALLIC_ROUGHNESS);
            if (hasOrmSpecularTexture && outMaterial.core.pbrMR.specularTexture.uuid != vbase::UUID {})
            {
                // ORCA/SunTemple stores absolute ORM values in the legacy Specular slot.
                // Do not multiply them by classic specular/shininess factors inferred by Assimp.
                outMaterial.core.pbrMR.metallicFactor  = 1.0f;
                outMaterial.core.pbrMR.roughnessFactor = 1.0f;
            }

            // Assimp's glTF importer does not always surface emissiveFactor via
            // AI_MATKEY_COLOR_EMISSIVE; it can stay black even when the material has an
            // emissive texture. Per glTF, emissive = emissiveFactor * emissiveTexture, so a
            // black factor would zero out the texture. Default the factor to white when an
            // emissive texture is present but the factor came through as black.
            if (outMaterial.core.pbrMR.emissiveTexture.uuid != vbase::UUID {} &&
                outMaterial.core.pbrMR.emissiveColorIntensity.r == 0.0f &&
                outMaterial.core.pbrMR.emissiveColorIntensity.g == 0.0f &&
                outMaterial.core.pbrMR.emissiveColorIntensity.b == 0.0f)
            {
                outMaterial.core.pbrMR.emissiveColorIntensity.r = 1.0f;
                outMaterial.core.pbrMR.emissiveColorIntensity.g = 1.0f;
                outMaterial.core.pbrMR.emissiveColorIntensity.b = 1.0f;
            }
        }

        if (outMaterial.name.empty())
            outMaterial.name = "Material";

        std::cout << "Imported material: " << outMaterial.name << " (model=" << static_cast<int>(outMaterial.model)
                  << ", props=" << outMaterial.properties.size() << ", textures=" << outMaterial.textures.size() << ")"
                  << std::endl;
    }

    VTextureRef VMeshImporter::loadTexture(const aiMaterial* material, const aiScene* scene, aiTextureType type) const
    {
        return loadTexture(material, scene, type, 0);
    }

    VTextureRef VMeshImporter::loadTexture(const aiMaterial* material,
                                           const aiScene*    scene,
                                           aiTextureType     type,
                                           unsigned          index) const
    {
        return loadTexture(material, scene, type, index, false);
    }

    VTextureRef VMeshImporter::loadTexture(const aiMaterial* material,
                                           const aiScene*    scene,
                                           aiTextureType     type,
                                           unsigned          index,
                                           bool              directXNormalMap) const
    {
        if (!material)
            return {};

        if (material->GetTextureCount(type) > index)
        {
            aiString str;
            if (material->GetTexture(type, index, &str) == aiReturn_SUCCESS)
            {
                VTexture              texture {};
                const std::string     textureKeyRaw = str.C_Str();
                std::filesystem::path relativePath(textureKeyRaw);

                std::filesystem::path texPath = (std::filesystem::path(m_FilePath).parent_path() / relativePath).lexically_normal();
                const bool            normalMap = type == aiTextureType_NORMALS;
                const auto            embeddedTexKey = std::string("embedded:") + textureKeyRaw +
                                            (normalMap ? (directXNormalMap ? "|normal:dx" : "|normal:gl") : "");
                const auto            texKey    = texPath.generic_string() +
                                       (normalMap ? (directXNormalMap ? "|normal:dx" : "|normal:gl") : "");
                if (auto cachedIt = m_ModelTextureCache.find(embeddedTexKey); cachedIt != m_ModelTextureCache.end())
                    return VTextureRef {cachedIt->second};
                if (auto cachedIt = m_ModelTextureCache.find(texKey); cachedIt != m_ModelTextureCache.end())
                    return VTextureRef {cachedIt->second};

                if (scene)
                {
                    const aiTexture* embeddedTexture = scene->GetEmbeddedTexture(textureKeyRaw.c_str());
                    if (!embeddedTexture)
                        embeddedTexture = scene->GetEmbeddedTexture(relativePath.filename().generic_string().c_str());

                    if (embeddedTexture)
                    {
                        if (embeddedTexture->mHeight != 0u)
                        {
                            std::cerr << "Unsupported raw embedded texture: " << textureKeyRaw << std::endl;
                            return {};
                        }

                        const auto* bytes = reinterpret_cast<const uint8_t*>(embeddedTexture->pcData);
                        const auto  byteCount = static_cast<size_t>(embeddedTexture->mWidth);
                        int width = 0;
                        int height = 0;
                        int channels = 0;
                        if (byteCount == 0u || !stbi_info_from_memory(bytes, static_cast<int>(byteCount), &width, &height, &channels))
                        {
                            std::cerr << "Failed to decode embedded texture info: " << textureKeyRaw << std::endl;
                            return {};
                        }

                        std::string ext = relativePath.extension().generic_string();
                        if (ext.empty())
                        {
                            std::string hint = embeddedTexture->achFormatHint;
                            hint = toLowerAscii(hint);
                            if (hint == "jpg")
                                ext = ".jpg";
                            else if (hint == "jpeg")
                                ext = ".jpeg";
                            else if (hint == "png")
                                ext = ".png";
                            else if (hint == "bmp")
                                ext = ".bmp";
                            else if (hint == "tga")
                                ext = ".tga";
                        }

                        const auto fileFormat = textureFileFormatFromExtension(ext);
                        if (fileFormat == VTextureFileFormat::eUnknown)
                        {
                            std::cerr << "Unsupported embedded texture format: " << textureKeyRaw << std::endl;
                            return {};
                        }

                        const std::filesystem::path modelPath(m_FilePath);
                        const std::string relativeModelSource = m_Registry.getSourceAssetPath(modelPath.generic_string(), true);
                        const std::string embeddedSourcePath = relativeModelSource + "#texture/" +
                                                               sanitizeAssetSegment(textureKeyRaw) +
                                                               (normalMap ? (directXNormalMap ? "_normal_dx" : "_normal_gl") : "");
                        const std::string relativeImportedPath =
                            m_Registry.getImportedAssetPath(
                                VAssetType::eTexture,
                                importedAssetKeyForCookedOutput(m_Registry, VAssetType::eTexture, embeddedSourcePath),
                                true);
                        const auto uuid = vbase::uuid_from_string_key(relativeImportedPath);

                        texture.uuid       = uuid;
                        texture.width      = static_cast<uint32_t>(width);
                        texture.height     = static_cast<uint32_t>(height);
                        texture.depth      = 1u;
                        texture.mipLevels  = 1u;
                        texture.arrayLayers = 1u;
                        texture.isCubemap  = false;
                        texture.type       = VTextureDimension::e2D;
                        texture.format     = VTextureFormat::eRGBA8;
                        texture.fileFormat = fileFormat;
                        texture.compressedBasisU = false;
                        texture.data.assign(bytes, bytes + byteCount);

                        const auto importedPath =
                            (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeImportedPath).generic_string();
                        auto sr = saveTexture(texture, importedPath);
                        if (!sr)
                        {
                            std::cerr << "Failed to save embedded texture: " << textureKeyRaw << std::endl;
                            return {};
                        }

                        auto rr = m_Registry.registerAsset(uuid, embeddedSourcePath, relativeImportedPath, VAssetType::eTexture);
                        if (!rr)
                        {
                            std::cerr << "Failed to register embedded texture: " << textureKeyRaw << std::endl;
                            return {};
                        }

                        const uint64_t sourceHash = hashString(textureKeyRaw);
                        const uint64_t dependencyHash = hashU64(byteCount, 0);
                        const uint64_t paramsHash = textureImportParamsHash(VTextureImporter::ImportOptions {});
                        (void)updateImportDatabaseRecord(m_Registry,
                                                         uuid,
                                                         toString(VAssetType::eTexture),
                                                         embeddedSourcePath,
                                                         relativeImportedPath,
                                                         "embedded_texture:1",
                                                         "vtexture:1",
                                                         sourceHash,
                                                         dependencyHash,
                                                         paramsHash);

                        m_ModelTextureCache.emplace(embeddedTexKey, uuid);
                        ++m_ModelProgressProcessed;
                        notifyProgress(embeddedSourcePath, m_ModelProgressProcessed, m_ModelProgressTotal);
                        std::cout << "  Loaded embedded texture: " << textureKeyRaw
                                  << ", uuid: " << vbase::to_string(uuid) << std::endl;
                        return VTextureRef {uuid};
                    }
                }

                vbase::Result<vbase::UUID, AssetError> tr =
                    vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);
                if (normalMap)
                {
                    VTextureImporter                normalImporter(m_Registry);
                    VTextureImporter::ImportOptions options {};
                    options.generateMipmaps       = false;
                    options.targetTextureFileFormat = VTextureFileFormat::eKTX2;
                    options.bakeNormalMap        = true;
                    options.directXNormalMap     = directXNormalMap;
                    tr = normalImporter.setOptions(options).importTexture(texPath.generic_string(), texture);
                }
                else
                {
                    tr = m_TextureImporter.importTexture(texPath.generic_string(), texture);
                }
                if (!tr)
                {
                    std::cerr << "Failed to import texture: " << texPath << std::endl;
                    return {};
                }

                const auto uuid = tr.value();
                m_ModelTextureCache.emplace(texKey, uuid);
                ++m_ModelProgressProcessed;
                notifyProgress(texKey, m_ModelProgressProcessed, m_ModelProgressTotal);

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
            if (subCount == 0 || sub.vertexCount == 0)
                continue;

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
            if (!sub.meshletGroup.meshlets.empty())
            {
                const auto& last = sub.meshletGroup.meshlets.back();
                sub.meshletGroup.meshletVertices.resize(last.vertexOffset + last.vertexCount);
                sub.meshletGroup.meshletTriangles.resize(last.triangleOffset + ((last.triangleCount * 3 + 3) & ~3));
            }
            else
            {
                sub.meshletGroup.meshletVertices.clear();
                sub.meshletGroup.meshletTriangles.clear();
            }
        }
    }

    void VMeshImporter::optimizeMeshIndices(VMesh& outMesh, const ImportOptions& options)
    {
        if (outMesh.indices.empty() || outMesh.positions.empty())
            return;

        for (const auto& sub : outMesh.subMeshes)
        {
            if (sub.indexCount == 0 || sub.vertexCount == 0)
                continue;
            if (sub.indexOffset + sub.indexCount > outMesh.indices.size() ||
                sub.vertexOffset + sub.vertexCount > outMesh.positions.size())
                continue;

            auto* indices = outMesh.indices.data() + sub.indexOffset;
            if (options.optimizeVertexCache)
            {
                std::vector<uint32_t> optimized(sub.indexCount);
                meshopt_optimizeVertexCache(optimized.data(), indices, sub.indexCount, sub.vertexCount);
                std::copy(optimized.begin(), optimized.end(), indices);
            }

            if (options.optimizeOverdraw)
            {
                std::vector<uint32_t> optimized(sub.indexCount);
                meshopt_optimizeOverdraw(optimized.data(),
                                         indices,
                                         sub.indexCount,
                                         reinterpret_cast<const float*>(outMesh.positions.data() + sub.vertexOffset),
                                         sub.vertexCount,
                                         sizeof(glm::vec3),
                                         1.05f);
                std::copy(optimized.begin(), optimized.end(), indices);
            }

            if (options.optimizeVertexFetch)
            {
                std::vector<uint32_t> remap(sub.vertexCount);
                const size_t usedVertexCount =
                    meshopt_optimizeVertexFetchRemap(remap.data(), indices, sub.indexCount, sub.vertexCount);
                if (usedVertexCount == 0)
                    continue;

                std::vector<uint32_t> remappedIndices(sub.indexCount);
                meshopt_remapIndexBuffer(remappedIndices.data(), indices, sub.indexCount, remap.data());
                std::copy(remappedIndices.begin(), remappedIndices.end(), indices);

                auto remapVertexSlice = [&](auto& values) {
                    using Value = typename std::decay_t<decltype(values)>::value_type;
                    if (values.size() != outMesh.vertexCount)
                        return;
                    std::vector<Value> source(values.begin() + static_cast<std::ptrdiff_t>(sub.vertexOffset),
                                              values.begin() +
                                                  static_cast<std::ptrdiff_t>(sub.vertexOffset + sub.vertexCount));
                    std::vector<Value> remapped(sub.vertexCount);
                    meshopt_remapVertexBuffer(remapped.data(), source.data(), sub.vertexCount, sizeof(Value), remap.data());
                    std::copy(remapped.begin(), remapped.end(), values.begin() + static_cast<std::ptrdiff_t>(sub.vertexOffset));
                };

                remapVertexSlice(outMesh.positions);
                remapVertexSlice(outMesh.normals);
                remapVertexSlice(outMesh.colors);
                remapVertexSlice(outMesh.texCoords0);
                remapVertexSlice(outMesh.texCoords1);
                remapVertexSlice(outMesh.tangents);
                remapVertexSlice(outMesh.jointIndices);
                remapVertexSlice(outMesh.jointWeights);
            }
        }
    }

    // ─── VGaussianSplatImporter ──────────────────────────────────────────────────

    namespace
    {
        // Peak-normalize interleaved PCM in place. No-op on silence.
        void normalizePcm16(std::vector<std::byte>& data)
        {
            auto*        samples = reinterpret_cast<int16_t*>(data.data());
            const size_t count   = data.size() / sizeof(int16_t);
            int32_t      peak    = 0;
            for (size_t i = 0; i < count; ++i)
                peak = std::max(peak, std::abs(static_cast<int32_t>(samples[i])));
            if (peak == 0 || peak >= 32767)
                return;
            const float scale = 32767.0f / static_cast<float>(peak);
            for (size_t i = 0; i < count; ++i)
            {
                const float v = static_cast<float>(samples[i]) * scale;
                samples[i] = static_cast<int16_t>(std::clamp(v, -32768.0f, 32767.0f));
            }
        }

        void normalizePcmF32(std::vector<std::byte>& data)
        {
            auto*        samples = reinterpret_cast<float*>(data.data());
            const size_t count   = data.size() / sizeof(float);
            float        peak    = 0.0f;
            for (size_t i = 0; i < count; ++i)
                peak = std::max(peak, std::abs(samples[i]));
            if (peak <= 0.0f || peak >= 1.0f)
                return;
            const float scale = 1.0f / peak;
            for (size_t i = 0; i < count; ++i)
                samples[i] *= scale;
        }

        // Convert interleaved PCM between format/channels/rate with ma_data_converter.
        // Returns the converted buffer and frame count, or false on failure.
        bool convertPcmFrames(const void*             srcFrames,
                              uint64_t                srcFrameCount,
                              ma_format               srcFormat,
                              uint32_t                srcChannels,
                              uint32_t                srcRate,
                              ma_format               dstFormat,
                              uint32_t                dstChannels,
                              uint32_t                dstRate,
                              std::vector<std::byte>& outData,
                              uint64_t&               outFrameCount)
        {
            ma_data_converter_config config =
                ma_data_converter_config_init(srcFormat, dstFormat, srcChannels, dstChannels, srcRate, dstRate);
            ma_data_converter converter;
            if (ma_data_converter_init(&config, nullptr, &converter) != MA_SUCCESS)
                return false;

            ma_uint64 expectedFrames = 0;
            if (ma_data_converter_get_expected_output_frame_count(&converter, srcFrameCount, &expectedFrames) !=
                MA_SUCCESS)
            {
                ma_data_converter_uninit(&converter, nullptr);
                return false;
            }

            const size_t dstBpf = ma_get_bytes_per_frame(dstFormat, dstChannels);
            const size_t srcBpf = ma_get_bytes_per_frame(srcFormat, srcChannels);
            // Headroom for resampler tail frames.
            outData.resize(static_cast<size_t>(expectedFrames + 64) * dstBpf);

            ma_uint64 totalIn  = 0;
            ma_uint64 totalOut = 0;
            for (;;)
            {
                ma_uint64 inFrames  = srcFrameCount - totalIn;
                ma_uint64 outFrames = (outData.size() / dstBpf) - totalOut;
                if (outFrames == 0)
                {
                    outData.resize(outData.size() + 4096 * dstBpf);
                    outFrames = (outData.size() / dstBpf) - totalOut;
                }
                const auto* in  = static_cast<const uint8_t*>(srcFrames) + totalIn * srcBpf;
                auto*       out = reinterpret_cast<uint8_t*>(outData.data()) + totalOut * dstBpf;
                if (ma_data_converter_process_pcm_frames(
                        &converter, inFrames != 0 ? in : nullptr, &inFrames, out, &outFrames) != MA_SUCCESS)
                {
                    ma_data_converter_uninit(&converter, nullptr);
                    return false;
                }
                totalIn += inFrames;
                totalOut += outFrames;
                // Keep draining with empty input until the resampler tail is flushed.
                if (inFrames == 0 && outFrames == 0)
                {
                    if (totalIn >= srcFrameCount)
                        break;
                    ma_data_converter_uninit(&converter, nullptr);
                    return false; // converter stalled mid-stream
                }
            }
            ma_data_converter_uninit(&converter, nullptr);

            outData.resize(static_cast<size_t>(totalOut) * dstBpf);
            outFrameCount = totalOut;
            return true;
        }
    } // namespace

    VAudioImporter::VAudioImporter(VAssetRegistry& registry) : m_Registry(registry) {}

    VAudioImporter& VAudioImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    vbase::Result<vbase::UUID, AssetError>
    VAudioImporter::importAudio(vbase::StringView filePath, VAudio& outAudio, bool forceReimport) const
    {
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        const std::string relativeSrcPath = m_Registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string relativeImportedPath = m_Registry.getImportedAssetPath(
            VAssetType::eAudio,
            importedAssetKeyForCookedOutput(m_Registry, VAssetType::eAudio, relativeSrcPath),
            true);

        VImport sourceVImport {};
        auto    sourceSidecarPath = osPath;
        sourceSidecarPath.replace_extension(".vimport");
        if (auto loaded = loadVImport(sourceSidecarPath.generic_string()))
            sourceVImport = std::move(loaded.value());

        const std::string ext            = osPath.extension().generic_string();
        const bool        isOgg          = ext == ".ogg";
        const auto        resolvedParams = resolveAudioImportParams(sourceVImport.params, ext, m_Options);
        auto              options        = resolvedParams.options;

        // Pin the passthrough variant to the actual source format; ogg cannot
        // passthrough because the runtime has no vorbis decoder.
        if (isPassthrough(options.storage))
        {
            if (ext == ".mp3")
                options.storage = VAudioStorage::ePassthroughMp3;
            else if (ext == ".flac")
                options.storage = VAudioStorage::ePassthroughFlac;
            else if (ext == ".wav")
                options.storage = VAudioStorage::ePassthroughWav;
            else
                options.storage = VAudioStorage::ePCM16;
        }

        const auto     lookupUUID = vbase::uuid_from_string_key(relativeImportedPath);
        const uint64_t sourceHash = hashFile(osPath);
        constexpr uint64_t dependencyHash = 0;
        const uint64_t paramsHash = audioImportParamsHash(options);
        constexpr auto importerVersion = "audio:1";
        constexpr auto outputSchema = "vaudio:1";

        auto entry = m_Registry.lookup(lookupUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport &&
            outputFilesAreNewerThanSource(m_Registry, osPath, {relativeImportedPath}))
        {
            if (importDatabaseRecordIsCurrent(m_Registry,
                                              lookupUUID,
                                              toString(VAssetType::eAudio),
                                              relativeSrcPath,
                                              relativeImportedPath,
                                              importerVersion,
                                              outputSchema,
                                              sourceHash,
                                              dependencyHash,
                                              paramsHash,
                                              {relativeImportedPath}))
            {
                (void)updateImportDatabaseRecord(m_Registry,
                                                 lookupUUID,
                                                 toString(VAssetType::eAudio),
                                                 relativeSrcPath,
                                                 relativeImportedPath,
                                                 importerVersion,
                                                 outputSchema,
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);
                std::cout << "Audio already imported: " << entry.sourcePath << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
            }
        }

        const auto fileBytes = readAll(osPath);
        if (fileBytes.empty())
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        outAudio = {};
        outAudio.uuid           = lookupUUID;
        outAudio.name           = osPath.stem().generic_string();
        outAudio.storage        = options.storage;
        outAudio.sourceFileName = relativeSrcPath;

        if (isPassthrough(options.storage))
        {
            // Keep the encoded bytes; probe metadata with a native-format decoder.
            ma_decoder_config config = ma_decoder_config_init(ma_format_unknown, 0, 0);
            ma_decoder        decoder;
            if (ma_decoder_init_memory(fileBytes.data(), fileBytes.size(), &config, &decoder) != MA_SUCCESS)
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

            ma_format fmt  = ma_format_unknown;
            ma_uint32 ch   = 0;
            ma_uint32 rate = 0;
            ma_decoder_get_data_format(&decoder, &fmt, &ch, &rate, nullptr, 0);
            ma_uint64 frames = 0;
            (void)ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
            ma_decoder_uninit(&decoder);
            if (ch == 0 || rate == 0)
                return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

            outAudio.sampleRate = rate;
            outAudio.channels   = ch;
            outAudio.frameCount = frames;
            outAudio.audioData.resize(fileBytes.size());
            std::memcpy(outAudio.audioData.data(), fileBytes.data(), fileBytes.size());
        }
        else
        {
            const ma_format dstFormat =
                options.storage == VAudioStorage::ePCMF32 ? ma_format_f32 : ma_format_s16;

            if (isOgg)
            {
                int    srcChannels = 0;
                int    srcRate     = 0;
                short* samples     = nullptr;
                const int frames   = stb_vorbis_decode_memory(
                    fileBytes.data(), static_cast<int>(fileBytes.size()), &srcChannels, &srcRate, &samples);
                if (frames <= 0 || samples == nullptr || srcChannels <= 0 || srcRate <= 0)
                    return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

                const uint32_t dstChannels = options.forceMono ? 1u : static_cast<uint32_t>(srcChannels);
                const uint32_t dstRate =
                    options.targetSampleRate != 0u ? options.targetSampleRate : static_cast<uint32_t>(srcRate);

                bool ok = true;
                if (dstFormat == ma_format_s16 && dstChannels == static_cast<uint32_t>(srcChannels) &&
                    dstRate == static_cast<uint32_t>(srcRate))
                {
                    const size_t byteCount =
                        static_cast<size_t>(frames) * static_cast<size_t>(srcChannels) * sizeof(int16_t);
                    outAudio.audioData.resize(byteCount);
                    std::memcpy(outAudio.audioData.data(), samples, byteCount);
                    outAudio.frameCount = static_cast<uint64_t>(frames);
                }
                else
                {
                    uint64_t convertedFrames = 0;
                    ok = convertPcmFrames(samples,
                                          static_cast<uint64_t>(frames),
                                          ma_format_s16,
                                          static_cast<uint32_t>(srcChannels),
                                          static_cast<uint32_t>(srcRate),
                                          dstFormat,
                                          dstChannels,
                                          dstRate,
                                          outAudio.audioData,
                                          convertedFrames);
                    outAudio.frameCount = convertedFrames;
                }
                std::free(samples);
                if (!ok)
                    return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

                outAudio.sampleRate = dstRate;
                outAudio.channels   = dstChannels;
            }
            else
            {
                // ma_decoder converts format/channels/rate on the fly.
                ma_decoder_config config = ma_decoder_config_init(
                    dstFormat, options.forceMono ? 1u : 0u, options.targetSampleRate);
                ma_decoder decoder;
                if (ma_decoder_init_memory(fileBytes.data(), fileBytes.size(), &config, &decoder) != MA_SUCCESS)
                    return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

                const uint32_t dstChannels = decoder.outputChannels;
                const uint32_t dstRate     = decoder.outputSampleRate;
                const size_t   bpf         = ma_get_bytes_per_frame(dstFormat, dstChannels);

                uint64_t totalFrames = 0;
                for (;;)
                {
                    constexpr ma_uint64 kChunkFrames = 16384;
                    outAudio.audioData.resize(static_cast<size_t>(totalFrames + kChunkFrames) * bpf);
                    auto* dst = reinterpret_cast<uint8_t*>(outAudio.audioData.data()) + totalFrames * bpf;

                    ma_uint64 framesRead = 0;
                    const ma_result rr = ma_decoder_read_pcm_frames(&decoder, dst, kChunkFrames, &framesRead);
                    totalFrames += framesRead;
                    if (rr != MA_SUCCESS || framesRead < kChunkFrames)
                        break;
                }
                ma_decoder_uninit(&decoder);

                if (totalFrames == 0)
                    return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

                outAudio.audioData.resize(static_cast<size_t>(totalFrames) * bpf);
                outAudio.frameCount = totalFrames;
                outAudio.sampleRate = dstRate;
                outAudio.channels   = dstChannels;
            }

            if (options.normalize)
            {
                if (dstFormat == ma_format_s16)
                    normalizePcm16(outAudio.audioData);
                else
                    normalizePcmF32(outAudio.audioData);
            }
        }

        outAudio.duration =
            outAudio.sampleRate != 0u
                ? static_cast<float>(static_cast<double>(outAudio.frameCount) / outAudio.sampleRate)
                : 0.0f;

        const std::string importedPath =
            (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeImportedPath).generic_string();

        auto sr_audio = saveAudio(outAudio, importedPath);
        if (!sr_audio)
        {
            std::cerr << "Failed to save audio." << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::err(sr_audio.error());
        }

        VImport vimport {};
        vimport.importer = toString(VAssetType::eAudio);
        vimport.uid      = outAudio.uuid;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;
        vimport.params =
            normalizedAudioImportParams(std::move(sourceVImport.params), resolvedParams.subtype, options);
        auto sr_import = saveVImport(vimport, sourceSidecarPath.generic_string());
        if (!sr_import)
            return vbase::Result<vbase::UUID, AssetError>::err(sr_import.error());

        auto rr = m_Registry.registerAsset(outAudio.uuid, relativeSrcPath, relativeImportedPath, VAssetType::eAudio);
        if (!rr)
            return vbase::Result<vbase::UUID, AssetError>::err(rr.error());

        auto dr = updateImportDatabaseRecord(m_Registry,
                                             outAudio.uuid,
                                             toString(VAssetType::eAudio),
                                             relativeSrcPath,
                                             relativeImportedPath,
                                             importerVersion,
                                             outputSchema,
                                             sourceHash,
                                             dependencyHash,
                                             paramsHash);
        if (!dr)
            return vbase::Result<vbase::UUID, AssetError>::err(dr.error());
        return vbase::Result<vbase::UUID, AssetError>::ok(outAudio.uuid);
    }

    VFontImporter::VFontImporter(VAssetRegistry& registry) : m_Registry(registry) {}

    VFontImporter& VFontImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    vbase::Result<vbase::UUID, AssetError>
    VFontImporter::importFont(vbase::StringView filePath, VFont& outFont, bool forceReimport) const
    {
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath))
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        const std::string relativeSrcPath = m_Registry.getSourceAssetPath(osPath.generic_string(), true);
        const std::string relativeImportedPath = m_Registry.getImportedAssetPath(
            VAssetType::eFont,
            importedAssetKeyForCookedOutput(m_Registry, VAssetType::eFont, relativeSrcPath),
            true);

        VImport sourceVImport {};
        auto    sourceSidecarPath = osPath;
        sourceSidecarPath.replace_extension(".vimport");
        if (auto loaded = loadVImport(sourceSidecarPath.generic_string()))
            sourceVImport = std::move(loaded.value());

        const std::string ext = osPath.extension().generic_string();

        const auto         lookupUUID     = vbase::uuid_from_string_key(relativeImportedPath);
        const uint64_t     sourceHash     = hashFile(osPath);
        constexpr uint64_t dependencyHash = 0;
        constexpr uint64_t paramsHash     = 0;
        constexpr auto     importerVersion = "font:1";
        constexpr auto     outputSchema    = "vfont:1";

        auto entry = m_Registry.lookup(lookupUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport &&
            outputFilesAreNewerThanSource(m_Registry, osPath, {relativeImportedPath}))
        {
            if (importDatabaseRecordIsCurrent(m_Registry,
                                              lookupUUID,
                                              toString(VAssetType::eFont),
                                              relativeSrcPath,
                                              relativeImportedPath,
                                              importerVersion,
                                              outputSchema,
                                              sourceHash,
                                              dependencyHash,
                                              paramsHash,
                                              {relativeImportedPath}))
            {
                (void)updateImportDatabaseRecord(m_Registry,
                                                 lookupUUID,
                                                 toString(VAssetType::eFont),
                                                 relativeSrcPath,
                                                 relativeImportedPath,
                                                 importerVersion,
                                                 outputSchema,
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);
                std::cout << "Font already imported: " << entry.sourcePath << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
            }
        }

        const auto fileBytes = readAll(osPath);
        if (fileBytes.empty())
            return vbase::Result<vbase::UUID, AssetError>::err(AssetError::eImportFailed);

        outFont                = {};
        outFont.uuid           = lookupUUID;
        outFont.name           = osPath.stem().generic_string();
        outFont.format         = ext == ".otf" ? VFontFormat::eOTF : VFontFormat::eTTF;
        outFont.sourceFileName = relativeSrcPath;
        outFont.fontData.resize(fileBytes.size());
        std::memcpy(outFont.fontData.data(), fileBytes.data(), fileBytes.size());

        const std::string importedPath =
            (std::filesystem::path(m_Registry.getAssetRootPath()) / relativeImportedPath).generic_string();

        auto sr_font = saveFont(outFont, importedPath);
        if (!sr_font)
        {
            std::cerr << "Failed to save font." << std::endl;
            return vbase::Result<vbase::UUID, AssetError>::err(sr_font.error());
        }

        VImport vimport {};
        vimport.importer = toString(VAssetType::eFont);
        vimport.uid      = outFont.uuid;
        vimport.source   = relativeSrcPath;
        vimport.output   = relativeImportedPath;
        vimport.params   = std::move(sourceVImport.params);
        auto sr_import   = saveVImport(vimport, sourceSidecarPath.generic_string());
        if (!sr_import)
            return vbase::Result<vbase::UUID, AssetError>::err(sr_import.error());

        auto rr = m_Registry.registerAsset(outFont.uuid, relativeSrcPath, relativeImportedPath, VAssetType::eFont);
        if (!rr)
            return vbase::Result<vbase::UUID, AssetError>::err(rr.error());

        auto dr = updateImportDatabaseRecord(m_Registry,
                                             outFont.uuid,
                                             toString(VAssetType::eFont),
                                             relativeSrcPath,
                                             relativeImportedPath,
                                             importerVersion,
                                             outputSchema,
                                             sourceHash,
                                             dependencyHash,
                                             paramsHash);
        if (!dr)
            return vbase::Result<vbase::UUID, AssetError>::err(dr.error());
        return vbase::Result<vbase::UUID, AssetError>::ok(outFont.uuid);
    }

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
        const uint64_t sourceHash     = hashFile(osPath);
        constexpr uint64_t dependencyHash = 0;
        const uint64_t paramsHash     = gaussianSplatImportParamsHash(m_Options);
        constexpr auto importerVersion = "gaussian_splat:1";
        constexpr auto outputSchema = "vgaussiansplat:1";

        auto entry      = m_Registry.lookup(lookupUUID);
        if (entry.type != VAssetType::eUnknown && !forceReimport &&
            outputFilesAreNewerThanSource(m_Registry, osPath, {relativeImportedPath}))
        {
            if (importDatabaseRecordIsCurrent(m_Registry,
                                              lookupUUID,
                                              toString(VAssetType::eGaussianSplat),
                                              relativeSrcPath,
                                              relativeImportedPath,
                                              importerVersion,
                                              outputSchema,
                                              sourceHash,
                                              dependencyHash,
                                              paramsHash,
                                              {relativeImportedPath}))
            {
                (void)updateImportDatabaseRecord(m_Registry,
                                                 lookupUUID,
                                                 toString(VAssetType::eGaussianSplat),
                                                 relativeSrcPath,
                                                 relativeImportedPath,
                                                 importerVersion,
                                                 outputSchema,
                                                 sourceHash,
                                                 dependencyHash,
                                                 paramsHash);
                std::cout << "GaussianSplat already imported: " << entry.sourcePath << std::endl;
                return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
            }
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

        auto dr = updateImportDatabaseRecord(m_Registry,
                                             lookupUUID,
                                             toString(VAssetType::eGaussianSplat),
                                             relativeSrcPath,
                                             relativeImportedPath,
                                             importerVersion,
                                             outputSchema,
                                             sourceHash,
                                             dependencyHash,
                                             paramsHash);
        if (!dr)
            return vbase::Result<vbase::UUID, AssetError>::err(dr.error());

        return vbase::Result<vbase::UUID, AssetError>::ok(lookupUUID);
    }

    VAssetImporter::VAssetImporter(VAssetRegistry& registry) :
        m_Registry(registry), m_TextureImporter(registry), m_MeshImporter(registry),
        m_GaussianSplatImporter(registry), m_AudioImporter(registry), m_FontImporter(registry)
    {}

    VAssetImporter& VAssetImporter::setOptions(const ImportOptions& options)
    {
        m_Options = options;
        return *this;
    }

    vbase::Result<void, AssetError> VAssetImporter::importOrReimportAssetFolder(vbase::StringView folderPath,
                                                                                bool              reimport)
    {
        std::filesystem::path osPath(folderPath);
        if (!std::filesystem::exists(osPath) || !std::filesystem::is_directory(osPath))
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        enum class CandidateKind
        {
            eTexture,
            eMesh,
            eSplat,
            eAudio,
            eFont,
            eShaderLibrary,
            eText,
        };

        struct ImportCandidate
        {
            std::filesystem::path path;
            std::string           relativePath;
            std::string           extension;
            CandidateKind         kind {CandidateKind::eText};
        };

        auto notifyProgress = [this](ImportProgress::Phase phase,
                                     size_t                processedFiles,
                                     size_t                totalFiles,
                                     const std::string&    currentPath = std::string {}) {
            if (m_Options.progress)
            {
                m_Options.progress(ImportProgress {.phase          = phase,
                                                   .processedFiles = processedFiles,
                                                   .totalFiles     = totalFiles,
                                                   .currentPath    = currentPath});
            }
        };

        size_t scannedRegularFiles = 0;
        size_t skippedFiles        = 0;
        size_t importedTextures    = 0;
        size_t importedMeshes      = 0;
        size_t importedSplats      = 0;
        size_t importedAudios      = 0;
        size_t importedFonts       = 0;
        size_t importedTextAssets  = 0;
        std::vector<ImportCandidate> candidates;

        const auto ignoredDirectories = makeIgnoredAssetImportDirectories(m_Registry, m_Options);

        std::cout << "[vasset] scanning asset folder: " << osPath.generic_string() << std::endl;
        notifyProgress(ImportProgress::Phase::eScan, 0, 0);

        for (auto it = std::filesystem::recursive_directory_iterator(osPath);
             it != std::filesystem::recursive_directory_iterator();
             ++it)
        {
            const auto&       entry   = *it;
            const std::string relPath = std::filesystem::relative(entry.path(), osPath).generic_string();
            if (entry.is_directory())
            {
                if (isIgnoredAssetImportDirectory(relPath, ignoredDirectories))
                {
                    std::cout << "[vasset] skip dir : " << relPath << std::endl;
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!entry.is_regular_file())
                continue;
            const std::string filePath = entry.path().generic_string();
            if (isIgnoredAssetImportPath(relPath, ignoredDirectories))
                continue;
            const std::string ext = entry.path().extension().generic_string();
            if (ext == ".vimport")
                continue;
            ++scannedRegularFiles;

            if (isValidTexture(ext))
            {
                candidates.push_back({entry.path(), relPath, ext, CandidateKind::eTexture});
            }
            else if (isValidModel(ext))
            {
                candidates.push_back({entry.path(), relPath, ext, CandidateKind::eMesh});
            }
            else if (isValidGaussianSplat(ext))
            {
                candidates.push_back({entry.path(), relPath, ext, CandidateKind::eSplat});
            }
            else if (isValidAudio(ext))
            {
                candidates.push_back({entry.path(), relPath, ext, CandidateKind::eAudio});
            }
            else if (isValidFont(ext))
            {
                candidates.push_back({entry.path(), relPath, ext, CandidateKind::eFont});
            }
            else if (isValidShaderLibraryManifest(entry.path()))
            {
                if (m_Options.importShaderLibraries)
                {
                    candidates.push_back({entry.path(), relPath, ext, CandidateKind::eShaderLibrary});
                }
                else
                {
                    ++skippedFiles;
                }
            }
            else if (isValidSourceTextAsset(entry.path()))
            {
                candidates.push_back({entry.path(), relPath, ext, CandidateKind::eText});
            }
            else
            {
                ++skippedFiles;
            }
            notifyProgress(ImportProgress::Phase::eScan, scannedRegularFiles, 0, relPath);
        }

        notifyProgress(ImportProgress::Phase::eImport, 0, candidates.size());

        for (size_t index = 0; index < candidates.size(); ++index)
        {
            const auto& candidate = candidates[index];
            const auto  filePath  = candidate.path.generic_string();
            notifyProgress(ImportProgress::Phase::eImport, index, candidates.size(), candidate.relativePath);

            if (candidate.kind == CandidateKind::eTexture)
            {
                std::cout << "[vasset] texture  : " << candidate.relativePath << std::endl;
                VTexture texture;
                auto     tr = m_TextureImporter.importTexture(filePath, texture, reimport);
                if (!tr)
                {
                    std::cerr << "[vasset] failed texture  : " << candidate.relativePath << " ("
                              << assetErrorLabel(tr.error()) << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(tr.error());
                }
                ++importedTextures;
            }
            else if (candidate.kind == CandidateKind::eMesh)
            {
                std::cout << "[vasset] mesh     : " << candidate.relativePath << std::endl;
                m_MeshImporter.setProgressCallback([this](std::string item, size_t processed, size_t total) {
                    if (m_Options.progress)
                    {
                        m_Options.progress(ImportProgress {.phase          = ImportProgress::Phase::eImport,
                                                           .processedFiles = processed,
                                                           .totalFiles     = total,
                                                           .currentPath    = std::move(item)});
                    }
                });
                VMesh mesh;
                auto  mr = m_MeshImporter.importMesh(filePath, mesh, reimport);
                m_MeshImporter.setProgressCallback({});
                if (!mr)
                {
                    std::cerr << "[vasset] failed mesh     : " << candidate.relativePath << " ("
                              << assetErrorLabel(mr.error()) << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(mr.error());
                }
                ++importedMeshes;
            }
            else if (candidate.kind == CandidateKind::eSplat)
            {
                std::cout << "[vasset] splat    : " << candidate.relativePath << std::endl;
                VGaussianSplat splat;
                auto           gr = m_GaussianSplatImporter.importGaussianSplat(filePath, splat, reimport);
                if (!gr)
                {
                    std::cerr << "[vasset] failed splat    : " << candidate.relativePath << " ("
                              << assetErrorLabel(gr.error()) << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(gr.error());
                }
                ++importedSplats;
            }
            else if (candidate.kind == CandidateKind::eAudio)
            {
                std::cout << "[vasset] audio    : " << candidate.relativePath << std::endl;
                VAudio audio;
                auto   ar = m_AudioImporter.importAudio(filePath, audio, reimport);
                if (!ar)
                {
                    std::cerr << "[vasset] failed audio    : " << candidate.relativePath << " ("
                              << assetErrorLabel(ar.error()) << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(ar.error());
                }
                ++importedAudios;
            }
            else if (candidate.kind == CandidateKind::eFont)
            {
                std::cout << "[vasset] font     : " << candidate.relativePath << std::endl;
                VFont font;
                auto  fr = m_FontImporter.importFont(filePath, font, reimport);
                if (!fr)
                {
                    std::cerr << "[vasset] failed font     : " << candidate.relativePath << " ("
                              << assetErrorLabel(fr.error()) << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(fr.error());
                }
                ++importedFonts;
            }
            else if (candidate.kind == CandidateKind::eShaderLibrary)
            {
                std::cout << "[vasset] shaderlib: " << candidate.relativePath << std::endl;
                auto rr = importShaderLibraryManifest(
                    m_Registry, filePath, reimport, m_Options.shaderVirtualIncludes, m_Options.diagnostics);
                if (!rr)
                {
                    std::cerr << "[vasset] failed shaderlib: " << candidate.relativePath << " ("
                              << assetErrorLabel(rr.error()) << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(rr.error());
                }
                ++importedTextAssets;
            }
            else if (candidate.kind == CandidateKind::eText)
            {
                std::cout << "[vasset] text     : " << candidate.relativePath << std::endl;
                auto type = inferSourceTextAssetType(candidate.path);
                auto rr   = importSourceTextAsset(m_Registry, filePath, reimport, type);
                if (!rr)
                {
                    std::cerr << "[vasset] failed text     : " << candidate.relativePath << " ("
                              << assetErrorLabel(rr.error()) << ")" << std::endl;
                    return vbase::Result<void, AssetError>::err(rr.error());
                }
                ++importedTextAssets;
            }

            notifyProgress(ImportProgress::Phase::eImport, index + 1, candidates.size(), candidate.relativePath);
        }

        std::cout << "[vasset] summary  : scanned=" << scannedRegularFiles << ", textures=" << importedTextures
                  << ", meshes=" << importedMeshes << ", splats=" << importedSplats
                  << ", audios=" << importedAudios << ", fonts=" << importedFonts
                  << ", text_assets=" << importedTextAssets << ", skipped=" << skippedFiles << std::endl;
        notifyProgress(ImportProgress::Phase::eDone, candidates.size(), candidates.size());

        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> VAssetImporter::importOrReimportAsset(vbase::StringView filePath, bool reimport)
    {
        std::filesystem::path osPath(filePath);
        if (!std::filesystem::exists(osPath) || std::filesystem::is_directory(osPath))
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        const std::string relPath =
            std::filesystem::relative(osPath, std::filesystem::path(m_Registry.getAssetRootPath())).generic_string();
        const auto ignoredDirectories = makeIgnoredAssetImportDirectories(m_Registry, m_Options);
        if (isIgnoredAssetImportPath(relPath, ignoredDirectories))
            return vbase::Result<void, AssetError>::err(AssetError::eNotSupported);

        auto notifyProgress = [this, &relPath](ImportProgress::Phase phase, size_t processedFiles) {
            if (m_Options.progress)
            {
                m_Options.progress(ImportProgress {.phase          = phase,
                                                   .processedFiles = processedFiles,
                                                   .totalFiles     = 1,
                                                   .currentPath    = relPath});
            }
        };

        notifyProgress(ImportProgress::Phase::eScan, 0);
        const std::string ext = osPath.extension().generic_string();
        notifyProgress(ImportProgress::Phase::eImport, 0);

        if (isValidTexture(ext))
        {
            VTexture texture;
            auto     tr = m_TextureImporter.importTexture(filePath, texture, reimport);
            if (!tr)
                return vbase::Result<void, AssetError>::err(tr.error());
            notifyProgress(ImportProgress::Phase::eDone, 1);
            return vbase::Result<void, AssetError>::ok();
        }

        if (isValidModel(ext))
        {
            m_MeshImporter.setProgressCallback([this](std::string item, size_t processed, size_t total) {
                if (m_Options.progress)
                {
                    m_Options.progress(ImportProgress {.phase          = ImportProgress::Phase::eImport,
                                                       .processedFiles = processed,
                                                       .totalFiles     = total,
                                                       .currentPath    = std::move(item)});
                }
            });
            VMesh mesh;
            auto  mr = m_MeshImporter.importMesh(filePath, mesh, reimport);
            m_MeshImporter.setProgressCallback({});
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
            notifyProgress(ImportProgress::Phase::eDone, 1);
            return vbase::Result<void, AssetError>::ok();
        }

        if (isValidAudio(ext))
        {
            VAudio audio;
            auto   ar = m_AudioImporter.importAudio(filePath, audio, reimport);
            if (!ar)
                return vbase::Result<void, AssetError>::err(ar.error());
            notifyProgress(ImportProgress::Phase::eDone, 1);
            return vbase::Result<void, AssetError>::ok();
        }

        if (isValidFont(ext))
        {
            VFont font;
            auto  fr = m_FontImporter.importFont(filePath, font, reimport);
            if (!fr)
                return vbase::Result<void, AssetError>::err(fr.error());
            notifyProgress(ImportProgress::Phase::eDone, 1);
            return vbase::Result<void, AssetError>::ok();
        }

        if (isValidShaderLibraryManifest(osPath))
        {
            auto rr = importShaderLibraryManifest(
                m_Registry, filePath, reimport, m_Options.shaderVirtualIncludes, m_Options.diagnostics);
            if (!rr)
                return vbase::Result<void, AssetError>::err(rr.error());
            notifyProgress(ImportProgress::Phase::eDone, 1);
            return vbase::Result<void, AssetError>::ok();
        }

        if (isValidSourceTextAsset(osPath))
        {
            auto type = inferSourceTextAssetType(osPath);
            auto rr   = importSourceTextAsset(m_Registry, filePath, reimport, type);
            if (!rr)
                return vbase::Result<void, AssetError>::err(rr.error());
            notifyProgress(ImportProgress::Phase::eDone, 1);
            return vbase::Result<void, AssetError>::ok();
        }

        return vbase::Result<void, AssetError>::err(AssetError::eNotSupported);
    }

} // namespace vasset
