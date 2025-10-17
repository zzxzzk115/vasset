#include "vasset/vmesh.hpp"

#include <filesystem>
#include <fstream>

namespace vasset
{
    bool saveMesh(const VMesh& mesh, const std::string& filePath)
    {
        // Binary writing
        std::filesystem::path path(filePath);
        if (path.has_parent_path() && !std::filesystem::exists(path.parent_path()))
            std::filesystem::create_directories(path.parent_path());
        std::ofstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        // 16 bytes for magic number
        const char magic[16] = "VMESH\0";
        file.write(magic, sizeof(magic));

        // 16 bytes for UUID
        file.write(reinterpret_cast<const char*>(&mesh.uuid), sizeof(mesh.uuid));

        // 4 bytes for vertex count
        file.write(reinterpret_cast<const char*>(&mesh.vertexCount), sizeof(mesh.vertexCount));

        // 4 bytes for vertex flags
        file.write(reinterpret_cast<const char*>(&mesh.vertexFlags), sizeof(mesh.vertexFlags));

        // N vertices
        for (uint32_t i = 0; i < mesh.vertexCount; ++i)
        {
            if (mesh.vertexFlags & VVertexFlags::ePosition)
                file.write(reinterpret_cast<const char*>(&mesh.positions[i]), sizeof(VPosition));
            if (mesh.vertexFlags & VVertexFlags::eNormal)
                file.write(reinterpret_cast<const char*>(&mesh.normals[i]), sizeof(VNormal));
            if (mesh.vertexFlags & VVertexFlags::eColor)
                file.write(reinterpret_cast<const char*>(&mesh.colors[i]), sizeof(VColor));
            if (mesh.vertexFlags & VVertexFlags::eTexCoord0)
                file.write(reinterpret_cast<const char*>(&mesh.texCoords0[i]), sizeof(VTexCoord));
            if (mesh.vertexFlags & VVertexFlags::eTexCoord1)
                file.write(reinterpret_cast<const char*>(&mesh.texCoords1[i]), sizeof(VTexCoord));
            if (mesh.vertexFlags & VVertexFlags::eTangent)
                file.write(reinterpret_cast<const char*>(&mesh.tangents[i]), sizeof(VTangent));
            if (mesh.vertexFlags & VVertexFlags::eJointIndices)
                file.write(reinterpret_cast<const char*>(&mesh.jointIndices[i]), sizeof(VJointIndices));
            if (mesh.vertexFlags & VVertexFlags::eJointWeights)
                file.write(reinterpret_cast<const char*>(&mesh.jointWeights[i]), sizeof(VJointWeights));
        }

        // 4 bytes for number of indices
        uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
        file.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));

        // N indices
        file.write(reinterpret_cast<const char*>(mesh.indices.data()), indexCount * sizeof(uint32_t));

        // 4 bytes for number of sub-meshes
        uint32_t subMeshCount = static_cast<uint32_t>(mesh.subMeshes.size());
        file.write(reinterpret_cast<const char*>(&subMeshCount), sizeof(subMeshCount));

        // N sub-meshes
        for (const auto& subMesh : mesh.subMeshes)
        {
            // 4 bytes for vertex offset
            file.write(reinterpret_cast<const char*>(&subMesh.vertexOffset), sizeof(subMesh.vertexOffset));

            // 4 bytes for vertex count
            file.write(reinterpret_cast<const char*>(&subMesh.vertexCount), sizeof(subMesh.vertexCount));

            // 4 bytes for index offset
            file.write(reinterpret_cast<const char*>(&subMesh.indexOffset), sizeof(subMesh.indexOffset));

            // 4 bytes for index count
            file.write(reinterpret_cast<const char*>(&subMesh.indexCount), sizeof(subMesh.indexCount));

            // 4 bytes for material index
            file.write(reinterpret_cast<const char*>(&subMesh.materialIndex), sizeof(subMesh.materialIndex));

            // 4 bytes for name length
            uint32_t nameLength = static_cast<uint32_t>(subMesh.name.size());
            file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));

            // N bytes for name
            file.write(subMesh.name.c_str(), nameLength);
        }

        // 4 bytes for number of materials
        uint32_t materialCount = static_cast<uint32_t>(mesh.materials.size());
        file.write(reinterpret_cast<const char*>(&materialCount), sizeof(materialCount));

        // N materials
        for (const auto& material : mesh.materials)
        {
            // 16 bytes for material UUID
            file.write(reinterpret_cast<const char*>(&material.uuid), sizeof(material.uuid));
        }

        // 4 bytes for length of meshlets
        uint32_t meshletCount = static_cast<uint32_t>(mesh.meshletGroup.meshlets.size());
        file.write(reinterpret_cast<const char*>(&meshletCount), sizeof(uint32_t));

        // N meshlets
        for (const auto& meshlet : mesh.meshletGroup.meshlets)
        {
            file.write(reinterpret_cast<const char*>(&meshlet.vertexOffset), sizeof(meshlet.vertexOffset));
            file.write(reinterpret_cast<const char*>(&meshlet.vertexCount), sizeof(meshlet.vertexCount));
            file.write(reinterpret_cast<const char*>(&meshlet.triangleOffset), sizeof(meshlet.triangleOffset));
            file.write(reinterpret_cast<const char*>(&meshlet.triangleCount), sizeof(meshlet.triangleCount));
            file.write(reinterpret_cast<const char*>(&meshlet.materialIndex), sizeof(meshlet.materialIndex));
            file.write(reinterpret_cast<const char*>(&meshlet.center), sizeof(meshlet.center));
            file.write(reinterpret_cast<const char*>(&meshlet.radius), sizeof(meshlet.radius));
        }

        // 4 bytes for length of meshlet vertices
        uint32_t meshletVertexCount = static_cast<uint32_t>(mesh.meshletGroup.meshletVertices.size());
        file.write(reinterpret_cast<const char*>(&meshletVertexCount), sizeof(uint32_t));

        // N meshlet vertices
        for (const auto& vertex : mesh.meshletGroup.meshletVertices)
        {
            file.write(reinterpret_cast<const char*>(&vertex), sizeof(vertex));
        }

        // 4 bytes for length of meshlet triangles
        uint32_t meshletTriangleCount = static_cast<uint32_t>(mesh.meshletGroup.meshletTriangles.size());
        file.write(reinterpret_cast<const char*>(&meshletTriangleCount), sizeof(uint32_t));

        // N meshlet triangles
        for (const auto& triangle : mesh.meshletGroup.meshletTriangles)
        {
            file.write(reinterpret_cast<const char*>(&triangle), sizeof(triangle));
        }

        // name
        uint32_t nameLength = static_cast<uint32_t>(mesh.name.size());
        file.write(reinterpret_cast<const char*>(&nameLength), sizeof(nameLength));
        file.write(mesh.name.c_str(), nameLength);

        file.close();

        return true;
    }

    bool loadMesh(const std::string& filePath, VMesh& outMesh)
    {
        // Binary reading
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
            return false;

        // 16 bytes for magic number
        char magic[16];
        file.read(magic, sizeof(magic));
        if (std::string(magic) != "VMESH")
            return false;

        // 16 bytes for UUID
        file.read(reinterpret_cast<char*>(&outMesh.uuid), sizeof(outMesh.uuid));

        // 4 bytes for number of vertices
        file.read(reinterpret_cast<char*>(&outMesh.vertexCount), sizeof(outMesh.vertexCount));

        // 4 bytes for vertex flags
        file.read(reinterpret_cast<char*>(&outMesh.vertexFlags), sizeof(outMesh.vertexFlags));

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
                file.read(reinterpret_cast<char*>(&outMesh.positions[i]), sizeof(VPosition));
            if (outMesh.vertexFlags & VVertexFlags::eNormal)
                file.read(reinterpret_cast<char*>(&outMesh.normals[i]), sizeof(VNormal));
            if (outMesh.vertexFlags & VVertexFlags::eColor)
                file.read(reinterpret_cast<char*>(&outMesh.colors[i]), sizeof(VColor));
            if (outMesh.vertexFlags & VVertexFlags::eTexCoord0)
                file.read(reinterpret_cast<char*>(&outMesh.texCoords0[i]), sizeof(VTexCoord));
            if (outMesh.vertexFlags & VVertexFlags::eTexCoord1)
                file.read(reinterpret_cast<char*>(&outMesh.texCoords1[i]), sizeof(VTexCoord));
            if (outMesh.vertexFlags & VVertexFlags::eTangent)
                file.read(reinterpret_cast<char*>(&outMesh.tangents[i]), sizeof(VTangent));
            if (outMesh.vertexFlags & VVertexFlags::eJointIndices)
                file.read(reinterpret_cast<char*>(&outMesh.jointIndices[i]), sizeof(VJointIndices));
            if (outMesh.vertexFlags & VVertexFlags::eJointWeights)
                file.read(reinterpret_cast<char*>(&outMesh.jointWeights[i]), sizeof(VJointWeights));
        }

        // 4 bytes for number of indices
        uint32_t indexCount = 0;
        file.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));
        outMesh.indices.resize(indexCount);

        // N indices
        file.read(reinterpret_cast<char*>(outMesh.indices.data()), indexCount * sizeof(uint32_t));

        // 4 bytes for number of sub-meshes
        uint32_t subMeshCount = 0;
        file.read(reinterpret_cast<char*>(&subMeshCount), sizeof(subMeshCount));
        outMesh.subMeshes.resize(subMeshCount);

        // N sub-meshes
        for (auto& subMesh : outMesh.subMeshes)
        {
            // 4 bytes for vertex offset
            file.read(reinterpret_cast<char*>(&subMesh.vertexOffset), sizeof(subMesh.vertexOffset));

            // 4 bytes for vertex count
            file.read(reinterpret_cast<char*>(&subMesh.vertexCount), sizeof(subMesh.vertexCount));

            // 4 bytes for index offset
            file.read(reinterpret_cast<char*>(&subMesh.indexOffset), sizeof(subMesh.indexOffset));

            // 4 bytes for index count
            file.read(reinterpret_cast<char*>(&subMesh.indexCount), sizeof(subMesh.indexCount));

            // 4 bytes for material index
            file.read(reinterpret_cast<char*>(&subMesh.materialIndex), sizeof(subMesh.materialIndex));

            // 4 bytes for name length
            uint32_t nameLength = 0;
            file.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));

            subMesh.name.resize(nameLength);
            file.read(reinterpret_cast<char*>(subMesh.name.data()), nameLength);
        }

        // 4 bytes for number of materials
        uint32_t materialCount = 0;
        file.read(reinterpret_cast<char*>(&materialCount), sizeof(materialCount));
        outMesh.materials.resize(materialCount);

        // N materials
        for (auto& material : outMesh.materials)
        {
            // 16 bytes for material UUID
            file.read(reinterpret_cast<char*>(&material.uuid), sizeof(material.uuid));
        }

        // 4 bytes for length of meshlets
        uint32_t meshletCount = 0;
        file.read(reinterpret_cast<char*>(&meshletCount), sizeof(meshletCount));
        outMesh.meshletGroup.meshlets.resize(meshletCount);

        // N meshlets
        for (auto& meshlet : outMesh.meshletGroup.meshlets)
        {
            file.read(reinterpret_cast<char*>(&meshlet.vertexOffset), sizeof(meshlet.vertexOffset));
            file.read(reinterpret_cast<char*>(&meshlet.vertexCount), sizeof(meshlet.vertexCount));
            file.read(reinterpret_cast<char*>(&meshlet.triangleOffset), sizeof(meshlet.triangleOffset));
            file.read(reinterpret_cast<char*>(&meshlet.triangleCount), sizeof(meshlet.triangleCount));
            file.read(reinterpret_cast<char*>(&meshlet.materialIndex), sizeof(meshlet.materialIndex));
            file.read(reinterpret_cast<char*>(&meshlet.center), sizeof(meshlet.center));
            file.read(reinterpret_cast<char*>(&meshlet.radius), sizeof(meshlet.radius));
        }

        // 4 bytes for length of meshlet vertices
        uint32_t meshletVertexCount = 0;
        file.read(reinterpret_cast<char*>(&meshletVertexCount), sizeof(meshletVertexCount));
        outMesh.meshletGroup.meshletVertices.resize(meshletVertexCount);

        // N meshlet vertices
        for (auto& vertex : outMesh.meshletGroup.meshletVertices)
        {
            file.read(reinterpret_cast<char*>(&vertex), sizeof(vertex));
        }

        // 4 bytes for length of meshlet triangles
        uint32_t meshletTriangleCount = 0;
        file.read(reinterpret_cast<char*>(&meshletTriangleCount), sizeof(meshletTriangleCount));
        outMesh.meshletGroup.meshletTriangles.resize(meshletTriangleCount);

        // N meshlet triangles
        for (auto& triangle : outMesh.meshletGroup.meshletTriangles)
        {
            file.read(reinterpret_cast<char*>(&triangle), sizeof(triangle));
        }

        // name
        uint32_t nameLength = 0;
        file.read(reinterpret_cast<char*>(&nameLength), sizeof(nameLength));
        outMesh.name.resize(nameLength);
        file.read(reinterpret_cast<char*>(outMesh.name.data()), nameLength);

        file.close();

        return true;
    }
} // namespace vasset