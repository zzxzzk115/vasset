add_requires("zlib")

target("dds-ktx")
    -- https://xmake.io/zh/api/description/project-target.html#headeronly
    set_kind("headeronly")
    set_default(false)
	add_headerfiles("dds-ktx/dds-ktx.h")
    add_includedirs("dds-ktx", {public = true}) -- public: let other targets to auto include
    add_rules("utils.install.cmake_importfiles")
    add_rules("utils.install.pkgconfig_importfiles")

-- NOTE: unreferenced - no target depends on miniply and no source includes miniply.h
-- (GaussForge brings its own PLY reader). Left non-default so the package does not ship a
-- dead archive; removing the vendored copy is a separate call.
target("miniply")
    set_kind("static")
    set_default(false)
    add_headerfiles("miniply/**.h")
    add_includedirs("miniply", {public = true}) -- public: let other targets to auto include
    add_files("miniply/**.cpp")

-- GaussForge + spz are default only when importers are enabled: runtime-only Android/WASM
-- packages must not build or install importer archives. `xmake install` only installs default
-- targets, and vasset-import is a static lib, so these archives must ship next to
-- vasset-import.lib. The vasset package already names them in `links` under link_importers;
-- without them installed a consumer link fails on "cannot open GaussForge.lib". Headers are
-- private (gf/ + spz reach only vasset_importers.cpp), hence {install = false} - they are
-- listed only so the files show up in generated IDE projects.
local enable_import_targets = not is_plat("android") and (not is_plat("wasm") or has_config("vasset_enable_wasm_import"))

target("spz")
    set_kind("static")
    set_default(enable_import_targets)
    add_headerfiles("spz/**.h", {install = false})
    add_includedirs("spz/src/cc", {public = true}) -- public: let other targets to auto include
    add_files("spz/**.cc")
    add_packages("zlib", {public = true})

target("GaussForge")
    set_kind("static")
    set_default(enable_import_targets)
    add_headerfiles("GaussForge/include/(gf/**.h)", {install = false})
    add_includedirs("GaussForge/include", {public = true})
    add_files("GaussForge/src/core/**.cpp", "GaussForge/src/io/**.cpp")
    remove_files("GaussForge/src/io/sog_*.cpp")
    add_deps("spz", {public = true})

-- vfilesystem is now consumed from xmake-repo (see source/libvasset add_requires), not vendored.
