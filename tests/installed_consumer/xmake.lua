set_project("vasset-installed-consumer")
set_languages("cxx23")
add_rules("mode.debug", "mode.release")
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

option("prefix")
    set_showmenu(true)
    set_description("Absolute path to a fresh vasset installation")
option_end()

if is_plat("windows") then
    set_runtimes(is_mode("debug") and "MTd" or "MT")
    add_cxxflags("/Zc:__cplusplus", "/EHsc")
end

-- Match the installed library's external dependencies, without importing any source targets.
add_requires("glm", "stb", "xxhash", "meshoptimizer", "tinyexr", "zstd", "zlib", "vfilesystem", "opencl")
add_requires("miniaudio 0.11.25")
add_requires("assimp", {configs = {shared = false, debug = is_mode("debug"), draco = is_plat("windows")}})
add_requires("ozz-animation", {configs = {tools = false, fbx = false, gltf = false, data = false, debug = is_mode("debug")}})
add_requires("ktx", {configs = {decoder = true, opencl = true, shared = false, vulkan = true}})
local shader_configs = {debug = is_mode("debug"), vshaderc_lib = true}
if is_plat("windows") then
    shader_configs.runtimes = is_mode("debug") and "MTd" or "MT"
end
add_requires("vshadersystem v1.0.0", {configs = shader_configs})

target("installed-consumer")
    set_kind("binary")
    add_files("main.cpp")
    add_defines("GLM_FORCE_DEPTH_ZERO_TO_ONE", "GLM_ENABLE_EXPERIMENTAL", "GLM_FORCE_RADIANS")
    add_packages("glm", "stb", "xxhash", "meshoptimizer", "tinyexr", "zstd", "zlib", "vfilesystem",
                 "opencl", "miniaudio", "assimp", "ozz-animation", "ktx", "vshadersystem")
    on_load(function (target)
        local prefix = assert(get_config("prefix"), "--prefix is required")
        assert(path.is_absolute(prefix), "--prefix must be absolute")
        target:add("includedirs", path.join(prefix, "include"))
        -- Full paths prevent a missing installed archive from falling back to build outputs.
        for _, name in ipairs({"vasset-import", "vasset", "GaussForge", "spz"}) do
            local archive = path.join(prefix, "lib", target:is_plat("windows") and (name .. ".lib") or ("lib" .. name .. ".a"))
            assert(os.isfile(archive), "missing installed archive: " .. archive)
            target:add("links", archive)
        end
        if target:is_plat("macosx") then
            target:add("rpathdirs", "@executable_path")
        elseif target:is_plat("linux") then
            target:add("rpathdirs", "$ORIGIN")
        end
    end)
    after_build(function (target)
        local bindir = path.join(target:pkg("vshadersystem"):installdir(), "bin")
        for _, file in ipairs(os.files(path.join(bindir, "*"))) do
            os.cp(file, target:targetdir())
        end
        for _, dir in ipairs(os.dirs(path.join(bindir, "*"))) do
            os.cp(dir, target:targetdir())
        end
    end)
