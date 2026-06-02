#include <vasset/vasset_import.hpp>
#include <vasset/vasset_runtime.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>

using namespace vasset;

namespace
{
    template<typename T>
    void writeScalar(std::ofstream& file, T value)
    {
        file.write(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    void writeTinyGaussianPlyWithLod(const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path, std::ios::binary);
        file << "ply\n";
        file << "format binary_little_endian 1.0\n";
        file << "element vertex 3\n";
        file << "property float x\n";
        file << "property float y\n";
        file << "property float z\n";
        file << "property float scale_0\n";
        file << "property float scale_1\n";
        file << "property float scale_2\n";
        file << "property float rot_0\n";
        file << "property float rot_1\n";
        file << "property float rot_2\n";
        file << "property float rot_3\n";
        file << "property float opacity\n";
        file << "property float f_dc_0\n";
        file << "property float f_dc_1\n";
        file << "property float f_dc_2\n";
        file << "property float importance\n";
        file << "property uint lod_level\n";
        file << "property uint cluster_id\n";
        file << "end_header\n";

        const float importance[] = {0.9f, 0.6f, 0.2f};
        const uint32_t lodLevel[] = {0u, 1u, 1u};
        const uint32_t clusterId[] = {42u, 43u, 43u};

        for (uint32_t i = 0; i < 3u; ++i)
        {
            writeScalar(file, static_cast<float>(i));
            writeScalar(file, 0.0f);
            writeScalar(file, 0.0f);
            writeScalar(file, -4.0f);
            writeScalar(file, -4.0f);
            writeScalar(file, -4.0f);
            writeScalar(file, 1.0f);
            writeScalar(file, 0.0f);
            writeScalar(file, 0.0f);
            writeScalar(file, 0.0f);
            writeScalar(file, 0.0f);
            writeScalar(file, 0.1f);
            writeScalar(file, 0.2f);
            writeScalar(file, 0.3f);
            writeScalar(file, importance[i]);
            writeScalar(file, lodLevel[i]);
            writeScalar(file, clusterId[i]);
        }
    }
} // namespace

TEST(MeshSerialization, BasicSerialization)
{
    VMesh mesh {};
    mesh.name        = "Test Mesh";
    mesh.uuid        = vbase::uuid_random();
    mesh.vertexCount = 3;
    mesh.vertexFlags = VVertexFlags::ePosition | VVertexFlags::eNormal;

    // Define vertices
    mesh.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    mesh.normals   = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};

    // Define indices
    mesh.indices = {0, 1, 2};

    // Define a sub-mesh
    VSubMesh subMesh {};
    subMesh.name          = "SubMesh1";
    subMesh.vertexOffset  = 0;
    subMesh.vertexCount   = 3;
    subMesh.indexOffset   = 0;
    subMesh.indexCount    = 3;
    subMesh.materialIndex = 0; // Assuming first material

    mesh.subMeshes.push_back(subMesh);

    // Define a material
    VMaterial mat {};
    mat.name = "test_material";
    mesh.materials.push_back(mat);

    // Serialize to binary
    auto result = saveMesh(mesh, "test_mesh.vmesh");
    ASSERT_TRUE(result);

    // Deserialize from binary
    VMesh loadedMesh {};
    result = loadMesh("test_mesh.vmesh", loadedMesh);
    ASSERT_TRUE(result);

    // Verify the loaded mesh
    ASSERT_EQ(loadedMesh.name, mesh.name);
    ASSERT_EQ(loadedMesh.vertexCount, mesh.vertexCount);
    ASSERT_EQ(loadedMesh.vertexFlags, mesh.vertexFlags);
    ASSERT_EQ(loadedMesh.positions, mesh.positions);
    ASSERT_EQ(loadedMesh.normals, mesh.normals);
    ASSERT_TRUE(loadedMesh.hasLocalBounds);
    EXPECT_FLOAT_EQ(loadedMesh.localBoundsMin.x, 0.0f);
    EXPECT_FLOAT_EQ(loadedMesh.localBoundsMin.y, 0.0f);
    EXPECT_FLOAT_EQ(loadedMesh.localBoundsMin.z, 0.0f);
    EXPECT_FLOAT_EQ(loadedMesh.localBoundsMax.x, 1.0f);
    EXPECT_FLOAT_EQ(loadedMesh.localBoundsMax.y, 1.0f);
    EXPECT_FLOAT_EQ(loadedMesh.localBoundsMax.z, 0.0f);
    ASSERT_EQ(loadedMesh.indices, mesh.indices);
    ASSERT_EQ(loadedMesh.subMeshes.size(), mesh.subMeshes.size());
    for (size_t i = 0; i < mesh.subMeshes.size(); ++i)
    {
        ASSERT_EQ(loadedMesh.subMeshes[i].name, mesh.subMeshes[i].name);
        ASSERT_EQ(loadedMesh.subMeshes[i].vertexOffset, mesh.subMeshes[i].vertexOffset);
        ASSERT_EQ(loadedMesh.subMeshes[i].vertexCount, mesh.subMeshes[i].vertexCount);
        ASSERT_EQ(loadedMesh.subMeshes[i].indexOffset, mesh.subMeshes[i].indexOffset);
        ASSERT_EQ(loadedMesh.subMeshes[i].indexCount, mesh.subMeshes[i].indexCount);
        ASSERT_EQ(loadedMesh.subMeshes[i].materialIndex, mesh.subMeshes[i].materialIndex);
    }
    ASSERT_EQ(loadedMesh.materials.size(), mesh.materials.size());
}

