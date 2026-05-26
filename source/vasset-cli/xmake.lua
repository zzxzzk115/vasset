-- target defination, name: vasset-cli
target("vasset-cli")
    -- set target kind: binary
    set_kind("binary")

    -- add source files
    add_files("**.cpp")

    -- add deps
    add_deps("vasset-import")
    if os.isdir(path.join(os.projectdir(), "builtin/generated/include")) then
        add_deps("vultra_builtin_assets")
        add_includedirs(path.join(os.projectdir(), "builtin/generated/include"))
        add_defines("VASSET_CLI_HAS_VULTRA_BUILTIN_SHADERS")
    end

    -- add defines
    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
    else
        add_defines("NDEBUG", { public = true })
    end

    -- default run arguments
    set_runargs("import", "$(projectdir)/resources")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset-cli")
