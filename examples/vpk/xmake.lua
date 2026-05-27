-- target defination, name: vasset-example-vpk
target("vasset-example-vpk")
    -- set target kind: binary
    set_kind("binary")

    -- add source files
    add_files("main.cpp")

    -- add deps
    add_deps("vasset", "vasset-cli")

    before_build(function (target)
        local vasset_cli = target:dep("vasset-cli"):targetfile()
        local asset_root = path.join(os.projectdir(), "resources")
        local out_vpk = path.join(target:autogendir(), "out.vpk")

        os.mkdir(target:autogendir())
        target:data_set("vasset.example.vpk", out_vpk)
        os.execv(vasset_cli, {"import", asset_root})
        os.execv(vasset_cli, {"pack", asset_root, out_vpk})
    end)

    -- copy out.vpk
	after_build(function (target)
		os.cp(target:data("vasset.example.vpk"), path.join(target:targetdir(), "out.vpk"))
	end	)

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset-example-vpk")
