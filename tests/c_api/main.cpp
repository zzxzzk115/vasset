// vasset C ABI smoke test: drives vasset_c_api.h end to end — the asset registry (CRUD, dependency
// graph, save/load round-trip), the UUID resolver, and a VPK pack/read round-trip — through the flat
// C functions, the same way an FFI consumer (e.g. VultraEngine) would. Also checks the import-half
// linkage. Validates the ABI without depending on heavy importer fixtures.
#include <vasset/vasset_c_api.h>
#include <vasset/vpk.hpp>

#include <vbase/core/uuid.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    std::string uuidHex(uint64_t hi, uint64_t lo) { return vbase::to_string(vbase::UUID {hi, lo}); }

    std::filesystem::path tempDir(const char* name)
    {
        auto dir = std::filesystem::temp_directory_path() / "vasset_c_api_test" / name;
        std::filesystem::create_directories(dir);
        return dir;
    }
} // namespace

TEST(VassetCApi, Version)
{
    EXPECT_GE(vasset_c_api_version(), 1u);
}

TEST(VassetCApi, TypeRoundTrip)
{
    EXPECT_STREQ(vasset_type_to_string(vasset_type_from_string("texture")), "texture");
    EXPECT_EQ(vasset_type_from_string("does-not-exist"), 0); // unknown
}

TEST(VassetCApi, RegistryCrudAndEnumeration)
{
    VAssetRegistryHandle reg = vasset_registry_create();
    ASSERT_NE(reg, nullptr);
    vasset_registry_set_root(reg, "assets");

    const std::string a = uuidHex(1, 2);
    const std::string b = uuidHex(3, 4);
    const int32_t     texture = vasset_type_from_string("texture");
    const int32_t     mesh    = vasset_type_from_string("mesh");

    EXPECT_EQ(vasset_registry_register(reg, a.c_str(), "tex/a.png", "imported/tex/a", texture), VASSET_OK);
    EXPECT_EQ(vasset_registry_register(reg, b.c_str(), "mesh/b.fbx", "imported/mesh/b", mesh), VASSET_OK);

    EXPECT_EQ(vasset_registry_contains(reg, a.c_str()), 1);
    EXPECT_EQ(vasset_registry_contains(reg, uuidHex(9, 9).c_str()), 0);
    EXPECT_EQ(std::string(vasset_registry_source_path(reg, a.c_str())), "tex/a.png");
    EXPECT_EQ(std::string(vasset_registry_imported_path(reg, a.c_str())), "imported/tex/a");
    EXPECT_EQ(vasset_registry_type(reg, b.c_str()), mesh);

    EXPECT_EQ(vasset_registry_count(reg), 2u);
    EXPECT_EQ(vasset_registry_update(reg, a.c_str(), "imported/tex/a2"), VASSET_OK);
    EXPECT_EQ(std::string(vasset_registry_imported_path(reg, a.c_str())), "imported/tex/a2");

    EXPECT_EQ(vasset_registry_unregister(reg, b.c_str()), VASSET_OK);
    EXPECT_EQ(vasset_registry_count(reg), 1u);
    EXPECT_LT(vasset_registry_unregister(reg, b.c_str()), 0); // already gone -> error

    vasset_registry_destroy(reg);
}

TEST(VassetCApi, DependencyGraphAndValidation)
{
    VAssetRegistryHandle reg = vasset_registry_create();
    const std::string    owner  = uuidHex(10, 1);
    const std::string    target = uuidHex(10, 2);
    const int32_t        tex     = vasset_type_from_string("texture");
    const int32_t        mat     = vasset_type_from_string("material");
    const int32_t        kindTex = 3; // VAssetDependencyKind::eMaterialTexture

    vasset_registry_register(reg, owner.c_str(), "mat/m.vmat", "imported/mat/m", mat);

    // Dependency on a target that isn't registered yet -> one validation issue.
    vasset_registry_add_dependency(reg, owner.c_str(), kindTex, target.c_str(), "tex/t.png", "albedo", 1);
    EXPECT_EQ(vasset_registry_dependency_count(reg, owner.c_str()), 1u);
    EXPECT_EQ(vasset_registry_dependency_kind_at(reg, 0), kindTex);
    EXPECT_EQ(std::string(vasset_registry_dependency_target_uuid_at(reg, 0)), target);
    EXPECT_EQ(std::string(vasset_registry_dependency_context_at(reg, 0)), "albedo");
    EXPECT_EQ(vasset_registry_dependency_required_at(reg, 0), 1);

    uint32_t issues = 0;
    EXPECT_EQ(vasset_registry_validate(reg, &issues), 0); // not ok
    EXPECT_EQ(issues, 1u);

    // Register the target -> dependency resolves, dependents reflect the edge, validation passes.
    vasset_registry_register(reg, target.c_str(), "tex/t.png", "imported/tex/t", tex);
    EXPECT_EQ(vasset_registry_dependent_count(reg, target.c_str()), 1u);
    EXPECT_EQ(std::string(vasset_registry_dependent_owner_at(reg, 0)), owner);
    EXPECT_EQ(vasset_registry_validate(reg, &issues), 1);
    EXPECT_EQ(issues, 0u);

    vasset_registry_clear_dependencies(reg, owner.c_str());
    EXPECT_EQ(vasset_registry_dependency_count(reg, owner.c_str()), 0u);

    vasset_registry_destroy(reg);
}

