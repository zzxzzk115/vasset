-- set project name
set_project("vasset")

-- set project version
set_version("0.1.0")

-- set language version: C++ 23
set_languages("cxx23")

-- root ?
local is_root = (os.projectdir() == os.scriptdir())
set_config("root", is_root)
set_config("project_dir", os.scriptdir())

-- global options
option("vasset_build_examples") -- build examples?
    set_default(true)
    set_showmenu(true)
    set_description("Enable vasset examples")
option_end()

option("vasset_build_tests") -- build tests?
    set_default(true)
    set_showmenu(true)
    set_description("Enable vasset tests")
option_end()

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")
    if is_mode("debug") then
        set_runtimes("MDd")
        add_links("ucrtd")
    else
        set_runtimes("MD")
    end
else
    add_cxxflags("-fexceptions")
end

-- add rules
rule("clangd.config")
    on_config(function (target)
        if is_host("windows") then
            os.cp(".clangd.win", ".clangd")
        else
            os.cp(".clangd.nowin", ".clangd")
        end
    end)
rule_end()

rule("copy_resources")
	after_build(function (target)
        local resource_files = target:values("resource_files")
        if resource_files then
            for _, pattern in ipairs(resource_files) do
                pattern = path.join(get_config("project_dir"), pattern)
                local files = os.files(pattern)
                for _, file in ipairs(files) do
                    local relpath = path.relative(file, get_config("project_dir"))
                    local target_dir = path.join(target:targetdir(), path.directory(relpath))
                    os.mkdir(target_dir)
                    os.cp(file, target_dir)
                    print("Copying resource file: " .. file .. " -> " .. target_dir)
                end
            end
        end
    end)

    after_install(function (target)
        local resource_files = target:values("resource_files")
        if resource_files then
            for _, pattern in ipairs(resource_files) do
                pattern = path.join(get_config("project_dir"), pattern)
                local files = os.files(pattern)
                for _, file in ipairs(files) do
                    local relpath = path.relative(file, get_config("project_dir"))
                    local target_dir = path.join(target:installdir(), "bin", path.directory(relpath))
                    os.mkdir(target_dir)
                    os.cp(file, target_dir)
                    print("Copying resource file: " .. file .. " -> " .. target_dir)
                end
            end
        end
    end)
rule_end()

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})
add_rules("clangd.config")

-- add repositories
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

-- include external
includes("external")

-- include source
includes("source")

-- include tests
if has_config("vasset_build_tests") then
    includes("tests")
end

-- if build examples, then include examples
if has_config("vasset_build_examples") then
    includes("examples")
end