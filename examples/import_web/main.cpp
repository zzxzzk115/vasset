#include <vasset/vasset_import.hpp>
#include <vasset/vasset_runtime.hpp>

#include <emscripten/emscripten.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    std::string g_LastReport;

    std::string assetTypeLabel(vasset::VAssetType type)
    {
        return vasset::toString(type);
    }

    std::string assetErrorLabel(vasset::AssetError error)
    {
        switch (error)
        {
            case vasset::AssetError::eOk:
                return "ok";
            case vasset::AssetError::eNotFound:
                return "not_found";
            case vasset::AssetError::eInvalidFormat:
                return "invalid_format";
            case vasset::AssetError::eInvalidImportFile:
                return "invalid_import_file";
            case vasset::AssetError::eUnknownImporter:
                return "unknown_importer";
            case vasset::AssetError::eImportFailed:
                return "import_failed";
            case vasset::AssetError::eIOError:
                return "io_error";
            case vasset::AssetError::eNotSupported:
                return "not_supported";
            case vasset::AssetError::eOutOfMemory:
                return "out_of_memory";
            default:
                return "unknown_error";
        }
    }

    std::string readTextPreview(const fs::path& filePath, size_t maxChars = 240)
    {
        std::ifstream f(filePath, std::ios::binary);
        if (!f)
            return "<failed to open text asset>";

        std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (data.size() > maxChars)
        {
            data.resize(maxChars);
            data += "...";
        }

        for (char& ch : data)
        {
            if (ch == '\r')
                ch = ' ';
            else if (ch == '\n')
                ch = ' ';
        }
        return data;
    }

    std::string summarizeTexture(const fs::path& importedPath)
    {
        vasset::VTexture texture {};
        auto             res = vasset::loadTexture(importedPath.generic_string(), texture);
        if (!res)
            return "failed to decode imported texture";

        std::ostringstream oss;
        oss << "texture " << texture.width << "x" << texture.height << ", mips=" << texture.mipLevels
            << ", bytes=" << texture.data.size();
        return oss.str();
    }

    std::string summarizeMesh(const fs::path& importedPath)
    {
        vasset::VMesh mesh {};
        auto          res = vasset::loadMesh(importedPath.generic_string(), mesh);
        if (!res)
            return "failed to decode imported mesh";

        std::ostringstream oss;
        oss << "mesh \"" << mesh.name << "\", vertices=" << mesh.vertexCount << ", indices=" << mesh.indices.size()
            << ", submeshes=" << mesh.subMeshes.size() << ", materials=" << mesh.materials.size();

        if (!mesh.materials.empty())
        {
            oss << ", material_names=[";
            for (size_t i = 0; i < mesh.materials.size(); ++i)
            {
                if (i > 0)
                    oss << ", ";
                oss << mesh.materials[i].name;
            }
            oss << "]";
        }
        return oss.str();
    }

    std::string summarizeGaussianSplat(const fs::path& importedPath)
    {
        vasset::VGaussianSplat splat {};
        auto                   res = vasset::loadGaussianSplat(importedPath.generic_string(), splat);
        if (!res)
            return "failed to decode imported gaussian splat";

        std::ostringstream oss;
        oss << "gaussian_splat \"" << splat.name << "\", points=" << splat.numPoints << ", shDegree="
            << splat.shDegree << ", antialiased=" << (splat.antialiased ? "true" : "false");
        return oss.str();
    }

    std::string summarizeTextAsset(vasset::VAssetType type, const fs::path& importedPath)
    {
        std::error_code ec;
        auto            size = fs::file_size(importedPath, ec);

        std::ostringstream oss;
        oss << assetTypeLabel(type) << ", bytes=";
        if (ec)
            oss << "unknown";
        else
            oss << size;
        oss << ", preview=\"" << readTextPreview(importedPath) << "\"";
        return oss.str();
    }

    std::string summarizeImportedAsset(const fs::path& assetRoot, const vasset::VAssetRegistry::AssetEntry& entry)
    {
        const fs::path importedPath = assetRoot / entry.importedPath;
        switch (entry.type)
        {
            case vasset::VAssetType::eTexture:
                return summarizeTexture(importedPath);
            case vasset::VAssetType::eMesh:
                return summarizeMesh(importedPath);
            case vasset::VAssetType::eGaussianSplat:
                return summarizeGaussianSplat(importedPath);
            case vasset::VAssetType::eScene:
            case vasset::VAssetType::eSceneManifest:
            case vasset::VAssetType::eScriptLua:
                return summarizeTextAsset(entry.type, importedPath);
            case vasset::VAssetType::eMaterial:
            case vasset::VAssetType::eUnknown:
            default:
                return "no summary available";
        }
    }

    int resetWorkspaceImpl(const char* workspaceRoot)
    {
        if (workspaceRoot == nullptr || workspaceRoot[0] == '\0')
        {
            g_LastReport = "reset failed: workspace root is empty";
            return 1;
        }

        fs::path root(workspaceRoot);
        if (root == "/" || root.empty())
        {
            g_LastReport = "reset failed: refusing to clear filesystem root";
            return 1;
        }

        std::error_code ec;
        fs::remove_all(root, ec);
        if (ec)
        {
            g_LastReport = "reset failed: could not remove existing workspace";
            return 1;
        }

        fs::create_directories(root, ec);
        if (ec)
        {
            g_LastReport = "reset failed: could not recreate workspace";
            return 1;
        }

        g_LastReport = "workspace reset: " + root.generic_string();
        return 0;
    }

    int importAssetRootImpl(const char* assetRootPath)
    {
        if (assetRootPath == nullptr || assetRootPath[0] == '\0')
        {
            g_LastReport = "import failed: asset root is empty";
            return 1;
        }

        const fs::path assetRoot(assetRootPath);
        if (!fs::exists(assetRoot) || !fs::is_directory(assetRoot))
        {
            g_LastReport = "import failed: asset root does not exist";
            return 1;
        }

        vasset::VAssetRegistry registry {};
        registry.setAssetRootPath(assetRoot.generic_string());
        registry.setImportedFolderName("imported");

        const std::string registryPath = (assetRoot / "imported" / "asset_registry.tsv").generic_string();
        const auto        loadResult = registry.load(registryPath);
        const bool        hadExistingRegistry = static_cast<bool>(loadResult);
        std::unordered_map<std::string, vasset::VAssetRegistry::AssetEntry> previousEntriesBySource;
        previousEntriesBySource.reserve(registry.getRegistry().size());
        for (const auto& [uuid, entry] : registry.getRegistry())
            previousEntriesBySource.emplace(entry.sourcePath, entry);

        vasset::VAssetImporter importer(registry);
        auto importResult = importer.importOrReimportAssetFolder(assetRoot.generic_string(), false);
        if (!importResult)
        {
            g_LastReport = "import failed: VAssetImporter returned " + assetErrorLabel(importResult.error()) +
                           ". Check the log above for the exact file that failed.";
            return 1;
        }

        registry.cleanup();

        auto saveResult = registry.save(registryPath);
        if (!saveResult)
        {
            g_LastReport = "import failed: could not save asset registry";
            return 1;
        }

        std::vector<vasset::VAssetRegistry::AssetEntry> entries;
        entries.reserve(registry.getRegistry().size());
        for (const auto& [uuid, entry] : registry.getRegistry())
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.sourcePath < rhs.sourcePath;
        });

        std::ostringstream oss;
        oss << "Imported " << entries.size() << " asset(s)\n";
        oss << "asset_root: " << assetRoot.generic_string() << "\n";
        oss << "registry: " << registryPath << "\n";
        oss << "import_mode: incremental\n";
        oss << "registry_loaded: " << (hadExistingRegistry ? "yes" : "no") << "\n";
        oss << "registry_entries_before: " << previousEntriesBySource.size() << "\n";

        size_t newEntries = 0;
        for (const auto& entry : entries)
        {
            if (!previousEntriesBySource.contains(entry.sourcePath))
                ++newEntries;
        }
        oss << "new_entries_this_run: " << newEntries << "\n";

        if (entries.empty())
        {
            oss << "\nNo importable assets were found. Try png/jpg, fbx/obj/gltf/dae, ply/spz/splat/ksplat, "
                   "vscn/vmanifest/lua.\n";
        }
        else
        {
            for (const auto& entry : entries)
            {
                oss << "\n- source:   " << entry.sourcePath << "\n";
                oss << "  type:     " << assetTypeLabel(entry.type) << "\n";
                oss << "  imported: " << entry.importedPath << "\n";
                oss << "  summary:  " << summarizeImportedAsset(assetRoot, entry) << "\n";
            }
        }

        g_LastReport = oss.str();
        return 0;
    }
} // namespace

extern "C"
{
    EMSCRIPTEN_KEEPALIVE int vasset_demo_reset_workspace(const char* workspaceRoot)
    {
        return resetWorkspaceImpl(workspaceRoot);
    }

    EMSCRIPTEN_KEEPALIVE int vasset_demo_import_asset_root(const char* assetRootPath)
    {
        return importAssetRootImpl(assetRootPath);
    }

    EMSCRIPTEN_KEEPALIVE const char* vasset_demo_last_report() { return g_LastReport.c_str(); }
}

int main() { return 0; }
