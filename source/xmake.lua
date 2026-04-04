includes("libvasset")
if not is_plat("android") and not is_plat("wasm") then
    includes("vasset-cli")
end