TEST(AssetRegistryDependencies, SaveLoadValidateAndQuery)
{
    VAssetRegistry registry {};
    registry.setAssetRootPath("resources");
    registry.setImportedFolderName("imported");

    const auto meshUuid    = vbase::uuid_from_string_key("mesh");
    const auto textureUuid = vbase::uuid_from_string_key("texture");
    const auto missingUuid = vbase::uuid_from_string_key("missing");

    ASSERT_TRUE(registry.registerAsset(meshUuid, "models/mesh.gltf#mesh/0", "imported/Mesh/mesh.vmesh", VAssetType::eMesh));
    ASSERT_TRUE(registry.registerAsset(textureUuid, "textures/albedo.png", "imported/Texture/albedo.vtexture", VAssetType::eTexture));

    registry.setDependencies(meshUuid,
                             {
                                 VAssetDependency {
                                     .kind       = VAssetDependencyKind::eMaterialTexture,
                                     .targetUuid = textureUuid,
                                     .targetPath = "textures/albedo.png",
                                     .context    = "material[0].baseColorTexture",
                                 },
                                 VAssetDependency {
                                     .kind       = VAssetDependencyKind::eMaterialTexture,
                                     .targetUuid = missingUuid,
                                     .targetPath = "textures/missing.png",
                                     .context    = "material[0].normalTexture",
                                 },
                             });

    const auto path = std::filesystem::temp_directory_path() / "vasset_registry_dependencies.tsv";
    ASSERT_TRUE(registry.save(path.generic_string()));

    VAssetRegistry loaded {};
    ASSERT_TRUE(loaded.load(path.generic_string()));
    std::filesystem::remove(path);

    const auto deps = loaded.dependencies(meshUuid);
    ASSERT_EQ(deps.size(), 2u);
    EXPECT_EQ(deps[0].kind, VAssetDependencyKind::eMaterialTexture);
    EXPECT_EQ(deps[0].context, "material[0].baseColorTexture");

    const auto dependents = loaded.dependents(textureUuid);
    ASSERT_EQ(dependents.size(), 1u);
    EXPECT_EQ(dependents[0].ownerUuid, meshUuid);
    EXPECT_EQ(dependents[0].dependency.targetPath, "textures/albedo.png");

    const auto validation = loaded.validateDependencies();
    ASSERT_FALSE(validation.ok());
    ASSERT_EQ(validation.issues.size(), 1u);
    EXPECT_EQ(validation.issues[0].kind, VAssetDependencyIssue::Kind::eMissingTarget);
    EXPECT_EQ(validation.issues[0].dependency.targetPath, "textures/missing.png");
}

TEST(AssetRegistryDependencies, DetectsSimpleCycles)
{
    VAssetRegistry registry {};

    const auto a = vbase::uuid_from_string_key("cycle-a");
    const auto b = vbase::uuid_from_string_key("cycle-b");

    ASSERT_TRUE(registry.registerAsset(a, "a.asset", "imported/Mesh/a.vmesh", VAssetType::eMesh));
    ASSERT_TRUE(registry.registerAsset(b, "b.asset", "imported/Mesh/b.vmesh", VAssetType::eMesh));

    registry.setDependencies(a, {VAssetDependency {.kind = VAssetDependencyKind::eRuntimePayload, .targetUuid = b}});
    registry.setDependencies(b, {VAssetDependency {.kind = VAssetDependencyKind::eRuntimePayload, .targetUuid = a}});

    const auto validation = registry.validateDependencies();
    ASSERT_FALSE(validation.ok());

    const auto cycleIt = std::find_if(validation.issues.begin(), validation.issues.end(), [](const auto& issue) {
        return issue.kind == VAssetDependencyIssue::Kind::eCycle;
    });
    ASSERT_NE(cycleIt, validation.issues.end());
    EXPECT_GE(cycleIt->cycle.size(), 3u);
}

