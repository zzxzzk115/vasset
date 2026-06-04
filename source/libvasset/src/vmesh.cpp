#include "vasset/vmesh.hpp"
#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>

#include <zstd.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace vasset
{
    struct VMeshFileHeader
    {
        char     magic[16]; // "VMESH"
        uint32_t version;   // 1
        uint32_t flags;     // bit0 = compressed
        uint64_t rawSize;   // uncompressed size
    };

    namespace
    {
        constexpr uint32_t kMetaHasDefaultTransform = 1u << 0u;
        constexpr uint32_t kMetaHasLocalBounds      = 1u << 1u;

        bool computeLocalBounds(const std::vector<VPosition>& positions, glm::vec3& outMin, glm::vec3& outMax)
        {
            if (positions.empty())
                return false;

            glm::vec3 minP(std::numeric_limits<float>::infinity());
            glm::vec3 maxP(-std::numeric_limits<float>::infinity());
            for (const auto& position : positions)
            {
                minP = glm::min(minP, position);
                maxP = glm::max(maxP, position);
            }

            if (!std::isfinite(minP.x) || !std::isfinite(minP.y) || !std::isfinite(minP.z) ||
                !std::isfinite(maxP.x) || !std::isfinite(maxP.y) || !std::isfinite(maxP.z))
            {
                return false;
            }

            outMin = minP;
            outMax = maxP;
            return true;
        }
    } // namespace

    vbase::Result<void, AssetError> saveMesh(const VMesh& mesh, vbase::StringView filePath, int zstdLevel)
    {
        // ------------------------------------------------------------
        // Prepare output directory
        // ------------------------------------------------------------
        std::filesystem::path path(filePath);
        if (path.has_parent_path() && !std::filesystem::exists(path.parent_path()))
        {
            std::filesystem::create_directories(path.parent_path());
        }

        // ------------------------------------------------------------
        // Serialize original VMESH format into memory first
        // ------------------------------------------------------------
        std::vector<uint8_t> raw;
        raw.reserve(1024 * 64);

        auto writeRaw = [&](const void* data, size_t size) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(data);

            raw.insert(raw.end(), p, p + size);
        };

        // 16 bytes for magic number
        const char magic[16] = "VMESH\0";
        writeRaw(magic, sizeof(magic));

        // 16 bytes for UUID
        writeRaw(&mesh.uuid, sizeof(mesh.uuid));

        // 4 bytes for vertex count
        writeRaw(&mesh.vertexCount, sizeof(mesh.vertexCount));

        // 4 bytes for vertex flags
        writeRaw(&mesh.vertexFlags, sizeof(mesh.vertexFlags));

        // N vertices
        for (uint32_t i = 0; i < mesh.vertexCount; ++i)
        {
            if (mesh.vertexFlags & VVertexFlags::ePosition)
                writeRaw(&mesh.positions[i], sizeof(VPosition));
            if (mesh.vertexFlags & VVertexFlags::eNormal)
                writeRaw(&mesh.normals[i], sizeof(VNormal));
            if (mesh.vertexFlags & VVertexFlags::eColor)
                writeRaw(&mesh.colors[i], sizeof(VColor));
            if (mesh.vertexFlags & VVertexFlags::eTexCoord0)
                writeRaw(&mesh.texCoords0[i], sizeof(VTexCoord));
            if (mesh.vertexFlags & VVertexFlags::eTexCoord1)
                writeRaw(&mesh.texCoords1[i], sizeof(VTexCoord));
            if (mesh.vertexFlags & VVertexFlags::eTangent)
                writeRaw(&mesh.tangents[i], sizeof(VTangent));
            if (mesh.vertexFlags & VVertexFlags::eJointIndices)
                writeRaw(&mesh.jointIndices[i], sizeof(VJointIndices));
            if (mesh.vertexFlags & VVertexFlags::eJointWeights)
                writeRaw(&mesh.jointWeights[i], sizeof(VJointWeights));
        }

        // 4 bytes for number of indices
        uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
        writeRaw(&indexCount, sizeof(indexCount));

        // N indices
        writeRaw(mesh.indices.data(), indexCount * sizeof(uint32_t));

        // 4 bytes for number of sub-meshes
        uint32_t subMeshCount = static_cast<uint32_t>(mesh.subMeshes.size());
        writeRaw(&subMeshCount, sizeof(subMeshCount));

        // N sub-meshes
        for (const auto& subMesh : mesh.subMeshes)
        {
            // 4 bytes for vertex offset
            writeRaw(&subMesh.vertexOffset, sizeof(subMesh.vertexOffset));

            // 4 bytes for vertex count
            writeRaw(&subMesh.vertexCount, sizeof(subMesh.vertexCount));

            // 4 bytes for index offset
            writeRaw(&subMesh.indexOffset, sizeof(subMesh.indexOffset));

            // 4 bytes for index count
            writeRaw(&subMesh.indexCount, sizeof(subMesh.indexCount));

            // 4 bytes for material index
            writeRaw(&subMesh.materialIndex, sizeof(subMesh.materialIndex));

            // 4 bytes for length of meshlets
            uint32_t meshletCount = static_cast<uint32_t>(subMesh.meshletGroup.meshlets.size());
            writeRaw(&meshletCount, sizeof(uint32_t));

            // N meshlets
            for (const auto& meshlet : subMesh.meshletGroup.meshlets)
            {
                writeRaw(&meshlet.vertexOffset, sizeof(meshlet.vertexOffset));
                writeRaw(&meshlet.vertexCount, sizeof(meshlet.vertexCount));
                writeRaw(&meshlet.triangleOffset, sizeof(meshlet.triangleOffset));
                writeRaw(&meshlet.triangleCount, sizeof(meshlet.triangleCount));
                writeRaw(&meshlet.materialIndex, sizeof(meshlet.materialIndex));
                writeRaw(&meshlet.center, sizeof(meshlet.center));
                writeRaw(&meshlet.radius, sizeof(meshlet.radius));
                writeRaw(&meshlet.coneAxis, sizeof(meshlet.coneAxis));
                writeRaw(&meshlet.coneCutoff, sizeof(meshlet.coneCutoff));
                writeRaw(&meshlet.coneApex, sizeof(meshlet.coneApex));
            }

            // 4 bytes for length of meshlet vertices
            uint32_t meshletVertexCount = static_cast<uint32_t>(subMesh.meshletGroup.meshletVertices.size());
            writeRaw(&meshletVertexCount, sizeof(uint32_t));

            // N meshlet vertices
            for (const auto& vertex : subMesh.meshletGroup.meshletVertices)
            {
                writeRaw(&vertex, sizeof(vertex));
            }

            // 4 bytes for length of meshlet triangles
            uint32_t meshletTriangleCount = static_cast<uint32_t>(subMesh.meshletGroup.meshletTriangles.size());
            writeRaw(&meshletTriangleCount, sizeof(uint32_t));

            // N meshlet triangles
            for (const auto& triangle : subMesh.meshletGroup.meshletTriangles)
            {
                writeRaw(&triangle, sizeof(triangle));
            }

            // 4 bytes for name length
            uint32_t nameLength = static_cast<uint32_t>(subMesh.name.size());
            writeRaw(&nameLength, sizeof(nameLength));

            // N bytes for name
            writeRaw(subMesh.name.c_str(), nameLength);
        }

        // 4 bytes for number of materials
        uint32_t materialCount = static_cast<uint32_t>(mesh.materials.size());
        writeRaw(&materialCount, sizeof(materialCount));

        // N materials
        auto writeString = [&](const std::string& str) {
            uint32_t len = static_cast<uint32_t>(str.size());
            writeRaw(&len, sizeof(len));
            if (len)
                writeRaw(str.data(), len);
        };
        auto writeTextureRef = [&](const VTextureRef& texRef) {
            writeRaw(reinterpret_cast<const char*>(&texRef.uuid), sizeof(texRef.uuid));
        };
        auto writeBytes = [&](const std::vector<uint8_t>& bytes) {
            uint32_t sz = static_cast<uint32_t>(bytes.size());
            writeRaw(&sz, sizeof(sz));
            if (sz)
                writeRaw(bytes.data(), sz);
        };

        for (const auto& material : mesh.materials)
        {
            // 1 byte model
            writeRaw(&material.model, sizeof(material.model));

            // 4 bytes features
            writeRaw(&material.features, sizeof(material.features));

            // name
            writeString(material.name);

            // ---- core payload (by model) ----
            switch (material.model)
            {
                case VMaterialModel::eUnlit: {
                    writeRaw(&material.core.unlit.color, sizeof(material.core.unlit.color));
                    writeTextureRef(material.core.unlit.colorTexture);
                    break;
                }
                case VMaterialModel::ePBRSpecularGlossiness: {
                    writeRaw(&material.core.pbrSG.diffuseColor, sizeof(material.core.pbrSG.diffuseColor));
                    writeRaw(&material.core.pbrSG.specularFactor, sizeof(material.core.pbrSG.specularFactor));
                    writeRaw(&material.core.pbrSG.glossinessFactor, sizeof(material.core.pbrSG.glossinessFactor));
                    writeTextureRef(material.core.pbrSG.diffuseTexture);
                    writeTextureRef(material.core.pbrSG.specularGlossinessTexture);
                    writeTextureRef(material.core.pbrSG.glossinessTexture);
                    writeTextureRef(material.core.pbrSG.normalTexture);
                    break;
                }
                case VMaterialModel::ePhong: {
                    writeRaw(&material.core.phong.diffuse, sizeof(material.core.phong.diffuse));
                    writeRaw(&material.core.phong.specular, sizeof(material.core.phong.specular));
                    writeRaw(&material.core.phong.shininess, sizeof(material.core.phong.shininess));
                    writeRaw(&material.core.phong.opacity, sizeof(material.core.phong.opacity));
                    writeRaw(&material.core.phong.ior, sizeof(material.core.phong.ior));
                    writeRaw(&material.core.phong.emissive, sizeof(material.core.phong.emissive));
                    writeTextureRef(material.core.phong.diffuseTexture);
                    writeTextureRef(material.core.phong.specularTexture);
                    writeTextureRef(material.core.phong.normalTexture);
                    writeTextureRef(material.core.phong.opacityTexture);
                    writeTextureRef(material.core.phong.emissiveTexture);
                    break;
                }
                case VMaterialModel::ePBRMetallicRoughness:
                case VMaterialModel::eUnknown:
                case VMaterialModel::eCustom:
                default: {
                    // Default to PBR MR payload on disk for unknown/custom as best-effort.
                    const auto& pbr = material.core.pbrMR;
                    writeRaw(&pbr.baseColor, sizeof(pbr.baseColor));
                    writeRaw(&pbr.metallicFactor, sizeof(pbr.metallicFactor));
                    writeRaw(&pbr.roughnessFactor, sizeof(pbr.roughnessFactor));

                    writeRaw(&pbr.alphaCutoff, sizeof(pbr.alphaCutoff));
                    writeRaw(&pbr.alphaMode, sizeof(pbr.alphaMode));
                    writeRaw(&pbr.opacity, sizeof(pbr.opacity));
                    writeRaw(&pbr.blendMode, sizeof(pbr.blendMode));

                    writeRaw(&pbr.emissiveColorIntensity, sizeof(pbr.emissiveColorIntensity));
                    writeRaw(&pbr.ambientColor, sizeof(pbr.ambientColor));
                    writeRaw(&pbr.ior, sizeof(pbr.ior));
                    writeRaw(&pbr.doubleSided, sizeof(pbr.doubleSided));

                    writeTextureRef(pbr.baseColorTexture);
                    writeTextureRef(pbr.alphaTexture);
                    writeTextureRef(pbr.metallicTexture);
                    writeTextureRef(pbr.roughnessTexture);
                    writeTextureRef(pbr.metallicRoughnessTexture);
                    writeTextureRef(pbr.specularTexture);
                    writeTextureRef(pbr.normalTexture);
                    writeTextureRef(pbr.ambientOcclusionTexture);
                    writeTextureRef(pbr.emissiveTexture);
                    break;
                }
            }

            // ---- texture bindings (lossless) ----
            uint32_t texCount = static_cast<uint32_t>(material.textures.size());
            writeRaw(&texCount, sizeof(texCount));
            for (const auto& tb : material.textures)
            {
                writeRaw(&tb.type, sizeof(tb.type));
                writeRaw(&tb.index, sizeof(tb.index));
                writeRaw(&tb.uvIndex, sizeof(tb.uvIndex));
                writeRaw(&tb.mapping, sizeof(tb.mapping));
                writeRaw(&tb.op, sizeof(tb.op));
                writeRaw(&tb.mapModeU, sizeof(tb.mapModeU));
                writeRaw(&tb.mapModeV, sizeof(tb.mapModeV));

                writeRaw(&tb.blend, sizeof(tb.blend));
                writeTextureRef(tb.texture);
            }

            // ---- dynamic properties ----
            uint32_t propCount = static_cast<uint32_t>(material.properties.size());
            writeRaw(&propCount, sizeof(propCount));
            for (const auto& prop : material.properties)
            {
                writeString(prop.key);
                writeRaw(&prop.semantic, sizeof(prop.semantic));
                writeRaw(&prop.index, sizeof(prop.index));
                writeRaw(&prop.type, sizeof(prop.type));
                writeBytes(prop.data);
            }
        }

        // name
        uint32_t nameLength = static_cast<uint32_t>(mesh.name.size());
        writeRaw(&nameLength, sizeof(nameLength));
        writeRaw(mesh.name.c_str(), nameLength);

        // Optional payload metadata. Kept at the tail so older readers can ignore it.
        const char metaMagic[16] = "VMESH_META1\0";
        writeRaw(metaMagic, sizeof(metaMagic));
        glm::vec3 localBoundsMin = mesh.localBoundsMin;
        glm::vec3 localBoundsMax = mesh.localBoundsMax;
        const bool hasLocalBounds =
            mesh.hasLocalBounds || computeLocalBounds(mesh.positions, localBoundsMin, localBoundsMax);
        uint32_t metaFlags = 0u;
        if (mesh.hasDefaultTransform)
            metaFlags |= kMetaHasDefaultTransform;
        if (hasLocalBounds)
            metaFlags |= kMetaHasLocalBounds;
        writeRaw(&metaFlags, sizeof(metaFlags));
        writeRaw(&mesh.defaultPosition, sizeof(mesh.defaultPosition));
        writeRaw(&mesh.defaultRotation, sizeof(mesh.defaultRotation));
        writeRaw(&mesh.defaultScale, sizeof(mesh.defaultScale));
        writeRaw(&localBoundsMin, sizeof(localBoundsMin));
        writeRaw(&localBoundsMax, sizeof(localBoundsMax));

        if (mesh.hasSkin)
        {
            const char skinMagic[16] = "VMESH_SKIN1\0";
            writeRaw(skinMagic, sizeof(skinMagic));
            writeRaw(&mesh.skeleton, sizeof(mesh.skeleton));
            writeString(mesh.skeletonPath);

            const uint32_t jointCount = static_cast<uint32_t>(mesh.jointNames.size());
            writeRaw(&jointCount, sizeof(jointCount));
            for (uint32_t i = 0; i < jointCount; ++i)
            {
                writeString(mesh.jointNames[i]);
                const int16_t parent = i < mesh.jointParents.size() ? mesh.jointParents[i] : -1;
                writeRaw(&parent, sizeof(parent));
            }

            const uint32_t inverseBindPoseCount = static_cast<uint32_t>(mesh.inverseBindPoses.size());
            writeRaw(&inverseBindPoseCount, sizeof(inverseBindPoseCount));
            for (const auto& inverseBindPose : mesh.inverseBindPoses)
                writeRaw(&inverseBindPose, sizeof(inverseBindPose));
        }

        // ------------------------------------------------------------
        // Write file
        // ------------------------------------------------------------
        std::ofstream file(std::string(filePath), std::ios::binary);
        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        // -------- Write container header --------
        VMeshFileHeader header {};
        memcpy(header.magic, "VMESH", 6);
        header.version = 1;
        header.flags   = (zstdLevel > 0) ? 1u : 0u;
        header.rawSize = raw.size();

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));

        // -------- Write payload --------
        if (zstdLevel <= 0)
        {
            // No compression
            file.write(reinterpret_cast<const char*>(raw.data()), raw.size());
        }
        else
        {
            size_t const         bound = ZSTD_compressBound(raw.size());
            std::vector<uint8_t> comp(bound);

            size_t const cSize = ZSTD_compress(comp.data(), bound, raw.data(), raw.size(), zstdLevel);

            if (ZSTD_isError(cSize))
            {
                std::cerr << "zstd compress failed: " << ZSTD_getErrorName(cSize) << std::endl;
                return vbase::Result<void, AssetError>::err(AssetError::eIOError);
            }

            file.write(reinterpret_cast<const char*>(comp.data()), cSize);
        }

        file.close();

        // ------------------------------------------------------------
        return vbase::Result<void, AssetError>::ok();
    }

    vbase::Result<void, AssetError> loadMesh(vbase::StringView filePath, VMesh& outMesh)
    {
        std::filesystem::path path(filePath);

        if (!std::filesystem::exists(path))
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        const auto size = std::filesystem::file_size(path);
        if (size == 0)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        std::vector<std::byte> buffer(size);

        std::ifstream file(path, std::ios::binary);
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));

        if (!file)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        return loadMeshFromMemory(buffer, outMesh);
    }

    vbase::Result<void, AssetError> loadMeshFromMemory(const std::vector<std::byte>& data, VMesh& outMesh)
    {
        if (data.size() < sizeof(VMeshFileHeader))
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        size_t offset = 0;

        auto readSafe = [&](void* dst, size_t size) -> bool {
            if (offset + size > data.size())
                return false;

            std::memcpy(dst, data.data() + offset, size);
            offset += size;
            return true;
        };

        // ------------------------------------------------------------
        // Read container header
        // ------------------------------------------------------------
        VMeshFileHeader header {};
        if (!readSafe(&header, sizeof(header)))
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        if (std::string(header.magic) != "VMESH")
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        bool compressed = (header.flags & 1u) != 0;

        // ------------------------------------------------------------
        // Extract payload
        // ------------------------------------------------------------
        std::vector<uint8_t> raw;

        if (!compressed)
        {
            if (offset + header.rawSize > data.size())
                return vbase::Result<void, AssetError>::err(AssetError::eIOError);

            raw.resize(header.rawSize);
            std::memcpy(raw.data(), data.data() + offset, header.rawSize);
        }
        else
        {
            size_t      compSize = data.size() - offset;
            const void* compData = data.data() + offset;

            raw.resize(header.rawSize);

            size_t dSize = ZSTD_decompress(raw.data(), raw.size(), compData, compSize);

            if (ZSTD_isError(dSize) || dSize != header.rawSize)
                return vbase::Result<void, AssetError>::err(AssetError::eIOError);
        }

        // ------------------------------------------------------------
        // Now parse raw VMESH data
        // ------------------------------------------------------------
        size_t rawOffset = 0;

        auto readRaw = [&](void* dst, size_t size) -> bool {
            if (rawOffset + size > raw.size())
                return false;

            std::memcpy(dst, raw.data() + rawOffset, size);
            rawOffset += size;
            return true;
        };

        // 16 bytes magic
        char magic[16];
        readRaw(magic, sizeof(magic));
        if (std::string(magic) != "VMESH")
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        // 16 bytes for UUID
        readRaw(&outMesh.uuid, sizeof(outMesh.uuid));

        // 4 bytes for number of vertices
        readRaw(&outMesh.vertexCount, sizeof(outMesh.vertexCount));

        // 4 bytes for vertex flags
        readRaw(&outMesh.vertexFlags, sizeof(outMesh.vertexFlags));

        // N vertices
        outMesh.positions.resize(outMesh.vertexCount);
        outMesh.normals.resize(outMesh.vertexCount);
        outMesh.colors.resize(outMesh.vertexCount);
        outMesh.texCoords0.resize(outMesh.vertexCount);
        outMesh.texCoords1.resize(outMesh.vertexCount);
        outMesh.tangents.resize(outMesh.vertexCount);
        outMesh.jointIndices.resize(outMesh.vertexCount);
        outMesh.jointWeights.resize(outMesh.vertexCount);

        for (uint32_t i = 0; i < outMesh.vertexCount; ++i)
        {
            if (outMesh.vertexFlags & VVertexFlags::ePosition)
                readRaw(&outMesh.positions[i], sizeof(VPosition));
            if (outMesh.vertexFlags & VVertexFlags::eNormal)
                readRaw(&outMesh.normals[i], sizeof(VNormal));
            if (outMesh.vertexFlags & VVertexFlags::eColor)
                readRaw(&outMesh.colors[i], sizeof(VColor));
            if (outMesh.vertexFlags & VVertexFlags::eTexCoord0)
                readRaw(&outMesh.texCoords0[i], sizeof(VTexCoord));
            if (outMesh.vertexFlags & VVertexFlags::eTexCoord1)
                readRaw(&outMesh.texCoords1[i], sizeof(VTexCoord));
            if (outMesh.vertexFlags & VVertexFlags::eTangent)
                readRaw(&outMesh.tangents[i], sizeof(VTangent));
            if (outMesh.vertexFlags & VVertexFlags::eJointIndices)
                readRaw(&outMesh.jointIndices[i], sizeof(VJointIndices));
            if (outMesh.vertexFlags & VVertexFlags::eJointWeights)
                readRaw(&outMesh.jointWeights[i], sizeof(VJointWeights));
        }

        // 4 bytes for number of indices
        uint32_t indexCount = 0;
        readRaw(&indexCount, sizeof(indexCount));

        outMesh.indices.resize(indexCount);

        // N indices
        readRaw(outMesh.indices.data(), indexCount * sizeof(uint32_t));

        // 4 bytes for number of sub-meshes
        uint32_t subMeshCount = 0;
        readRaw(&subMeshCount, sizeof(subMeshCount));

        outMesh.subMeshes.resize(subMeshCount);

        // N sub-meshes
        for (auto& subMesh : outMesh.subMeshes)
        {
            readRaw(&subMesh.vertexOffset, sizeof(subMesh.vertexOffset));
            readRaw(&subMesh.vertexCount, sizeof(subMesh.vertexCount));
            readRaw(&subMesh.indexOffset, sizeof(subMesh.indexOffset));
            readRaw(&subMesh.indexCount, sizeof(subMesh.indexCount));
            readRaw(&subMesh.materialIndex, sizeof(subMesh.materialIndex));

            // meshlets
            uint32_t meshletCount = 0;
            readRaw(&meshletCount, sizeof(meshletCount));
            subMesh.meshletGroup.meshlets.resize(meshletCount);

            for (auto& meshlet : subMesh.meshletGroup.meshlets)
            {
                readRaw(&meshlet.vertexOffset, sizeof(meshlet.vertexOffset));
                readRaw(&meshlet.vertexCount, sizeof(meshlet.vertexCount));
                readRaw(&meshlet.triangleOffset, sizeof(meshlet.triangleOffset));
                readRaw(&meshlet.triangleCount, sizeof(meshlet.triangleCount));
                readRaw(&meshlet.materialIndex, sizeof(meshlet.materialIndex));
                readRaw(&meshlet.center, sizeof(meshlet.center));
                readRaw(&meshlet.radius, sizeof(meshlet.radius));
                readRaw(&meshlet.coneAxis, sizeof(meshlet.coneAxis));
                readRaw(&meshlet.coneCutoff, sizeof(meshlet.coneCutoff));
                readRaw(&meshlet.coneApex, sizeof(meshlet.coneApex));
            }

            uint32_t meshletVertexCount = 0;
            readRaw(&meshletVertexCount, sizeof(meshletVertexCount));
            subMesh.meshletGroup.meshletVertices.resize(meshletVertexCount);

            for (auto& vertex : subMesh.meshletGroup.meshletVertices)
            {
                readRaw(&vertex, sizeof(vertex));
            }

            uint32_t meshletTriangleCount = 0;
            readRaw(&meshletTriangleCount, sizeof(meshletTriangleCount));
            subMesh.meshletGroup.meshletTriangles.resize(meshletTriangleCount);

            for (auto& triangle : subMesh.meshletGroup.meshletTriangles)
            {
                readRaw(&triangle, sizeof(triangle));
            }

            uint32_t nameLength = 0;
            readRaw(&nameLength, sizeof(nameLength));

            subMesh.name.resize(nameLength);
            readRaw(subMesh.name.data(), nameLength);
        }

        // materials
        uint32_t materialCount = 0;
        readRaw(&materialCount, sizeof(materialCount));

        outMesh.materials.resize(materialCount);

        for (auto& material : outMesh.materials)
        {
            auto readString = [&]() -> std::string {
                uint32_t len = 0;
                readRaw(&len, sizeof(len));
                std::string out;
                out.resize(len);
                if (len)
                    readRaw(out.data(), len);
                return out;
            };
            auto readTextureRef = [&](VTextureRef& texRef) {
                readRaw(reinterpret_cast<char*>(&texRef.uuid), sizeof(texRef.uuid));
            };
            auto readBytes = [&]() -> std::vector<uint8_t> {
                uint32_t sz = 0;
                readRaw(&sz, sizeof(sz));
                std::vector<uint8_t> bytes(sz);
                if (sz)
                    readRaw(bytes.data(), sz);
                return bytes;
            };

            // 1 byte model
            readRaw(&material.model, sizeof(material.model));

            // 4 bytes features
            readRaw(&material.features, sizeof(material.features));

            // name
            material.name = readString();

            // ---- core payload ----
            switch (material.model)
            {
                case VMaterialModel::eUnlit: {
                    material.core.unlit = {};
                    readRaw(&material.core.unlit.color, sizeof(material.core.unlit.color));
                    readTextureRef(material.core.unlit.colorTexture);
                    break;
                }
                case VMaterialModel::ePBRSpecularGlossiness: {
                    material.core.pbrSG = {};
                    readRaw(&material.core.pbrSG.diffuseColor, sizeof(material.core.pbrSG.diffuseColor));
                    readRaw(&material.core.pbrSG.specularFactor, sizeof(material.core.pbrSG.specularFactor));
                    readRaw(&material.core.pbrSG.glossinessFactor, sizeof(material.core.pbrSG.glossinessFactor));
                    readTextureRef(material.core.pbrSG.diffuseTexture);
                    readTextureRef(material.core.pbrSG.specularGlossinessTexture);
                    readTextureRef(material.core.pbrSG.glossinessTexture);
                    readTextureRef(material.core.pbrSG.normalTexture);
                    break;
                }
                case VMaterialModel::ePhong: {
                    material.core.phong = {};
                    readRaw(&material.core.phong.diffuse, sizeof(material.core.phong.diffuse));
                    readRaw(&material.core.phong.specular, sizeof(material.core.phong.specular));
                    readRaw(&material.core.phong.shininess, sizeof(material.core.phong.shininess));
                    readRaw(&material.core.phong.opacity, sizeof(material.core.phong.opacity));
                    readRaw(&material.core.phong.ior, sizeof(material.core.phong.ior));
                    readRaw(&material.core.phong.emissive, sizeof(material.core.phong.emissive));
                    readTextureRef(material.core.phong.diffuseTexture);
                    readTextureRef(material.core.phong.specularTexture);
                    readTextureRef(material.core.phong.normalTexture);
                    readTextureRef(material.core.phong.opacityTexture);
                    readTextureRef(material.core.phong.emissiveTexture);
                    break;
                }
                case VMaterialModel::ePBRMetallicRoughness:
                case VMaterialModel::eUnknown:
                case VMaterialModel::eCustom:
                default: {
                    material.core.pbrMR = {};
                    auto& pbr           = material.core.pbrMR;

                    readRaw(&pbr.baseColor, sizeof(pbr.baseColor));
                    readRaw(&pbr.metallicFactor, sizeof(pbr.metallicFactor));
                    readRaw(&pbr.roughnessFactor, sizeof(pbr.roughnessFactor));

                    readRaw(&pbr.alphaCutoff, sizeof(pbr.alphaCutoff));
                    readRaw(&pbr.alphaMode, sizeof(pbr.alphaMode));
                    readRaw(&pbr.opacity, sizeof(pbr.opacity));
                    readRaw(&pbr.blendMode, sizeof(pbr.blendMode));

                    readRaw(&pbr.emissiveColorIntensity, sizeof(pbr.emissiveColorIntensity));
                    readRaw(&pbr.ambientColor, sizeof(pbr.ambientColor));
                    readRaw(&pbr.ior, sizeof(pbr.ior));
                    readRaw(&pbr.doubleSided, sizeof(pbr.doubleSided));

                    readTextureRef(pbr.baseColorTexture);
                    readTextureRef(pbr.alphaTexture);
                    readTextureRef(pbr.metallicTexture);
                    readTextureRef(pbr.roughnessTexture);
                    readTextureRef(pbr.metallicRoughnessTexture);
                    readTextureRef(pbr.specularTexture);
                    readTextureRef(pbr.normalTexture);
                    readTextureRef(pbr.ambientOcclusionTexture);
                    readTextureRef(pbr.emissiveTexture);
                    break;
                }
            }

            // ---- texture bindings (lossless) ----
            uint32_t texCount = 0;
            readRaw(&texCount, sizeof(texCount));
            material.textures.clear();
            material.textures.reserve(texCount);
            for (uint32_t ti = 0; ti < texCount; ++ti)
            {
                VMaterialTextureBinding tb;
                readRaw(&tb.type, sizeof(tb.type));
                readRaw(&tb.index, sizeof(tb.index));
                readRaw(&tb.uvIndex, sizeof(tb.uvIndex));
                readRaw(&tb.mapping, sizeof(tb.mapping));
                readRaw(&tb.op, sizeof(tb.op));
                readRaw(&tb.mapModeU, sizeof(tb.mapModeU));
                readRaw(&tb.mapModeV, sizeof(tb.mapModeV));

                readRaw(&tb.blend, sizeof(tb.blend));
                readTextureRef(tb.texture);
                material.textures.push_back(tb);
            }

            // ---- dynamic properties ----
            uint32_t propCount = 0;
            readRaw(&propCount, sizeof(propCount));
            material.properties.clear();
            material.properties.reserve(propCount);
            for (uint32_t i = 0; i < propCount; ++i)
            {
                VMaterialProperty prop;
                prop.key = readString();
                readRaw(&prop.semantic, sizeof(prop.semantic));
                readRaw(&prop.index, sizeof(prop.index));
                readRaw(&prop.type, sizeof(prop.type));
                prop.data = readBytes();
                material.properties.push_back(std::move(prop));
            }
        }

        // mesh name
        uint32_t nameLength = 0;
        readRaw(&nameLength, sizeof(nameLength));

        outMesh.name.resize(nameLength);
        readRaw(outMesh.name.data(), nameLength);

        outMesh.hasDefaultTransform = false;
        outMesh.defaultPosition     = glm::vec3 {0.0f};
        outMesh.defaultRotation     = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};
        outMesh.defaultScale        = glm::vec3 {1.0f};
        outMesh.hasLocalBounds      = false;
        outMesh.localBoundsMin      = glm::vec3 {0.0f};
        outMesh.localBoundsMax      = glm::vec3 {0.0f};
        outMesh.hasSkin             = false;
        outMesh.skeleton            = {};
        outMesh.skeletonPath.clear();
        outMesh.jointNames.clear();
        outMesh.jointParents.clear();
        outMesh.inverseBindPoses.clear();

        if (rawOffset + 16 + sizeof(uint32_t) <= raw.size())
        {
            char metaMagic[16] {};
            const size_t metaOffset = rawOffset;
            if (readRaw(metaMagic, sizeof(metaMagic)) && std::string(metaMagic) == "VMESH_META1")
            {
                uint32_t metaFlags = 0;
                if (readRaw(&metaFlags, sizeof(metaFlags)))
                {
                    outMesh.hasDefaultTransform = (metaFlags & kMetaHasDefaultTransform) != 0;
                    readRaw(&outMesh.defaultPosition, sizeof(outMesh.defaultPosition));
                    readRaw(&outMesh.defaultRotation, sizeof(outMesh.defaultRotation));
                    readRaw(&outMesh.defaultScale, sizeof(outMesh.defaultScale));
                    if (glm::dot(outMesh.defaultRotation, outMesh.defaultRotation) > 1e-12f)
                        outMesh.defaultRotation = glm::normalize(outMesh.defaultRotation);
                    else
                        outMesh.defaultRotation = glm::quat {1.0f, 0.0f, 0.0f, 0.0f};

                    if ((metaFlags & kMetaHasLocalBounds) != 0u &&
                        rawOffset + sizeof(outMesh.localBoundsMin) + sizeof(outMesh.localBoundsMax) <= raw.size())
                    {
                        if (readRaw(&outMesh.localBoundsMin, sizeof(outMesh.localBoundsMin)) &&
                            readRaw(&outMesh.localBoundsMax, sizeof(outMesh.localBoundsMax)))
                        {
                            outMesh.hasLocalBounds = true;
                        }
                    }
                }
            }
            else
            {
                rawOffset = metaOffset;
            }
        }

        if (rawOffset + 16 + sizeof(outMesh.skeleton) <= raw.size())
        {
            const size_t skinOffset = rawOffset;
            char         skinMagic[16] {};
            auto readStringTail = [&]() -> std::string {
                uint32_t len = 0;
                if (!readRaw(&len, sizeof(len)) || rawOffset + len > raw.size())
                    return {};
                std::string out;
                out.resize(len);
                if (len)
                    readRaw(out.data(), len);
                return out;
            };

            if (readRaw(skinMagic, sizeof(skinMagic)) && std::string(skinMagic) == "VMESH_SKIN1")
            {
                outMesh.hasSkin = true;
                if (!readRaw(&outMesh.skeleton, sizeof(outMesh.skeleton)))
                    return vbase::Result<void, AssetError>::err(AssetError::eIOError);
                outMesh.skeletonPath = readStringTail();

                uint32_t jointCount = 0;
                if (!readRaw(&jointCount, sizeof(jointCount)))
                    return vbase::Result<void, AssetError>::err(AssetError::eIOError);
                outMesh.jointNames.resize(jointCount);
                outMesh.jointParents.resize(jointCount);
                for (uint32_t i = 0; i < jointCount; ++i)
                {
                    outMesh.jointNames[i] = readStringTail();
                    if (!readRaw(&outMesh.jointParents[i], sizeof(outMesh.jointParents[i])))
                        return vbase::Result<void, AssetError>::err(AssetError::eIOError);
                }

                uint32_t inverseBindPoseCount = 0;
                if (!readRaw(&inverseBindPoseCount, sizeof(inverseBindPoseCount)))
                    return vbase::Result<void, AssetError>::err(AssetError::eIOError);
                outMesh.inverseBindPoses.resize(inverseBindPoseCount);
                for (auto& inverseBindPose : outMesh.inverseBindPoses)
                {
                    if (!readRaw(&inverseBindPose, sizeof(inverseBindPose)))
                        return vbase::Result<void, AssetError>::err(AssetError::eIOError);
                }
            }
            else
            {
                rawOffset = skinOffset;
            }
        }

        if (!outMesh.hasLocalBounds)
            outMesh.hasLocalBounds =
                computeLocalBounds(outMesh.positions, outMesh.localBoundsMin, outMesh.localBoundsMax);

        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset
