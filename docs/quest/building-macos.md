# Building the Quest APK on macOS

Tested on an Apple-silicon Mac (2026-11-18). Takedawn's own Android notes ([../android.md](../android.md)) are written
for Linux; these are the differences and the Quest parts.

## What you need

- The Android SDK (`~/Library/Android/sdk`) with build-tools and a platform, and NDK **30** (`30.0.16248370`; r28 or
  newer works).
- A JDK 17+: Android Studio's own is fine (`/Applications/Android Studio.app/Contents/jbr/Contents/Home`).
- CMake, Ninja, `pkg-config`, `meson` (Homebrew).
- **Homebrew `bison`**: macOS's own bison 2.3 cannot build Mesa (`invalid option -- W`); put
  `/opt/homebrew/opt/bison/bin` first on `PATH`.
- Python 3.10+ with `capstone` in the checkout's `.venv` (macOS's python3 is 3.9):
  ```sh
  /opt/homebrew/opt/python@3.12/bin/python3.12 -m venv .venv
  ```
  ```sh
  .venv/bin/pip install capstone
  ```
- The Khronos OpenXR loader for Android, unpacked to `_deps/openxr` (`include/openxr/*.h`,
  `lib/arm64-v8a/libopenxr_loader.so`) from `org.khronos.openxr:openxr_loader_for_android` (an `.aar` is a zip:
  `prefab/modules/headers/include` and `jni/arm64-v8a`). `BO3_OPENXR_DIR` names another place; `none` builds the
  plain Android APK.
- Your own disc's `default.xbe`, in `Burnout 3 Takedown/default.xbe` (the toolkit's `tools.xiso` extracts it from an
  `.xiso`):
  ```sh
  cd ../xboxrecomp && ../burnout3-quest/.venv/bin/python -m tools.xiso get "<your image>.iso" default.xbe -o "../burnout3-quest/Burnout 3 Takedown/"
  ```

## Steps

The environment for every step:

```sh
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home" ANDROID_HOME=~/Library/Android/sdk ANDROID_NDK_HOME=~/Library/Android/sdk/ndk/30.0.16248370 PATH="/opt/homebrew/opt/bison/bin:$PWD/.venv/bin:$PATH" XBOXRECOMP_DIR=../xboxrecomp
```

1. Generate the game's C from your `default.xbe` (35 s; 24,002 functions):
   ```sh
   ./regen.sh --disasm
   ```
2. Once: SDL2, glslang, ncnn, libadrenotools and the Turnip build into `~/.cache/bo3-android-deps`:
   ```sh
   tools/android/fetch-deps.sh
   ```
3. The APK, `out/takedawn-android-arm64.apk`, signed with a debug key made on first use:
   ```sh
   tools/android/build-apk.sh
   ```

## What this branch changed for macOS and the Quest

- `build-apk.sh`: `check-fp`'s `llvm-objdump` from the NDK's host prebuilt (`$HOST_TAG`), not `linux-x86_64`.
- `build-apk.sh`: the OpenXR loader linked and packed; the texture upscaler's shader files packed as assets; a seed
  profile only when `BO3_SEED_SAVE` names one.
- The game activity carries `org.khronos.openxr.intent.category.IMMERSIVE_HMD`; the setup, driver and licence pages
  stay 2D panels.
- The toolkit (`mrchrisster/xboxrecomp`, branch `quest3-xr`, as a sibling checkout like Takedawn's own): the OpenXR
  screen and 3D (`src/video/xr_theater.c`), the runtime-made device (`src/d3d/nv2a_vk_core.c`), the scene snapshot
  (`src/d3d/nv2a_pb_vulkan.c` `xs_mark`/`xs_flip`), the presenter's hooks (`src/video/fb_present_vulkan.c`) and the
  headset's input (`src/input`).
