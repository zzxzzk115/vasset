#include <vasset/vasset_importers.hpp>
#include <vasset/vasset_registry.hpp>
#include <vasset/vasset_import_database.hpp>
#include <vshadersystem/vsh_format.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

bool checkShaderCook()
{
    std::filesystem::create_directories("consumer-shaders/imported");
    std::filesystem::create_directories("consumer-shaders/source");
    {
        std::ofstream manifest("consumer-shaders/smoke.vshaderlib.lua");
        manifest << "return { name = \"smoke\", root = \"source\", shaders = {\"*.vshader\"} }\n";
        std::ofstream shader("consumer-shaders/source/smoke.vshader");
        shader << "[shader(\"fragment\")]\n"
                  "float4 fragmentMain() : SV_Target0 { return float4(1, 0, 0, 1); }\n";
    }
    vasset::VAssetRegistry registry {};
    registry.setAssetRootPath("consumer-shaders");
    registry.setImportedFolderName("imported");
    vasset::VAssetImporter importer {registry};
    if (!importer.importOrReimportAsset("consumer-shaders/smoke.vshaderlib.lua", true)
        || registry.getRegistry().size() != 1)
        return false;
    auto output = std::filesystem::path("consumer-shaders") / registry.getRegistry().begin()->second.importedPath;
    // Reproduce a matching legacy cache record with newer outputs and unchanged source hashes.
    const std::string databasePath = "consumer-shaders/imported/asset_database.tsv";
    vasset::VAssetImportDatabase database;
    if (!database.load(databasePath) || database.records().size() != 1)
        return false;
    auto legacy = database.records().begin()->second;
    legacy.importerVersion = "shader_library:2";
    database.upsert(legacy);
    if (!database.save(databasePath))
        return false;
    {
        std::ofstream stale(output, std::ios::binary);
        stale << "legacy compiler output sentinel";
    }
    if (!importer.importOrReimportAsset("consumer-shaders/smoke.vshaderlib.lua"))
        return false;
    for (bool web : {false, true})
    {
        output.replace_extension(web ? ".vshweblib" : ".vshlib");
        std::ifstream stream(output, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
        auto library = vshadersystem::v1::read_library(bytes);
        if (!library.isOk() || library.value().entries.size() != 1)
            return false;
        const auto& entry = library.value().entries.front();
        auto binary = vshadersystem::v1::read_binary(entry.blob);
        if (!binary.isOk() || entry.stage != vshadersystem::ShaderStage::eFrag
            || binary.value().stage != entry.stage || binary.value().spirv.empty()
            || (web && binary.value().wgsl.empty()))
            return false;
    }
    // Force recompilation so an old cooked file cannot hide a compiler failure.
    {
        std::ofstream shader("consumer-shaders/source/smoke.vshader");
        shader << "this is not valid Slang;\n";
    }
    if (importer.importOrReimportAsset("consumer-shaders/smoke.vshaderlib.lua", true))
        return false;
    std::cout << "Installed shader consumer: legacy-cache recook, SPIR-V/WGSL readback and invalid-source rejection PASS\n";
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2 || !std::filesystem::is_regular_file(argv[1]))
        return 1;

    // Import a private copy so the smoke test never modifies the source fixture.
    std::filesystem::create_directories("consumer-assets/imported");
    std::filesystem::copy_file(argv[1], "consumer-assets/input.spz",
                               std::filesystem::copy_options::overwrite_existing);
    vasset::VAssetRegistry registry {};
    registry.setAssetRootPath("consumer-assets");
    registry.setImportedFolderName("imported");
    vasset::VGaussianSplatImporter importer {registry};
    vasset::VGaussianSplat imported {};
    auto result = importer.importGaussianSplat("consumer-assets/input.spz", imported, true);
    if (!result || imported.numPoints <= 0 || registry.getRegistry().size() != 1)
        return 1;

    const auto& entry = registry.getRegistry().begin()->second;
    vasset::VGaussianSplat loaded {};
    if (!vasset::loadGaussianSplat("consumer-assets/" + entry.importedPath, loaded)
        || loaded.numPoints != imported.numPoints || loaded.shDegree != imported.shDegree)
        return 1;
    vasset::VGaussianSplat missing {};
    if (importer.importGaussianSplat("consumer-assets/missing.spz", missing, true))
        return 1;
    std::cout << "Installed Gaussian consumer: " << loaded.numPoints << " points, readback and missing-file rejection PASS\n";
    return checkShaderCook() ? 0 : 1;
}
