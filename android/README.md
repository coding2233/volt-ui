# Volt-UI Android Build

## Prerequisites

- **Android SDK** (API 34+) with `$ANDROID_HOME` set
- **Android NDK** (r26+) with `$ANDROID_NDK_HOME` set
- **Java 17+** (for Gradle)
- **xmake** (for C++ cross-compilation)

## Setup

```bash
# 1. Generate Gradle Wrapper (one-time)
cd android
gradle wrapper --gradle-version 8.7

# 2. Build everything (native + APK)
./build-android.sh

# Or build step-by-step:

# 2a. Cross-compile native libs
cd ..
xmake f -p android -a arm64-v8a -m release --ndk=$ANDROID_NDK_HOME -y
xmake build volt-ui example-android

# 2b. Copy .so to jniLibs
mkdir -p android/app/src/main/jniLibs/arm64-v8a
cp build/android/arm64-v8a/release/libmain.so android/app/src/main/jniLibs/arm64-v8a/
cp build/android/arm64-v8a/release/libSDL3.so android/app/src/main/jniLibs/arm64-v8a/

# 2c. Assemble APK
cd android && ./gradlew assembleRelease
```

## Architecture

```
android/
├── build.gradle.kts        # Root Gradle config (AGP 8.4)
├── settings.gradle.kts     # Module settings
├── gradle.properties       # JVM args
├── build-android.sh        # Full build script (xmake + Gradle)
├── gradle/wrapper/         # Gradle Wrapper (generated)
└── app/
    ├── build.gradle.kts    # App module (compileSdk 34, minSdk 24)
    └── src/main/
        ├── AndroidManifest.xml
        ├── java/org/libsdl/app/   # SDL3 Java glue (from SDL release-3.4.4)
        ├── jniLibs/               # Built .so files (in .gitignore)
        └── res/values/strings.xml
```

The app uses SDL3's standard `SDLActivity` as its entry point, which loads
`libmain.so` and calls `SDL_main()` — the standard SDL3 Android bootstrap.

Native C++ code for the Android target is in `examples/android/main.cpp`.
