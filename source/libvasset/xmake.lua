add_requires("glm", "stb", "xxhash", "meshoptimizer", "tinyexr", "zstd")
local enable_import_targets = not is_plat("android") and (not is_plat("wasm") or has_config("vasset_enable_wasm_import"))

if enable_import_targets then
    add_requires("assimp", {configs = {shared = false, debug = is_mode("debug"), draco = not is_plat("wasm")}})
    add_requires("vshadersystem v0.9.0", {configs = { debug = is_mode("debug") }})
end
if is_plat("windows") then
    add_requires("ktx-windows", {configs = {shared = false}})
else
    add_requires("ktx", {configs = {decoder = true, vulkan = true, shared = false}})
end

local runtime_headers = {
    "include/(vasset/asset_error.hpp)",
    "include/(vasset/uuid_resolver.hpp)",
    "include/(vasset/vasset_registry.hpp)",
    "include/(vasset/vasset_runtime.hpp)",
    "include/(vasset/vasset_type.hpp)",
    "include/(vasset/vgaussiansplat.hpp)",
    "include/(vasset/vmaterial.hpp)",
    "include/(vasset/vmesh.hpp)",
    "include/(vasset/vpk.hpp)",
    "include/(vasset/vtexture.hpp)",
    "include/(vasset/vvertex.hpp)",
}

local import_headers = {
    "include/(vasset/editor_filesystem.hpp)",
    "include/(vasset/vasset.hpp)",
    "include/(vasset/vasset_import_database.hpp)",
    "include/(vasset/vasset_import.hpp)",
    "include/(vasset/vasset_importers.hpp)",
    "include/(vasset/vimport.hpp)",
}

-- target definition, name: vasset
target("vasset")
    set_kind("static")
    if is_plat("android") then
        add_cflags("-fPIC")
        add_cxflags("-fPIC")
    end
    add_includedirs("include", {public = true})
    add_headerfiles(table.unpack(runtime_headers))
    add_files("src/vasset_registry.cpp", "src/vgaussiansplat.cpp", "src/vmesh.cpp", "src/vpk.cpp", "src/vpk_filesystem.cpp",
              "src/vtexture.cpp")
    add_deps("dds-ktx", "vfilesystem", {public = true})
    add_packages("glm", "stb", "xxhash", "meshoptimizer", "tinyexr", "zstd", { public = true })
    if is_plat("windows") then
        add_packages("ktx-windows", { public = true })
    else
        add_packages("ktx", { public = true })
    end
    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
    else
        add_defines("NDEBUG", { public = true })
    end
    add_defines("GLM_FORCE_DEPTH_ZERO_TO_ONE", { public = true })
    add_defines("GLM_ENABLE_EXPERIMENTAL", { public = true })
    add_defines("GLM_FORCE_RADIANS", { public = true })
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset")

if enable_import_targets then
    target("vasset-import")
        set_kind("static")
        if is_plat("android") then
            add_cflags("-fPIC")
            add_cxflags("-fPIC")
        end
        add_includedirs("include", {public = true})
        add_headerfiles(table.unpack(import_headers))
        add_files("src/editor_filesystem.cpp", "src/vasset_import_database.cpp", "src/vasset_importers.cpp", "src/vimport.cpp")
        add_deps("vasset", "GaussForge", {public = true})
        add_packages("assimp", "vshadersystem", { public = true })
        if is_mode("debug") then
            add_defines("_DEBUG", { public = true })
        else
            add_defines("NDEBUG", { public = true })
        end
        add_defines("VASSET_HAS_IMPORTERS", { public = true })
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset-import")
end
