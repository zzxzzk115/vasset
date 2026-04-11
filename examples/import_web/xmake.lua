if is_plat("wasm") then
    target("vasset-example-import-web")
        set_kind("binary")
        set_extension(".html")

        add_files("main.cpp")
        add_deps("vasset-import")

        add_ldflags("--shell-file", path.join(os.scriptdir(), "shell.html"), {force = true})
        add_ldflags("-sALLOW_MEMORY_GROWTH=1", {force = true})
        add_ldflags("-sFORCE_FILESYSTEM=1", {force = true})
        add_ldflags("-sNO_EXIT_RUNTIME=1", {force = true})
        add_ldflags("-sEXPORTED_FUNCTIONS=['_main','_vasset_demo_reset_workspace','_vasset_demo_import_asset_root','_vasset_demo_last_report']", {force = true})
        add_ldflags("-sEXPORTED_RUNTIME_METHODS=['ccall','FS']", {force = true})

        set_targetdir("$(builddir)/$(plat)/$(arch)/$(mode)/vasset-example-import-web")
end
