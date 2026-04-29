add_rules("mode.debug", "mode.release")

add_requires("libsdl3", "imgui")


-- Volt static library
target("volt-ui")
    set_kind("static")
    set_languages("c++17")
    add_packages("libsdl3", "imgui")
    add_files("src/**.cpp")
    add_includedirs("include", {public = true})
    add_includedirs("src", {public = false})


-- Examples
for _, dir in ipairs(os.dirs("examples/*")) do
    local name = path.basename(dir)
    target(name)
        set_kind("binary")
        set_default(false)
        add_files(path.join(dir, "*.cpp"))
        add_deps("volt-ui")
        add_packages("libsdl3", "imgui")
end

