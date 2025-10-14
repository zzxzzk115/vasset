#pragma once

#include "vasset/vmaterial.hpp"
#include "vasset/vvertex.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vasset
{
    struct VSubMesh // -> aiMesh
    {
        uint32_t vertexOffset {0};
        uint32_t vertexCount {0};

        uint32_t indexOffset {0};
        uint32_t indexCount {0};

        uint32_t materialIndex {0};

        std::string name;
    };

    struct VMesh // -> aiNode
    {
        VUUID uuid;

        uint32_t     vertexCount {0};
        VVertexFlags vertexFlags {VVertexFlags::eGeneral};

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
    };

    bool saveMesh(const VMesh& mesh, const std::string& filePath);
    bool loadMesh(const std::string& filePath, VMesh& outMesh);
} // namespace vasset