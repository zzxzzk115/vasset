add_requires("glm", "nlohmann_json", "stb", "xxhash", "meshoptimizer")
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

    -- add packages
    add_packages("glm", "nlohmann_json", "stb", "xxhash", "meshoptimizer", "assimp", { public = true })
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

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset")
