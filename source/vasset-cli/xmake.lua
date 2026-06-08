-- target defination, name: vasset-cli
target("vasset-cli")
    -- set target kind: binary
    set_kind("binary")

    -- add source files
    add_files("**.cpp")

    -- add deps
    add_deps("vasset-import")
    -- The engine GLSL include sources are read straight from builtin/shaders/include on disk (the
    -- same source files builtin.vpk is packed from); no generated header is involved. Pass the
    -- directory as a string define (forward-slashed so Windows backslashes don't need escaping).
    local builtin_shader_includes = path.join(os.projectdir(), "builtin/shaders/include"):gsub("\\", "/")
    if os.isdir(builtin_shader_includes) then
        add_defines("VASSET_CLI_HAS_VULTRA_BUILTIN_SHADERS")
        add_defines("VASSET_CLI_BUILTIN_SHADER_INCLUDE_DIR=\"" .. builtin_shader_includes .. "\"")
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
