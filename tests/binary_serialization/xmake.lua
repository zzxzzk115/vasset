add_requires("gtest")

-- target defination, name: test-binary-serialization
target("test-binary-serialization")
    -- set target kind: binary
    set_kind("binary")

    -- add source files
    add_files("**.cpp")

    -- add packages
    add_packages("gtest")

    -- add deps
    add_deps("vasset")

    -- add defines
    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
    else
        add_defines("NDEBUG", { public = true })
    end

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test-binary-serialization")
