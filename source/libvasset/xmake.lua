add_requires("glm", "stb", "xxhash", "meshoptimizer", "tinyexr", "zstd")
if not is_plat("android") and not is_plat("wasm") then
    add_requires("assimp", {configs = {shared = true, debug = is_mode("debug"), draco = true}})
    if is_plat("linux") then
        add_requires("zlib")
    end
end
if is_plat("windows") then
    add_requires("ktx-windows")
else
    add_requires("ktx", {configs = {decoder = true, vulkan = true}})
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

if not is_plat("android") and not is_plat("wasm") then
    target("vasset-import")
        set_kind("static")
        if is_plat("android") then
            add_cflags("-fPIC")
            add_cxflags("-fPIC")
        end
        add_includedirs("include", {public = true})
        add_headerfiles(table.unpack(import_headers))
        add_files("src/editor_filesystem.cpp", "src/vasset_importers.cpp", "src/vimport.cpp")
        add_deps("vasset", "GaussForge", {public = true})
        add_packages("assimp", { public = true })
        if is_plat("linux") then
            -- Some Linux assimp shared-package builds do not propagate zlib strongly enough for
            -- downstream executables, so link it explicitly on the importer target as well.
            add_packages("zlib", { public = true })
            -- GNU ld on Linux can drop DSOs needed only by shared-library dependencies when
            -- --as-needed is active, which shows up as "DSO missing from command line".
            add_ldflags("-Wl,--no-as-needed", { public = true })
        end
        if is_mode("debug") then
            add_defines("_DEBUG", { public = true })
        else
            add_defines("NDEBUG", { public = true })
        end
        add_defines("VASSET_HAS_IMPORTERS", { public = true })
        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset-import")
end