TEST(AssetPack, RootClosureExcludesUnusedAssets)
{
    namespace fs = std::filesystem;

    const fs::path root = fs::temp_directory_path() / "vasset_pack_root_closure";
    fs::remove_all(root);
    fs::create_directories(root / "scenes");
    fs::create_directories(root / "scripts");
    fs::create_directories(root / "imported");

    {
        std::ofstream(root / "scenes" / "main.vscn") << "ScriptComponent/scriptUri = \"res://scripts/used.lua\"\n";
        std::ofstream(root / "scripts" / "used.lua") << "return { used = true }\n";
        std::ofstream(root / "scripts" / "unused.lua") << "return { used = false }\n";
    }

    VAssetRegistry registry {};
    registry.setAssetRootPath(root.generic_string());
    registry.setImportedFolderName("imported");

    const auto sceneUuid = vbase::uuid_from_string_key("imported/Scene/main");
    const auto usedUuid = vbase::uuid_from_string_key("imported/ScriptLua/used");
    const auto unusedUuid = vbase::uuid_from_string_key("imported/ScriptLua/unused");

    ASSERT_TRUE(registry.registerAsset(sceneUuid, "scenes/main.vscn", "imported/Scene/main", VAssetType::eScene));
    ASSERT_TRUE(registry.registerAsset(usedUuid, "scripts/used.lua", "imported/ScriptLua/used", VAssetType::eScriptLua));
    ASSERT_TRUE(
        registry.registerAsset(unusedUuid, "scripts/unused.lua", "imported/ScriptLua/unused", VAssetType::eScriptLua));
    registry.setDependencies(sceneUuid,
                             {VAssetDependency {
                                 .kind       = VAssetDependencyKind::eSceneComponent,
                                 .targetUuid = usedUuid,
                                 .targetPath = "scripts/used.lua",
                                 .context    = "script",
                             }});
    ASSERT_TRUE(registry.save((root / "imported" / "asset_registry.tsv").generic_string()));

    const auto outVpk = root / "resources.vpk";
    VpkPackOptions options;
    options.rootPaths = {"res://scenes/main.vscn"};
    auto packResult = packAssetFolderToVpk(root.generic_string(), outVpk.generic_string(), options);
    ASSERT_TRUE(packResult);

    auto opened = openVpk(outVpk.generic_string());
    ASSERT_TRUE(opened);
    EXPECT_TRUE(readVpkFile(opened.value(), outVpk.generic_string(), "scenes/main.vscn"));
    EXPECT_TRUE(readVpkFile(opened.value(), outVpk.generic_string(), "scripts/used.lua"));
    EXPECT_FALSE(readVpkFile(opened.value(), outVpk.generic_string(), "scripts/unused.lua"));

    fs::remove_all(root);
}

TEST(MeshSerialization, SkinMetadataRoundTrip)
{
    VMesh mesh {};
    mesh.name        = "Skinned Mesh";
    mesh.uuid        = vbase::uuid_random();
    mesh.vertexCount = 2;
    mesh.vertexFlags =
        VVertexFlags::ePosition | VVertexFlags::eJointIndices | VVertexFlags::eJointWeights;
    mesh.positions = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    mesh.jointIndices = {glm::ivec4(0, 1, 2, 3), glm::ivec4(3, 2, 1, 0)};
    mesh.jointWeights = {glm::vec4(0.4f, 0.3f, 0.2f, 0.1f), glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)};
    mesh.indices = {0, 1};

    VSubMesh subMesh {};
    subMesh.vertexOffset = 0;
    subMesh.vertexCount = 2;
    subMesh.indexOffset = 0;
    subMesh.indexCount = 2;
    mesh.subMeshes.push_back(subMesh);

    mesh.hasSkin = true;
    mesh.skeleton = vbase::uuid_random();
    mesh.skeletonPath = "characters/hero.fbx#skeleton";
    mesh.jointNames = {"root", "spine"};
    mesh.jointParents = {-1, 0};
    mesh.inverseBindPoses = {glm::mat4(1.0f), glm::mat4(2.0f)};

    auto result = saveMesh(mesh, "test_skinned_mesh.vmesh");
    ASSERT_TRUE(result);

    VMesh loaded {};
    result = loadMesh("test_skinned_mesh.vmesh", loaded);
    ASSERT_TRUE(result);

    ASSERT_TRUE(loaded.hasSkin);
    ASSERT_EQ(loaded.skeleton, mesh.skeleton);
    ASSERT_EQ(loaded.skeletonPath, mesh.skeletonPath);
    ASSERT_EQ(loaded.jointNames, mesh.jointNames);
    ASSERT_EQ(loaded.jointParents, mesh.jointParents);
    ASSERT_EQ(loaded.jointIndices, mesh.jointIndices);
    ASSERT_EQ(loaded.jointWeights, mesh.jointWeights);
    ASSERT_EQ(loaded.inverseBindPoses.size(), mesh.inverseBindPoses.size());
    EXPECT_FLOAT_EQ(loaded.inverseBindPoses[1][0][0], 2.0f);
}

