-- target defination, name: vasset-example-vpk
target("vasset-example-vpk")
    -- set target kind: binary
    set_kind("binary")

    -- add source files
    add_files("main.cpp")

    -- add deps
    add_deps("vasset-import")

    -- The importer links the prebuilt Slang shared libs (vshaderc-lib) on desktop; they use
    -- @rpath/$ORIGIN install names, so the exe must find them next to itself (copied below). Depend on
    -- the vshadersystem package directly (already pulled transitively) so target:pkg can locate its
    -- bin/, which bundles the Slang runtime when built with vshaderc_lib.
    if is_plat("windows", "linux", "macosx") then
        add_packages("vshadersystem")
        if is_plat("macosx") then
            add_rpathdirs("@executable_path")
        elseif is_plat("linux") then
            add_rpathdirs("$ORIGIN")
        end
    end

    after_build(function (target)
        -- This step runs the freshly built tool to pack a vpk. On wasm/android the artifact is a
        -- .html / .so, not a host-runnable executable, so skip it there (error 193 otherwise).
        if is_plat("wasm") or is_plat("android") then
            return
        end

        -- Copy the Slang runtime next to the exe so it can run (matching the rpath above). The
        -- vshadersystem package bundles it in its bin/ when built with vshaderc_lib.
        local vsh = target:pkg("vshadersystem")
        if vsh and vsh:installdir() then
            local bindir = path.join(vsh:installdir(), "bin")
            if os.isdir(bindir) then
                for _, f in ipairs(os.files(path.join(bindir, "*"))) do os.trycp(f, target:targetdir()) end
                for _, d in ipairs(os.dirs(path.join(bindir, "*"))) do os.trycp(d, target:targetdir()) end
            end
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
