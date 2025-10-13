#pragma once

#include <glm/glm.hpp>

namespace vasset
{
    // Flags to indicate which vertex attributes are present
    enum class VVertexFlags : uint32_t
    {
        eNone         = 0,
        ePosition     = 1 << 0,
        eNormal       = 1 << 1,
        eColor        = 1 << 2,
        eTexCoord0    = 1 << 3,
        eTexCoord1    = 1 << 4,
        eTangent      = 1 << 5,
        eJointIndices = 1 << 6,
        eJointWeights = 1 << 7,

        eGeneral = ePosition | eNormal | eTexCoord0 | eTangent,
        eAll     = ePosition | eNormal | eColor | eTexCoord0 | eTexCoord1 | eTangent | eJointIndices | eJointWeights
    };
    inline VVertexFlags operator|(VVertexFlags a, VVertexFlags b)
    {
        return static_cast<VVertexFlags>(static_cast<int>(a) | static_cast<int>(b));
    }
    inline VVertexFlags& operator|=(VVertexFlags& a, VVertexFlags b)
    {
        a = a | b;
        return a;
    }
    inline bool operator&(VVertexFlags a, VVertexFlags b) { return (static_cast<int>(a) & static_cast<int>(b)) != 0; }
    inline VVertexFlags  operator&(VVertexFlags a, int b) { return static_cast<VVertexFlags>(static_cast<int>(a) & b); }
    inline VVertexFlags& operator&=(VVertexFlags& a, VVertexFlags b)
    {
        a = a & static_cast<int>(b);
        return a;
    }
    inline VVertexFlags operator~(VVertexFlags a) { return static_cast<VVertexFlags>(~static_cast<int>(a)); }

    using VPosition     = glm::vec3;
    using VNormal       = glm::vec3;
    using VColor        = glm::vec3;
    using VTexCoord     = glm::vec2;
    using VTangent      = glm::vec4; // packed tangent
    using VJointIndices = glm::vec4;
    using VJointWeights = glm::vec4;

    // Vertex structure used in meshes
    struct VVertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec3 color;
        glm::vec2 texCoord0;
        glm::vec2 texCoord1;
        glm::vec4 tangent; // packed tangent
        glm::vec4 jointIndices;
        glm::vec4 jointWeights;

        VVertex() :
            position(0.0f), normal(0.0f), color(1.0f), texCoord0(0.0f), texCoord1(0.0f), tangent(0.0f),
            jointIndices(0.0f), jointWeights(0.0f)
        {}
    };
} // namespace vasset