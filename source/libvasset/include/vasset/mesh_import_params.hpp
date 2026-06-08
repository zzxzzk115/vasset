#pragma once

#include "vasset/vasset_importers.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace vasset
{
    // Flat .vimport param keys for mesh import options. Stable strings for merge friendliness.
    constexpr std::string_view kMeshParamCalcTangentSpace {"mesh.calc_tangent_space"};
    constexpr std::string_view kMeshParamGenSmoothNormals {"mesh.gen_smooth_normals"};
    constexpr std::string_view kMeshParamGenUVCoords {"mesh.gen_uv_coords"};
    constexpr std::string_view kMeshParamFlipUVs {"mesh.flip_uvs"};
    constexpr std::string_view kMeshParamPreTransformVertices {"mesh.pre_transform_vertices"};
    constexpr std::string_view kMeshParamGenerateMeshlets {"mesh.generate_meshlets"};
    constexpr std::string_view kMeshParamOptimizeVertexCache {"mesh.optimize_vertex_cache"};
    constexpr std::string_view kMeshParamOptimizeOverdraw {"mesh.optimize_overdraw"};
    constexpr std::string_view kMeshParamOptimizeVertexFetch {"mesh.optimize_vertex_fetch"};

    // Resolve stored .vimport params (sparse) into full options, starting from `defaults` and
    // overriding only the keys present. Absent keys keep their default value.
    [[nodiscard]] VMeshImporter::ImportOptions
    resolveMeshImportParams(const std::unordered_map<std::string, std::string>& params,
                            const VMeshImporter::ImportOptions&                  defaults = {});

    // Serialize options back to the param map, writing ONLY fields that differ from the struct
    // defaults (so an all-default mesh writes a near-empty [params]). Erases known keys first.
    [[nodiscard]] std::unordered_map<std::string, std::string>
    normalizedMeshImportParams(std::unordered_map<std::string, std::string> existing,
                               const VMeshImporter::ImportOptions&          options);
} // namespace vasset