TEST(AnimationSerialization, SkeletonAndAnimationRoundTrip)
{
    VSkeleton skeleton {};
    skeleton.uuid = vbase::uuid_random();
    skeleton.name = "Hero Skeleton";
    skeleton.jointNames = {"root", "hand"};
    skeleton.jointParents = {-1, 0};
    skeleton.ozzData = {std::byte {1}, std::byte {2}, std::byte {3}};

    auto result = saveSkeleton(skeleton, "test_skeleton.vskel");
    ASSERT_TRUE(result);

    VSkeleton loadedSkeleton {};
    result = loadSkeleton("test_skeleton.vskel", loadedSkeleton);
    ASSERT_TRUE(result);
    ASSERT_EQ(loadedSkeleton.uuid, skeleton.uuid);
    ASSERT_EQ(loadedSkeleton.name, skeleton.name);
    ASSERT_EQ(loadedSkeleton.jointNames, skeleton.jointNames);
    ASSERT_EQ(loadedSkeleton.jointParents, skeleton.jointParents);
    ASSERT_EQ(loadedSkeleton.ozzData, skeleton.ozzData);

    VAnimation animation {};
    animation.uuid = vbase::uuid_random();
    animation.name = "Idle";
    animation.duration = 1.5f;
    animation.ozzData = {std::byte {4}, std::byte {5}};

    result = saveAnimation(animation, "test_animation.vanim");
    ASSERT_TRUE(result);

    VAnimation loadedAnimation {};
    result = loadAnimation("test_animation.vanim", loadedAnimation);
    ASSERT_TRUE(result);
    ASSERT_EQ(loadedAnimation.uuid, animation.uuid);
    ASSERT_EQ(loadedAnimation.name, animation.name);
    EXPECT_FLOAT_EQ(loadedAnimation.duration, animation.duration);
    ASSERT_EQ(loadedAnimation.ozzData, animation.ozzData);
}

TEST(TextureSerialization, BasicSerialization)
{
    VTexture texture {};
    texture.uuid       = vbase::uuid_random();
    texture.width      = 256;
    texture.height     = 256;
    texture.format     = VTextureFormat::eRGBA8;
    texture.fileFormat = VTextureFileFormat::ePNG;
    texture.compressedBasisU = true;

    // Fill with dummy data
    texture.data.resize(texture.width * texture.height * 4, 255);

    // Serialize to binary
    auto result = saveTexture(texture, "test_texture.vtex");
    ASSERT_TRUE(result);

    // Deserialize from binary
    VTexture loadedTexture {};
    result = loadTexture("test_texture.vtex", loadedTexture);
    ASSERT_TRUE(result);

    // Verify the loaded texture
    ASSERT_EQ(loadedTexture.uuid, texture.uuid);
    ASSERT_EQ(loadedTexture.width, texture.width);
    ASSERT_EQ(loadedTexture.height, texture.height);
    ASSERT_EQ(loadedTexture.format, texture.format);
    ASSERT_EQ(loadedTexture.fileFormat, texture.fileFormat);
    ASSERT_EQ(loadedTexture.compressedBasisU, texture.compressedBasisU);
    ASSERT_EQ(loadedTexture.data, texture.data);
}

