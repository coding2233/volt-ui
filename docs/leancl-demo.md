# Build & Run Guide

## Headless Console Test (for server/CI)

```bash
# 1. Build C++ with leanclr
xmake f --enable_leanclr=y -y
xmake build headless

# 2. Build C# test assembly (use csc from dotnet SDK, target .NET Framework for leanclr)
cd scripts/AppHeadless
CSC="$HOME/.dotnet/sdk/8.0.420/Roslyn/bincore/csc.dll"
BCL=../../deps/leanclr/src/libraries/dotnetframework4.x
dotnet "$CSC" -noconfig -nostdlib -target:library \
  -out:../../build/linux/x86_64/release/AppHeadless.dll \
  -reference:"$BCL/mscorlib.dll" \
  -reference:"$BCL/System.dll" \
  -reference:"$BCL/System.Core.dll" \
  AppHeadless.cs
cd ../..

# 3. Run
cd build/linux/x86_64/release/
./headless
```

## Troubleshooting "ScriptHost::Initialize failed."

The error log now prints an `RtErr` code. Common codes:

| Code | Meaning | Fix |
|------|---------|-----|
| 0x1B (27) | FileNotFound | BCL path incorrect; ensure `deps/leanclr/src/libraries/dotnetframework4.x/` exists |
| other | Various | Check leanclr submodule is initialized: `git submodule update --init --recursive` |

Run with `LOG_TRACE` to see all debug output (already enabled in headless test).
