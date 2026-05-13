add_rules("mode.debug", "mode.release")

add_requires("libsdl3")
add_requires("imgui", {configs = {sdl3 = true, sdl3_renderer = true}, version = "v1.92.7-docking"})

option("enable_leanclr")
    set_default(false)
    set_showmenu(true)
    set_description("Enable C# scripting via LeanCLR runtime")

target("volt-ui")
    set_kind("static")
    set_languages("c++17")
    add_packages("libsdl3", "imgui")
    add_files("log.c/src/log.c")
    add_includedirs("log.c/src", {public = true})
    add_files("src/VoltApp.cpp", "src/ScriptHost.cpp")
    if is_plat("android") then
        add_files("src/VoltApp_android.cpp")
    else
        add_files("src/VoltApp_desktop.cpp")
    end
    add_includedirs("include", {public = true})
    add_defines("LOG_USE_COLOR")
    if has_config("enable_leanclr") then
        includes("deps/leanclr.lua")
        add_deps("leanclr")
        add_defines("VOLT_ENABLE_LEANCLR", "LEANCLR_STATIC")
    end

for _, dir in ipairs(os.dirs("examples/*")) do
    local name = path.basename(dir)
    if name ~= "android" then
        target("example-"..name)
        set_kind("binary")
        set_default(false)
        add_files(path.join(dir, "*.cpp"))
        add_deps("volt-ui")
        add_packages("libsdl3", "imgui")
        if has_config("enable_leanclr") then
            add_deps("leanclr")
            add_defines("VOLT_ENABLE_LEANCLR")
        end
    end
end

target("example-android")
    set_default(false)
    add_files("examples/android/*.cpp")
    add_deps("volt-ui")
    add_packages("libsdl3", "imgui")
    if is_plat("android") then
        set_kind("shared")
        set_filename("libmain.so")
    else
        set_kind("binary")
    end

add_plugindirs("xmake/plugins")