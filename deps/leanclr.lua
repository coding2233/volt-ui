target("leanclr")
    set_kind("static")
    set_languages("c++17")
    add_defines("LEANCLR_STATIC")
    if is_plat("windows") then
        add_defines("LEANCLR_PLATFORM_WIN")
    elseif is_plat("linux") then
        add_defines("LEANCLR_PLATFORM_LINUX", "LEANCLR_PLATFORM_POSIX")
    elseif is_plat("macosx") then
        add_defines("LEANCLR_PLATFORM_MAC", "LEANCLR_PLATFORM_POSIX")
    end

    local runtime_dirs = {
        "alloc", "codegen", "gc", "icalls",
        "interp", "intrinsics", "log", "metadata",
        "misc", "platform", "utils", "vm", "public_impl"
    }

    for _, dir in ipairs(runtime_dirs) do
        add_files(path.join("leanclr/src/runtime", dir, "*.cpp"))
    end

    add_includedirs("leanclr/src/runtime", {public = true})
    for _, dir in ipairs(runtime_dirs) do
        add_includedirs(path.join("leanclr/src/runtime", dir), {public = true})
    end
    add_includedirs(path.join("leanclr/src/runtime", "public"), {public = true})
    add_includedirs(path.join("leanclr/src/runtime", "3rd"), {public = true})
