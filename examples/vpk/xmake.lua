-- target defination, name: vasset-example-vpk
target("vasset-example-vpk")
    -- set target kind: binary
    set_kind("binary")

    -- add source files
    add_files("main.cpp")

    -- add deps
    add_deps("vasset-import")

    after_build(function (target)
        -- This step runs the freshly built tool to pack a vpk. On wasm/android the artifact is a
        -- .html / .so, not a host-runnable executable, so skip it there (error 193 otherwise).
        if is_plat("wasm") or is_plat("android") then
            return
        end
        local source_asset_root = path.absolute(path.join(os.scriptdir(), "..", "..", "resources"))
        local asset_root = path.join(target:autogendir(), "vasset-example-vpk-resources")
        local out_vpk = path.join(target:targetdir(), "out.vpk")

        os.rm(asset_root)
        os.mkdir(asset_root)
        os.cp(path.join(source_asset_root, "*"), asset_root)
        os.execv(target:targetfile(), {asset_root, out_vpk})
    end)

    -- set target directory
    set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset-example-vpk")
