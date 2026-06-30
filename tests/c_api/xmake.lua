add_requires("gtest")

-- target definition, name: test-c-api
target("test-c-api")
    set_kind("binary")
    add_files("**.cpp")
    add_packages("gtest")
    add_deps("vasset", "vasset-import")
    if is_mode("debug") then
        add_defines("_DEBUG", { public = true })
    else
        add_defines("NDEBUG", { public = true })
    end
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/test-c-api")
