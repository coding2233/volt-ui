add_rules("mode.debug", "mode.release")

add_requires("libsdl3")
add_requires("imgui", {configs = {sdl3 = true, sdl3_renderer = true}})

target("volt-ui")
    set_kind("static")
    set_languages("c++17")
    add_packages("libsdl3", "imgui")
    add_files("src/**.cpp")
    add_includedirs("include", {public = true})

for _, dir in ipairs(os.dirs("examples/*")) do
    local name = path.basename(dir)
    target(name)
        set_kind("binary")
        set_default(false)
        add_files(path.join(dir, "*.cpp"))
        add_deps("volt-ui")
        add_packages("libsdl3", "imgui")
end
