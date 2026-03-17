#pragma once

#include "vasset/asset_error.hpp"

#include <vbase/core/result.hpp>
#include <vbase/core/string_view.hpp>
#include <vbase/core/uuid.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace vasset
{
    // Per-splat data -- GPU-uploadable as a structured / storage buffer.
    // Exactly 64 bytes, 16-byte aligned so it can be uploaded verbatim.
    struct alignas(16) VGaussianSplatPoint
    {
        glm::vec3 position;      // world-space center of the Gaussian
        float     opacity;       // raw logit -- renderer applies sigmoid(x) to get [0,1]

        glm::vec3 scale;         // log-space per-axis scale -- renderer applies exp(x)
        float     pad0 {0.0f};

        glm::vec4 rotation;      // unit quaternion, xyzw storage order

        glm::vec3 shDC;          // SH DC color component (raw) -- 0.5 + 0.282095 * x = linear RGB
        float     pad1 {0.0f};
    };
    static_assert(sizeof(VGaussianSplatPoint) == 64);
    static_assert(sizeof(VGaussianSplatPoint) % 16 == 0);

    // Cloud-level Gaussian Splat asset.
    struct VGaussianSplat
    {
        vbase::UUID uuid;

        std::string name;
        std::string sourceFileName; // not serialized

        int32_t numPoints   {0};
        int32_t shDegree    {0};   // 0, 1, 2, or 3
        bool    antialiased {false};

        // Per-splat data -- size == numPoints.
        std::vector<VGaussianSplatPoint> splats;

        // Higher-order spherical harmonics coefficients (omitted when shDegree == 0).
        // Layout: for each point, for each coefficient, 3 RGB floats -- fastest-varying axis is channel.
        // Total size: numPoints * shCoeffsPerPoint(shDegree) * 3
        //   degree 1 ->  9 floats/point, degree 2 -> 24, degree 3 -> 45
        std::vector<float> sh;
    };

    vbase::Result<void, AssetError>
    saveGaussianSplat(const VGaussianSplat& splat, vbase::StringView filePath, int zstdLevel = 3);
    vbase::Result<void, AssetError> loadGaussianSplat(vbase::StringView filePath, VGaussianSplat& outSplat);
    vbase::Result<void, AssetError> loadGaussianSplatFromMemory(const std::vector<std::byte>& data,
                                                                VGaussianSplat&               outSplat);
} // namespace vasset