TEST(GaussianSplatSerialization, LodSidecarRoundTrip)
{
    VGaussianSplat splat {};
    splat.uuid        = vbase::uuid_random();
    splat.name        = "Test Splat LOD";
    splat.numPoints   = 3;
    splat.shDegree    = 0;
    splat.antialiased = false;
    splat.splats.resize(3);

    for (int i = 0; i < splat.numPoints; ++i)
    {
        auto& point    = splat.splats[static_cast<size_t>(i)];
        point.position = glm::vec3(static_cast<float>(i), 1.0f, 2.0f);
        point.opacity  = 0.5f;
        point.scale    = glm::vec3(-4.0f);
        point.rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        point.shDC     = glm::vec3(0.1f, 0.2f, 0.3f);
    }

    splat.lod.type       = VGaussianSplatLodType::eFlatImportance;
    splat.lod.importance = {1.0f, 0.5f, 0.25f};
    splat.lod.lodLevel   = {0u, 1u, 1u};
    splat.lod.clusterId  = {7u, 8u, 8u};

    auto result = saveGaussianSplat(splat, "test_splat_lod.vgs", 0);
    ASSERT_TRUE(result);

    VGaussianSplat loaded {};
    result = loadGaussianSplat("test_splat_lod.vgs", loaded);
    ASSERT_TRUE(result);

    ASSERT_EQ(loaded.uuid, splat.uuid);
    ASSERT_EQ(loaded.name, splat.name);
    ASSERT_EQ(loaded.numPoints, splat.numPoints);
    ASSERT_EQ(loaded.splats.size(), splat.splats.size());
    ASSERT_EQ(loaded.lod.type, splat.lod.type);
    ASSERT_EQ(loaded.lod.importance, splat.lod.importance);
    ASSERT_EQ(loaded.lod.lodLevel, splat.lod.lodLevel);
    ASSERT_EQ(loaded.lod.clusterId, splat.lod.clusterId);
}

TEST(GaussianSplatImport, LodExtrasFromPly)
{
    namespace fs = std::filesystem;

    const fs::path root = fs::current_path() / "test_lod_asset_root";
    fs::remove_all(root);
    fs::create_directories(root / "models");

    const fs::path plyPath = root / "models" / "tiny_lod.ply";
    writeTinyGaussianPlyWithLod(plyPath);

    VAssetRegistry registry {};
    registry.setAssetRootPath(root.generic_string());
    registry.setImportedFolderName("imported");

    VGaussianSplatImporter importer {registry};
    VGaussianSplat         imported {};
    auto importResult = importer.importGaussianSplat(plyPath.generic_string(), imported, true);
    ASSERT_TRUE(importResult);

    ASSERT_EQ(imported.lod.type, VGaussianSplatLodType::eFlatImportance);
    ASSERT_EQ(imported.lod.importance, (std::vector<float> {0.9f, 0.6f, 0.2f}));
    ASSERT_EQ(imported.lod.lodLevel, (std::vector<uint32_t> {0u, 1u, 1u}));
    ASSERT_EQ(imported.lod.clusterId, (std::vector<uint32_t> {42u, 43u, 43u}));

    const auto entry = registry.lookup(importResult.value());
    ASSERT_EQ(entry.type, VAssetType::eGaussianSplat);

    VGaussianSplat loaded {};
    auto loadResult = loadGaussianSplat((root / entry.importedPath).generic_string(), loaded);
    ASSERT_TRUE(loadResult);
    ASSERT_EQ(loaded.lod.type, VGaussianSplatLodType::eFlatImportance);
    ASSERT_EQ(loaded.lod.importance, imported.lod.importance);
    ASSERT_EQ(loaded.lod.lodLevel, imported.lod.lodLevel);
    ASSERT_EQ(loaded.lod.clusterId, imported.lod.clusterId);

    fs::remove_all(root);
}

TEST(UUID, FilePath)
{
    std::string path  = "imported/textures/example_texture.vtex";
    auto        uuid1 = vbase::uuid_from_string_key(path);
    auto        uuid2 = vbase::uuid_from_string_key(path);
    ASSERT_EQ(uuid1, uuid2); // Same path should yield same UUID
    ASSERT_TRUE(uuid1.valid());
    std::string differentPath = "imported/textures/another_texture.vtex";
    auto        uuid3         = vbase::uuid_from_string_key(differentPath);
    ASSERT_NE(uuid1, uuid3); // Different paths should yield different UUIDs
    ASSERT_TRUE(uuid3.valid());
}

TEST(UUID, StringConversion)
{
    auto        originalUUID = vbase::uuid_random();
    std::string uuidStr      = vbase::to_string(originalUUID);
    vbase::UUID parsedUUID;
    vbase::try_parse_uuid(uuidStr.c_str(), parsedUUID);
    ASSERT_EQ(originalUUID, parsedUUID); // Conversion to string and back should yield same UUID
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