TEST(VassetCApi, RegistrySaveLoadRoundTrip)
{
    const auto         path = (tempDir("registry") / "asset_registry.tsv").generic_string();
    const std::string  a    = uuidHex(7, 7);

    VAssetRegistryHandle w = vasset_registry_create();
    vasset_registry_set_root(w, "assets");
    vasset_registry_register(w, a.c_str(), "tex/a.png", "imported/tex/a", vasset_type_from_string("texture"));
    ASSERT_EQ(vasset_registry_save(w, path.c_str()), VASSET_OK);
    vasset_registry_destroy(w);

    VAssetRegistryHandle r = vasset_registry_create();
    ASSERT_EQ(vasset_registry_load(r, path.c_str()), VASSET_OK);
    EXPECT_EQ(vasset_registry_contains(r, a.c_str()), 1);
    EXPECT_EQ(std::string(vasset_registry_source_path(r, a.c_str())), "tex/a.png");
    vasset_registry_destroy(r);
}

TEST(VassetCApi, ResolverRoundTrip)
{
    VAssetResolverHandle res = vasset_resolver_create();
    vasset_resolver_set_scheme(res, "res");

    const std::string id = uuidHex(5, 6);
    vasset_resolver_register(res, id.c_str(), "sprites/hero.png");

    const char* uri = vasset_resolver_resolve(res, id.c_str());
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(std::string(uri), "res://sprites/hero.png");

    const char* back = vasset_resolver_reverse(res, "res://sprites/hero.png");
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(std::string(back), id);

    EXPECT_EQ(vasset_resolver_resolve(res, uuidHex(0, 0).c_str()), nullptr); // unknown
    vasset_resolver_destroy(res);
}

TEST(VassetCApi, VpkReadRoundTrip)
{
    const auto vpkPath = (tempDir("vpk") / "pack.vpk").generic_string();

    // Build a VPK with one entry via the C++ writer, then read it back through the C ABI.
    const std::vector<std::byte> payload = {std::byte {'h'}, std::byte {'i'}, std::byte {'!'}};
    vasset::VpkWriteItem item;
    item.uuid          = vbase::UUID {8, 9};
    item.type          = vasset::VAssetType::eTexture;
    item.logicalPath   = "res://a.bin";
    item.bytes         = payload;
    item.allowCompress = false;
    ASSERT_TRUE(static_cast<bool>(vasset::writeVpk(vpkPath, {item}, 6)));

    VAssetVpkHandle vpk = vasset_vpk_open(vpkPath.c_str());
    ASSERT_NE(vpk, nullptr);
    EXPECT_GE(vasset_vpk_file_count(vpk), 1u);
    ASSERT_EQ(vasset_vpk_asset_count(vpk), 1u);
    EXPECT_EQ(std::string(vasset_vpk_asset_uuid_at(vpk, 0)), vbase::to_string(item.uuid));
    EXPECT_EQ(std::string(vasset_vpk_asset_path_at(vpk, 0)), "res://a.bin");
    EXPECT_EQ(vasset_vpk_asset_type_at(vpk, 0), static_cast<int32_t>(vasset::VAssetType::eTexture));

    VAssetBlob blob {};
    ASSERT_EQ(vasset_vpk_read(vpk, "res://a.bin", &blob), VASSET_OK);
    ASSERT_EQ(blob.size, payload.size());
    EXPECT_EQ(blob.data[0], 'h');
    EXPECT_EQ(blob.data[2], '!');
    vasset_blob_free(&blob);

    // Resolver populated from the VPK registry resolves the embedded uuid.
    VAssetResolverHandle res = vasset_resolver_create();
    EXPECT_EQ(vasset_resolver_load_from_vpk(res, vpk), VASSET_OK);
    const char* uri = vasset_resolver_resolve(res, vbase::to_string(item.uuid).c_str());
    ASSERT_NE(uri, nullptr);
    EXPECT_EQ(std::string(uri), "res://a.bin");
    vasset_resolver_destroy(res);

    EXPECT_LT(vasset_vpk_read(vpk, "res://missing.bin", &blob), 0); // unknown path -> error
    vasset_vpk_close(vpk);
}

TEST(VassetCApi, ImportHalfLinkageReportsError)
{
    // Exercises the vasset-import TU: importing a path that doesn't exist must fail (negative) and
    // populate the thread-local last-error string.
    EXPECT_LT(vasset_import_folder("this/path/does/not/exist", 0), 0);
    EXPECT_GT(std::string(vasset_last_error()).size(), 0u);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
