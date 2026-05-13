#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ANDROID_DIR="$SCRIPT_DIR"

# ------------------------------------------------------------------
# 1. Cross-compile native libraries via xmake
# ------------------------------------------------------------------
echo "==> Cross-compiling native libs for Android..."

cd "$PROJECT_DIR"

# Build for each ABI
abis=("arm64-v8a" "armeabi-v7a" "x86_64")
for abi in "${abis[@]}"; do
    echo "  -> Building for $abi..."
    xmake f -p android -a "$abi" -m release --ndk="$ANDROID_NDK_HOME" -y
    xmake build volt-ui
    xmake build example-basic
done

# ------------------------------------------------------------------
# 2. Copy .so files into jniLibs
# ------------------------------------------------------------------
echo "==> Copying .so files to jniLibs..."

JNILIBS="$ANDROID_DIR/app/src/main/jniLibs"
mkdir -p "$JNILIBS"

for abi in "${abis[@]}"; do
    BUILD_DIR="$PROJECT_DIR/build/android/$abi/release"
    TARGET_DIR="$JNILIBS/$abi"
    mkdir -p "$TARGET_DIR"
    
    # Copy volt-ui library
    cp "$BUILD_DIR/libvolt-ui.a" "$TARGET_DIR/" 2>/dev/null || true
    
    # Copy example shared library (rename to libmain.so for SDLActivity)
    if ls "$BUILD_DIR"/*example-basic* 2>/dev/null | head -1 > /dev/null; then
        cp "$BUILD_DIR"/*example-basic* "$TARGET_DIR/libmain.so"
    fi
    
    # Copy SDL3 shared library
    cp "$BUILD_DIR/libSDL3.so" "$TARGET_DIR/" 2>/dev/null || true
    
    echo "  -> $abi: $(ls "$TARGET_DIR" | tr '\n' ' ')"
done

# ------------------------------------------------------------------
# 3. Assemble APK via Gradle
# ------------------------------------------------------------------
echo "==> Building APK..."
cd "$ANDROID_DIR"
./gradlew assembleRelease

APK_PATH="$ANDROID_DIR/app/build/outputs/apk/release/app-release-unsigned.apk"
if [ -f "$APK_PATH" ]; then
    echo "==> APK built: $APK_PATH"
else
    echo "ERROR: APK not found at $APK_PATH"
    exit 1
fi
