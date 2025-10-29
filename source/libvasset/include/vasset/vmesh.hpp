#pragma once

#include "vasset/vmaterial.hpp"
#include "vasset/vvertex.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vasset
{
    struct alignas(16) VMeshlet
    {
        uint32_t vertexOffset {0};
        uint32_t vertexCount {0};
        uint32_t triangleOffset {0};
        uint32_t triangleCount {0};

        uint32_t materialIndex {0};
        uint32_t paddingU0 {0}; // ensure 16-byte alignment
        uint32_t paddingU1 {0}; // ensure 16-byte alignment
        uint32_t paddingU2 {0}; // ensure 16-byte alignment

        glm::vec3 center;
        float     radius {0.0f};

        glm::vec3 coneAxis;
        float     coneCutoff {0.0f}; // cosine of the cone cutoff angle

        glm::vec3 coneApex;
        float     paddingF0; // ensure 16-byte alignment
    };
    static_assert(sizeof(VMeshlet) % 16 == 0);

    struct VMeshletGroup
    {
        std::vector<VMeshlet> meshlets;
        std::vector<uint32_t> meshletVertices;
        std::vector<uint8_t>  meshletTriangles;
    };

    struct VSubMesh // -> aiMesh
    {
        uint32_t vertexOffset {0};
        uint32_t vertexCount {0};

        uint32_t indexOffset {0};
        uint32_t indexCount {0};

        uint32_t materialIndex {0};

        VMeshletGroup meshletGroup;

        std::string name;
    };

    struct VMesh // -> aiNode
    {
        VUUID uuid;

        uint32_t     vertexCount {0};
        VVertexFlags vertexFlags {VVertexFlags::eNone};

        std::vector<VPosition>     positions;
        std::vector<VNormal>       normals;
        std::vector<VColor>        colors;
        std::vector<VTexCoord>     texCoords0;
        std::vector<VTexCoord>     texCoords1;
        std::vector<VTangent>      tangents;
        std::vector<VJointIndices> jointIndices;
        std::vector<VJointWeights> jointWeights;

        std::vector<uint32_t> indices;

        std::vector<VSubMesh> subMeshes;

        std::vector<VMaterialRef> materials;

        std::string name;

        std::string sourceFileName; // Not serialized
    };

    struct VMeshMeta
    {
        VUUID       uuid;
        std::string extension; // original file extension
    };

    bool saveMesh(const VMesh& mesh, const std::string& filePath, const std::filesystem::path& srcFilePath);
    bool loadMesh(const std::string& filePath, VMesh& outMesh);
} // namespace vasset