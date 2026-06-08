#include <vasset/tool_cli.hpp>

#ifdef VASSET_CLI_HAS_VULTRA_BUILTIN_SHADERS
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace
{
    // Build the importer's shader virtual includes from the engine's builtin GLSL include sources,
    // read straight from VASSET_CLI_BUILTIN_SHADER_INCLUDE_DIR (builtin/shaders/include on disk).
    // virtualPath mirrors what shaders #include: "include/<path relative to that dir>".
    vasset::VAssetImporter::ImportOptions makeBuiltinImportOptions()
    {
        namespace fs = std::filesystem;

        vasset::VAssetImporter::ImportOptions options;

        const fs::path  root = VASSET_CLI_BUILTIN_SHADER_INCLUDE_DIR;
        std::error_code ec;
        if (!fs::is_directory(root, ec))
            return options;

        for (const auto& entry : fs::recursive_directory_iterator(root, ec))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".glsl")
                continue;

            std::ifstream     in(entry.path(), std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();

            const std::string rel = fs::relative(entry.path(), root, ec).generic_string();
            options.shaderVirtualIncludes.push_back({
                .virtualPath = "include/" + rel,
                .sourceText  = ss.str(),
            });
        }
        return options;
    }
} // namespace
#endif

int main(int argc, char** argv)
{
#ifdef VASSET_CLI_HAS_VULTRA_BUILTIN_SHADERS
    return vasset::tool::run_vasset_cli(argc, argv, makeBuiltinImportOptions());
#else
    return vasset::tool::run_vasset_cli(argc, argv);
#endif
}
