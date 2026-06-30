if not is_plat("android") and not is_plat("wasm") then
    includes("binary_serialization")
    includes("importers")
    includes("c_api")
end
