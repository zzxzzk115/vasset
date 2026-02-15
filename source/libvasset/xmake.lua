add_requires("glm", "stb", "xxhash", "meshoptimizer", "tinyexr", "zstd")
add_requires("assimp", {configs = {shared = true, debug = is_mode("debug"), draco = true}})
if is_plat("windows") then
    add_requires("ktx-windows")
else
    add_requires("ktx", {configs = {decoder = true, vulkan = true}})
end

-- target defination, name: vasset
target("vasset")
    -- set target kind: static library
    set_kind("static")

    -- add include dir
    add_includedirs("include", {public = true}) -- public: let other targets to auto include

    -- add header files
    add_headerfiles("include/(vasset/**.hpp)")

    -- add source files
    add_files("src/**.cpp")

    -- add deps
    add_deps("dds-ktx", "vfilesystem", {public = true})

    -- add packages
    add_packages("glm", "stb", "xxhash", "meshoptimizer", "tinyexr", "zstd", "assimp", { public = true })
    if is_plat("windows") then
        add_packages("ktx-windows", { public = true })
    else
        add_packages("ktx", { public = true })
    end

    -- add defines
    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
    else
        add_defines("NDEBUG", { public = true })
    end

    -- GLM force settings
    add_defines("GLM_FORCE_DEPTH_ZERO_TO_ONE", { public = true }) -- for vulkan depth range [0, 1]
    -- add_defines("GLM_FORCE_LEFT_HANDED", { public = true }) -- for left-handed coordinate system
    add_defines("GLM_ENABLE_EXPERIMENTAL", { public = true }) -- for experimental features
    add_defines("GLM_FORCE_RADIANS", { public = true }) -- force radians

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset")
