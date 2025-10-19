-- target defination, name: vasset-cli
target("vasset-cli")
    -- set target kind: binary
    set_kind("binary")

    -- add source files
    add_files("**.cpp")

    -- add deps
    add_deps("vasset")

    -- add defines
    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
    else
        add_defines("NDEBUG", { public = true })
    end

    -- default run arguments
    set_runargs("resources", "imported", "$(projectdir)")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset-cli")
