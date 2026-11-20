# Burnout 3 Quest (Meta Quest 3 Port)

**Burnout 3: Takedown** (Xbox, 2004) runs natively on **Meta Quest 3**, on a **floating 3D screen** in mixed
reality or a dark room, at the game's own 60 frames a second.

This is a Meta Quest 3 (OpenXR / Vulkan) port of [Takedawn](https://github.com/xenophobentx/takedawn) by
xenophobentx, the static recompilation of the original Xbox game for macOS, Linux, Windows and Android, built on the
[xboxrecomp](https://github.com/xenophobentx/xboxrecomp) toolkit. Takedawn's own README is kept as
[README.takedawn.md](README.takedawn.md): everything in it (settings, online play, mods, the other platforms) still
applies.

> [!IMPORTANT]
> Unofficial, non-commercial fan project. Not affiliated with, endorsed by or sponsored by Electronic Arts,
> Criterion Games, Microsoft or Meta. All trademarks belong to their owners.
>
> **You'll need your own copy.** No game data is included in this repository: no disc images, textures, models,
> sound, music, films or saves, and no code generated from the game. You need your own original Xbox disc of
> *Burnout 3: Takedown* (North America, NTSC-U) as an `.iso` / `.xiso` image.

---

## Meta Quest 3 Features

- **Floating 3D screen (OpenXR):** the game on a quad layer in front of you, over passthrough (mixed reality) or in a
  dark room, at 120 Hz composition (each of the game's 60 frames shown twice, evenly).
- **Stereoscopic 3D look, Futurama style:** each eye is made from the frame's 3D scene and its depth buffer, taken
  right before the HUD is drawn (Burnout's own scene marks). Far scenery sits behind the screen, the car on it; the
  HUD and menus float just in front of the nearest point of the scene (a results or pause page is never cut through),
  films stay flat. 3D DEPTH: off / normal / deep / extra.
- **GPU path end to end:** Takedawn's Vulkan device is made by the OpenXR runtime (`XR_KHR_vulkan_enable2`); every
  frame is drawn into the headset's swapchain with a render pass (no copy, no CPU readback), at most 1920 wide.
- **Sharper textures:** the world texture upscaler (NNEDI3 / FSRCNNX, on the GPU) ships in the APK, and Takedawn
  texture packs work (an AI-upscaled pack made from your own disc: [docs/quest/research.md](docs/quest/research.md)).
- **Straight to the main menu:** with a profile saved, the films, title screen and LOAD PROFILE are passed by
  themselves (B takes over).
- **Touch Plus controls and haptics:** native OpenXR bindings; the game's rumble on the controllers.
- **The headset's own Adreno 740 driver** (Vulkan 1.3), chosen automatically on a Quest.

---

## Installing & Playing on Quest 3

1. Build the APK ([Building](#building-for-meta-quest-android-arm64-v8a)) and sideload it with SideQuest, Meta Quest
   Developer Hub or `adb`:
   ```sh
   adb install -r out/takedawn-android-arm64.apk
   ```
   The app id is Takedawn's (`io.github.xenophobentx.takedawn`): a Takedawn APK signed with another key has to be
   uninstalled first (its uninstall dialog offers to keep the data).
2. Copy your disc image (North America, NTSC-U) into the headset's **Download** folder, over USB (the Quest shows up
   as a drive) or with `adb`:
   ```sh
   adb push "Burnout 3 - Takedown (USA).xiso.iso" /sdcard/Download/
   ```
3. In the headset, open **Takedawn** from **Unknown Sources** in your library. The setup page opens as a 2D panel:
   **Choose your Burnout 3: Takedown disc image** → **Downloads** → your image. It is checked in a few seconds (716
   files); then **Start the game**. The headset's own graphics driver is picked by itself.
4. The first start shows the game's films and asks for a profile: choose **YES** to save it and pick **PROFILE 1**.
   From then on the game goes straight to the main menu with your profile (about 15 s).

The library also lists **Takedawn: graphics driver**, the driver page (leave it on Automatic on a Quest).

### Quest Touch Plus Controls

| Touch Plus Input | Xbox Controller / Action |
| --- | --- |
| Left Stick / Right Stick | Left Stick / Right Stick |
| A / B / X / Y | A / B / X / Y |
| Left / Right Trigger | Left / Right Trigger (brake / accelerate) |
| Left / Right Grip (`LB` / `RB`) | White / Black |
| Left / Right Stick Click (`L3` / `R3`) | Left / Right Thumb |
| Left Menu Button | Start |
| **`L3 + R3` (click both sticks)** | **VR menu**: resume, **mode** (screen / immersive beta: head-tracked races, the HUD on its own panel), level horizon, view (16:9 / 4:3 / full), background (dark room / mixed reality), **3D depth** (off / normal / deep / extra), recenter, reset screen, exit |
| **`LB + RB + L3`** | **Toggle 3D on / off live** |
| **Hold `LB + RB` (both grips)** | **Window mode: move the screen with your hands, pull hands apart / together for distance** |
| `LB + RB` + Left Stick Up/Down | Screen distance |
| `LB + RB` + Right Stick Up/Down | Screen size |
| `LB + RB + R3` | Toggle dark room / mixed reality (passthrough) |
| `LB + RB + Y` | Reset screen size and distance |
| `LB + RB + Menu` | Back |

Placement, background and 3D depth are remembered (`files/xr_view.txt`). Takedawn's own settings overlay opens with
Back + Start as on every platform.

### `env.txt` (optional, in the app's `files` folder)

| Line | Effect |
| --- | --- |
| `RECOMP_GFX_ANISO=16` | 16x anisotropic filtering |
| `RECOMP_GFX_TEXTURE_UPSCALE=nnedi3` + `RECOMP_GFX_TEXTURE_UPSCALE_SCALE=2x` | the world textures upscaled 2x on the GPU (holds 60 fps) |
| `RECOMP_XR=0` | no OpenXR: Takedawn's flat Android window |
| `RECOMP_XR_HZ=90` + `RECOMP_GFX_HIGH_REFRESH=1` | 90 Hz with frames blended between the game's 60 steps: 85-89 frames a second in races |
| `RECOMP_XR_HZ=90` | the headset's refresh (default 120) |
| `TAKEDAWN_AUTOLOAD=0` | the title's own boot screens (no auto-load of the profile) |

A file pushed with `adb` into `Android/data/...` must be world-readable (`chmod 644`; folders `755`) for the app to
read it.

---

## Building for Meta Quest (Android `arm64-v8a`)

On macOS (Apple silicon), with the Android SDK, NDK 30, a JDK 17+, CMake, Ninja, Homebrew `bison` and Python 3.10+
(`capstone`). The toolkit is a sibling checkout, as Takedawn has it, on this port's branch:

```sh
git clone https://github.com/mrchrisster/burnout3-quest
```
```sh
git clone -b quest3-xr https://github.com/mrchrisster/xboxrecomp
```

Then, with your own `default.xbe` extracted to `burnout3-quest/Burnout 3 Takedown/default.xbe`, the OpenXR loader
unpacked to `_deps/openxr`, `regen.sh` and `tools/android/build-apk.sh`: every step, the environment and the fixes
macOS needs are in [docs/quest/building-macos.md](docs/quest/building-macos.md).

---

## How it was made

[docs/quest/research.md](docs/quest/research.md): the candidates compared (Takedawn's Xbox recomp vs. the PS2 port),
why Takedawn, what the [GT3 Revisited](https://github.com/matt-pulsipher/GT3-revisited) VR notes taught, how the 3D
works, the texture experiments with their frame rates, and how to test on a Quest without wearing it.

---

## Credits

- **[xenophobentx](https://github.com/xenophobentx)** and Takedawn's contributors ([CONTRIBUTORS.md](CONTRIBUTORS.md)):
  [Takedawn](https://github.com/xenophobentx/takedawn) and its [xboxrecomp](https://github.com/xenophobentx/xboxrecomp)
  toolkit, the Vulkan backend, the Android port and everything this builds on.
- **Ned Heller ([@sp00nznet](https://github.com/sp00nznet))**: [burnout3](https://github.com/sp00nznet/burnout3) and the
  original [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) toolkit Takedawn grew from.
- **[apexJCL](https://github.com/apexJCL)**: the toolkit fork Takedawn's runtime is based on.
- **[GT3 Revisited](https://github.com/matt-pulsipher/GT3-revisited)**: its VR notes (render pass into the Quest's
  swapchain, headset testing over adb, sustained performance levels, ignoring the Touch controllers' Android gamepad).
- **[Futurama Quest](https://github.com/mrchrisster/futurama-quest)**: the OpenXR theater this port's screen, controls and
  menu are ported from.
- Real-ESRGAN (Xintao Wang et al.), NNEDI3 (bjin's mpv-prescalers) and FSRCNNX (igv), for the texture work.
- Criterion Games, for Burnout 3.

Quest port by [mrchrisster](https://github.com/mrchrisster). Licence: MIT, as Takedawn ([LICENSE](LICENSE)); third-party
code and its licences: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
