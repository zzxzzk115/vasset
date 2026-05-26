#include <vasset/tool_cli.hpp>

#ifdef VASSET_CLI_HAS_VULTRA_BUILTIN_SHADERS
#include <builtin_shaders.hpp>

#include <cstddef>
#include <string>
#endif

int main(int argc, char** argv)
{
#ifdef VASSET_CLI_HAS_VULTRA_BUILTIN_SHADERS
    vasset::VAssetImporter::ImportOptions options;
    options.shaderVirtualIncludes.reserve(builtin_shader_include_sources_count);
    for (size_t i = 0; i < builtin_shader_include_sources_count; ++i)
    {
        const auto& source = builtin_shader_include_sources[i];
        options.shaderVirtualIncludes.push_back({
            .virtualPath = source.path,
            .sourceText  = std::string(reinterpret_cast<const char*>(source.data), source.size),
        });
    }
    return vasset::tool::run_vasset_cli(argc, argv, options);
#else
    return vasset::tool::run_vasset_cli(argc, argv);
#endif
}
