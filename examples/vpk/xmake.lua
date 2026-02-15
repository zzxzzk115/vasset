-- target defination, name: vasset-example-vpk
target("vasset-example-vpk")
    -- set target kind: binary
    set_kind("binary")

    -- add source files
    add_files("main.cpp")

    -- add deps
    add_deps("vasset")

    -- copy out.vpk
	after_build(function (target)
		os.cp("$(scriptdir)/out.vpk", target:targetdir())
	end	)

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset-example-vpk")
