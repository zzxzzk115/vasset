-- Run after installing to a fresh prefix; verify the shipped archive set, not build outputs.
function main(prefix, mode)
    assert(prefix and (mode == "importers" or mode == "runtime"), "expected: PREFIX importers|runtime")
    local function archive(name)
        return os.isfile(path.join(prefix, "lib", name .. ".lib"))
            or os.isfile(path.join(prefix, "lib", "lib" .. name .. ".a"))
    end
    assert(archive("vasset"), "missing runtime archive")
    assert(os.isfile(path.join(prefix, "include/vasset/vfont.hpp")), "missing public vfont.hpp")
    for _, name in ipairs({"vasset-import", "GaussForge", "spz"}) do
        assert(archive(name) == (mode == "importers"), "unexpected install state: " .. name)
    end
    assert(not archive("miniply"), "unused miniply archive must not ship")
    print("Installed archive/header contract: " .. mode .. " PASS")
end
