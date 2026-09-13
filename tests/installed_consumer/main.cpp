#include <vasset/vasset_importers.hpp>
#include <vasset/vasset_registry.hpp>

#include <filesystem>
#include <iostream>

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
    return 0;
}
