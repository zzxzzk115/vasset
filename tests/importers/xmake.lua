-- target defination, name: test-importers
target("test-importers")
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

    -- add rules
    add_rules("copy_resources")

    set_values("resource_files", "resources/textures/awesomeface.png")

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test-importers")
