-- Importer tests need the same Slang runtime deployment as the VPK example.
rule("vasset.test_slang_runtime")
    on_load(function (target)
        target:add("packages", "vshadersystem")
        if target:is_plat("macosx") then
            target:add("rpathdirs", "@executable_path")
        elseif target:is_plat("linux") then
            target:add("rpathdirs", "$ORIGIN")
        end
    end)
    after_build(function (target)
        local vsh = assert(target:pkg("vshadersystem"), "missing test Slang package")
        local bindir = path.join(vsh:installdir(), "bin")
        assert(os.isdir(bindir), "missing test Slang runtime: " .. bindir)
        for _, file in ipairs(os.files(path.join(bindir, "*"))) do
            os.cp(file, target:targetdir())
        end
        for _, dir in ipairs(os.dirs(path.join(bindir, "*"))) do
            os.cp(dir, target:targetdir())
        end
    end)
rule_end()

if not is_plat("android") and not is_plat("wasm") then
    includes("binary_serialization")
    includes("importers")
    includes("c_api")
end
