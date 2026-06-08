#include "vasset/mesh_import_params.hpp"

#include <array>

namespace vasset
{
    namespace
    {
        bool parseBool(std::string_view v, bool fallback)
        {
            if (v == "true" || v == "1")
                return true;
            if (v == "false" || v == "0")
                return false;
            return fallback;
        }

        std::string boolParam(bool v) { return v ? "true" : "false"; }

        const std::string* findParam(const std::unordered_map<std::string, std::string>& params, std::string_view key)
        {
            const auto it = params.find(std::string(key));
            return it != params.end() ? &it->second : nullptr;
        }
    } // namespace

    VMeshImporter::ImportOptions resolveMeshImportParams(const std::unordered_map<std::string, std::string>& params,
                                                         const VMeshImporter::ImportOptions& defaults)
    {
        VMeshImporter::ImportOptions out = defaults;
        if (const auto* v = findParam(params, kMeshParamCalcTangentSpace))
            out.calcTangentSpace = parseBool(*v, out.calcTangentSpace);
        if (const auto* v = findParam(params, kMeshParamGenSmoothNormals))
            out.genSmoothNormals = parseBool(*v, out.genSmoothNormals);
        if (const auto* v = findParam(params, kMeshParamGenUVCoords))
            out.genUVCoords = parseBool(*v, out.genUVCoords);
        if (const auto* v = findParam(params, kMeshParamFlipUVs))
            out.flipUVs = parseBool(*v, out.flipUVs);
        if (const auto* v = findParam(params, kMeshParamPreTransformVertices))
            out.preTransformVertices = parseBool(*v, out.preTransformVertices);
        if (const auto* v = findParam(params, kMeshParamGenerateMeshlets))
            out.generateMeshlets = parseBool(*v, out.generateMeshlets);
        if (const auto* v = findParam(params, kMeshParamOptimizeVertexCache))
            out.optimizeVertexCache = parseBool(*v, out.optimizeVertexCache);
        if (const auto* v = findParam(params, kMeshParamOptimizeOverdraw))
            out.optimizeOverdraw = parseBool(*v, out.optimizeOverdraw);
        if (const auto* v = findParam(params, kMeshParamOptimizeVertexFetch))
            out.optimizeVertexFetch = parseBool(*v, out.optimizeVertexFetch);
        return out;
    }

    std::unordered_map<std::string, std::string>
    normalizedMeshImportParams(std::unordered_map<std::string, std::string> existing,
                               const VMeshImporter::ImportOptions&          options)
    {
        constexpr std::array<std::string_view, 9> kKeys {
            kMeshParamCalcTangentSpace, kMeshParamGenSmoothNormals,    kMeshParamGenUVCoords,
            kMeshParamFlipUVs,          kMeshParamPreTransformVertices, kMeshParamGenerateMeshlets,
            kMeshParamOptimizeVertexCache, kMeshParamOptimizeOverdraw, kMeshParamOptimizeVertexFetch};
        for (const auto key : kKeys)
            existing.erase(std::string(key));

        const VMeshImporter::ImportOptions def {};
        const auto setBool = [&](std::string_view key, bool v, bool d) {
            if (v != d)
                existing[std::string(key)] = boolParam(v);
        };
        setBool(kMeshParamCalcTangentSpace, options.calcTangentSpace, def.calcTangentSpace);
        setBool(kMeshParamGenSmoothNormals, options.genSmoothNormals, def.genSmoothNormals);
        setBool(kMeshParamGenUVCoords, options.genUVCoords, def.genUVCoords);
        setBool(kMeshParamFlipUVs, options.flipUVs, def.flipUVs);
        setBool(kMeshParamPreTransformVertices, options.preTransformVertices, def.preTransformVertices);
        setBool(kMeshParamGenerateMeshlets, options.generateMeshlets, def.generateMeshlets);
        setBool(kMeshParamOptimizeVertexCache, options.optimizeVertexCache, def.optimizeVertexCache);
        setBool(kMeshParamOptimizeOverdraw, options.optimizeOverdraw, def.optimizeOverdraw);
        setBool(kMeshParamOptimizeVertexFetch, options.optimizeVertexFetch, def.optimizeVertexFetch);
        return existing;
    }
} // namespace vasset
