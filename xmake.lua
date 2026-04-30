add_rules("mode.debug", "mode.release")

add_requires("libsdl3")
add_requires("imgui", {configs = {sdl3 = true, sdl3_renderer = true}})

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
    add_files("src/**.cpp")
    add_includedirs("include", {public = true})
    if has_config("enable_leanclr") then
        includes("deps/leanclr.lua")
        add_deps("leanclr")
        add_defines("VOLT_ENABLE_LEANCLR")
    end

for _, dir in ipairs(os.dirs("examples/*")) do
    local name = path.basename(dir)
    target("example-"..name)
    set_kind("binary")
    set_default(false)
    add_files(path.join(dir, "*.cpp"))
    add_deps("volt-ui")
    add_packages("libsdl3", "imgui")
end

add_plugindirs("xmake/plugins")