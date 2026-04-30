task("build-appmain")
    on_run(function()
        local csc = os.getenv("CSC")
        if not csc or csc == "" then
            local candidates = {
                path.join(os.getenv("DOTNET_ROOT") or "", "sdk", "*", "Roslyn", "bincore", "csc.dll"),
                path.join(os.getenv("HOME") or os.getenv("USERPROFILE") or "", ".dotnet", "sdk", "*", "Roslyn", "bincore", "csc.dll"),
            }
            if os.host() == "windows" then
                table.insert(candidates, 1, "C:/Program Files/dotnet/sdk/*/Roslyn/bincore/csc.dll")
            else
                table.insert(candidates, 1, "/usr/share/dotnet/sdk/*/Roslyn/bincore/csc.dll")
            end
            for _, pattern in ipairs(candidates) do
                local files = os.files(pattern)
                if files and #files > 0 then
                    csc = files[#files]
                    break
                end
            end
        end

        if not csc or not os.isfile(csc) then
            print("error: .NET SDK not found.")
            print("  Please install .NET SDK from https://dotnet.microsoft.com/download")
            print("  Or set CSC environment variable to the path of csc.dll")
            return
        end

        local bcl = path.absolute(path.join("deps", "leanclr", "src", "libraries", "dotnetframework4.x"))
        local plat = os.host()
        local arch = os.arch()
        local mode = get_config("mode") or "release"
        local outdir = path.join(os.projectdir(), "build", plat, arch, mode)
        os.mkdir(outdir)

        local output = path.join(outdir, "AppMain.dll")
        local sources = os.files(path.join("scripts", "AppMain", "*.cs"))

        if #sources == 0 then
            print("warning: no .cs files found in scripts/AppMain")
        end

        local argv = {
            csc,
            "-noconfig", "-nostdlib", "-target:library",
            "-out:" .. output,
            "-reference:" .. path.join(bcl, "mscorlib.dll"),
            "-reference:" .. path.join(bcl, "System.dll"),
            "-reference:" .. path.join(bcl, "System.Core.dll"),
        }
        for _, src in ipairs(sources) do
            table.insert(argv, src)
        end

        os.execv("dotnet", argv)

        local bcl_out = path.join(outdir, "dotnetframework4.x")
        os.mkdir(bcl_out)
        for _, f in ipairs(os.files(path.join(bcl, "*.dll"))) do
            os.cp(f, bcl_out)
        end

        print("=> AppMain.dll built at: " .. output)
    end)
    set_menu {
        usage = "xmake build-appmain"
    ,   description = "Compile C# scripts in scripts/AppMain into a DLL via Roslyn"
    ,   options = {}
    }
