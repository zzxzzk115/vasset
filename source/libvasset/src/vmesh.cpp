#include "vasset/vmesh.hpp"
#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>

#include <zstd.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace vasset
{
    struct VMeshFileHeader
    {
        char     magic[16]; // "VMESH"
        uint32_t version;   // 1
        uint32_t flags;     // bit0 = compressed
        uint64_t rawSize;   // uncompressed size
    };

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
        for (const auto& material : mesh.materials)
        {
            // 16 bytes for material UUID
            writeRaw(&material.uuid, sizeof(material.uuid));
        }

        // name
        uint32_t nameLength = static_cast<uint32_t>(mesh.name.size());
        writeRaw(&nameLength, sizeof(nameLength));
        writeRaw(mesh.name.c_str(), nameLength);

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
        struct FileGuard
        {
            std::ifstream file;
            ~FileGuard()
            {
                if (file.is_open())
                    file.close();
            }
        } fileGuard;

        fileGuard.file = std::ifstream(std::string(filePath), std::ios::binary);
        if (!fileGuard.file)
            return vbase::Result<void, AssetError>::err(AssetError::eNotFound);

        std::streamsize size = fileGuard.file.tellg();
        fileGuard.file.seekg(0, std::ios::beg);

        if (size <= 0)
            return vbase::Result<void, AssetError>::err(AssetError::eIOError);

        std::vector<std::byte> buffer(size);

        if (!fileGuard.file.read(reinterpret_cast<char*>(buffer.data()), size))
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
            readRaw(&material.uuid, sizeof(material.uuid));
        }

        // mesh name
        uint32_t nameLength = 0;
        readRaw(&nameLength, sizeof(nameLength));

        outMesh.name.resize(nameLength);
        readRaw(outMesh.name.data(), nameLength);

        return vbase::Result<void, AssetError>::ok();
    }
} // namespace vasset