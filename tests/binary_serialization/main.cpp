#include <vasset/vasset.hpp>

#include <gtest/gtest.h>

using namespace vasset;

TEST(MaterialSerialization, BasicSerialization)
{
    VMaterial material {};
    material.name                   = "Test Material";
    material.type                   = VMaterialType::ePBRMetallicRoughness;
    material.pbrMR.baseColor        = {1.0f, 0.0f, 0.0f, 1.0f};        // Red color
    material.pbrMR.baseColorTexture = VTextureRef {VUUID::generate()}; // Dummy texture reference
    material.pbrMR.metallicFactor   = 0.5f;

    // Serialize to binary
    bool result = saveMaterial(material, "test_material.vmat");
    ASSERT_TRUE(result);

    // Deserialize from binary
    VMaterial loadedMaterial {};
    result = loadMaterial("test_material.vmat", loadedMaterial);
    ASSERT_TRUE(result);

    // Verify the loaded material
    ASSERT_EQ(loadedMaterial.name, material.name);
    ASSERT_EQ(loadedMaterial.type, material.type);
    ASSERT_EQ(loadedMaterial.pbrMR.baseColor, material.pbrMR.baseColor);
    ASSERT_EQ(loadedMaterial.pbrMR.baseColorTexture.uuid, material.pbrMR.baseColorTexture.uuid);
    ASSERT_EQ(loadedMaterial.pbrMR.metallicFactor, material.pbrMR.metallicFactor);
}

TEST(MeshSerialization, BasicSerialization)
{
    VMesh mesh {};
    mesh.name        = "Test Mesh";
    mesh.uuid        = VUUID::generate();
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

    // Define a material reference
    VMaterialRef matRef {};
    matRef.uuid = VUUID::generate(); // Dummy material UUID
    mesh.materials.push_back(matRef);

    // Serialize to binary
    bool result = saveMesh(mesh, "test_mesh.vmesh");
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
    for (size_t i = 0; i < mesh.materials.size(); ++i)
    {
        ASSERT_EQ(loadedMesh.materials[i].uuid, mesh.materials[i].uuid);
    }
}

TEST(TextureSerialization, BasicSerialization)
{
    VTexture texture {};
    texture.uuid       = VUUID::generate();
    texture.width      = 256;
    texture.height     = 256;
    texture.format     = VTextureFormat::eRGBA8;
    texture.fileFormat = VTextureFileFormat::ePNG;

    // Fill with dummy data
    texture.data.resize(texture.width * texture.height * 4, 255);

    // Serialize to binary
    bool result = saveTexture(texture, "test_texture.vtex");
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
    ASSERT_EQ(loadedTexture.data, texture.data);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}