# Burnout 3 on Quest 3: research notes (2026-11-18)

How this port came about, what was measured, and what is left. Numbers are from a Quest 3 (Horizon OS, Android 14,
Adreno 740), with Meta's `VrApi` logcat lines and Takedawn's own frame counters.

## The candidates

| | Takedawn (Xbox) | burnout3-pc (PS2) |
|---|---|---|
| Disc | Xbox NTSC-U (`default.xbe`) | PS2 SLUS-21050 |
| How | the Xbox program statically recompiled to C; the NV2A push buffer replayed on Vulkan | the PS2 program lifted to C++; VU1 microcode as GPU shaders, the GS on the GPU, VIF/VU on threads |
| Android | APK, tested on Adreno (Retroid Pocket 5, SD865: 60 fps at 640x480) | APK, tested on Mali (Pixel 7 Pro: ~30 fps on its first night) |
| Cost | the game's code native, no hardware model beside it | about three busy cores on a PC even at 1x |

Takedawn was chosen: the lighter design, the Adreno tuning and the headroom for drawing twice for 3D. (The PS2 port
remains the richer PC port: high frame rates, modern effects.)

## Getting it on the Quest

- Takedawn's APK builds from source on macOS ([building-macos.md](building-macos.md)).
- The bundled Turnip driver fails on the Quest's KGSL (`vkCreateDevice: VK_ERROR_OUT_OF_DEVICE_MEMORY`, then a slow
  readback present). The headset's own driver is Vulkan 1.3.295 (whatever the package manager's feature reports) and
  runs it at 60 fps: Automatic now picks it on a Quest (`GpuDriver.isQuest`).
- Files pushed with adb into `Android/data/<app>/files` are created `rw-rw----` / `rwxrws---` by the shell and are not
  readable by the app: `chmod 644` files, `755` folders.

## The OpenXR screen (`xboxrecomp/src/video/xr_theater.c`)

A C port of [Futurama Quest](https://github.com/mrchrisster/futurama-quest)'s OpenXR theater (placement, window
mode, Touch mapping, haptics, the VR menu), drawn on the GPU:

- `XR_KHR_vulkan_enable2`: the runtime makes Takedawn's one instance and device (`nvk_core_set_xr_hooks`). The
  runtime must be handed `vk_dyn`'s own `vkCreateInstance`/`vkCreateDevice` (they fill Takedawn's function pointers):
  the loader's directly gave a NULL call at the first instance function.
- The presenter's finished frame (game + settings overlay) is drawn into an sRGB quad swapchain image with a render
  pass, sRGB-decoded in the shader so the sRGB attachment stores the original bytes, scaled to at most 1920 wide (the
  immersive window is 4128x2208).
- A frame thread runs `xrWaitFrame`/`xrBeginFrame`/`xrEndFrame` at 120 Hz with passthrough or a dark room, a 64x64
  clear projection layer, the quad and the menu. Queue access by OpenXR calls is under `nvk_queue_lock`.
- Touch controllers are port 0 (`xbox_InputSetXrSource`); the Quest's controllers also appear as an Android gamepad
  (vendor 0x2833), which gets no port while OpenXR input is on. On-screen touch controls are forced off.

## Lessons from GT3 Revisited ([docs/dev/VR.md](https://github.com/matt-pulsipher/GT3-revisited/blob/main/docs/dev/VR.md))

- A transfer copy into a Quest swapchain image costs ~1.6 ms GPU and a ~7 ms CPU stall in the submit; a render pass
  does not. Here: the longest present went from 24.9 ms (copy, full size) to 0.7-1.9 ms (render pass, 1920 wide).
- Ask for sustained-high CPU/GPU levels (`XR_EXT_performance_settings`).
- Ignore the Touch controllers' Android gamepad in VR.
- Their true stereo (eyes from the tracked head at the car's camera, the HUD on its own panel, menus/replays on a
  screen) is the model for an immersive mode here: it needs the RenderWare camera found in the recompiled game.

## The 3D look

Futurama shifts its 3D geometry per eye in its own renderer (`clip.x += eye * s * (clip.w - focus)`). Takedawn
replays the Xbox's push buffer, so the same shift is applied after the fact, from depth:

1. Burnout's own scene marks (`config/patches.json` `hdr_scene` / `hdr_ui`, made for HDR) bracket each view's 3D,
   including its bloom and radial blur. They are emitted while the headset wants 3D (`nvk_xr_scene_wanted`).
2. At the first draw outside the scene into the shown target, the backend copies the target's colour and depth
   aside (`xs_mark`; depth through a buffer into an R32 float image); the flip copies them next to the window slot
   (`xs_flip`, `fbv_slot_scene`).
3. Window z is linear in 1/w, so Futurama's shift is linear in the depth value: `shift = S * (d - f) / (1 - f)`, 0 at
   the focus depth `f` (taken where the car sits in the chase view), S at the sky, negative nearer than the car (held
   to -0.35 S). Each eye looks its pixel up where its shift brings it from (four fixed-point steps).
4. The HUD is what the frame has and the scene lacks (`frame != scene`): it is not shifted and stays on the screen
   plane. Frames with no scene (menus, films) are shown flat in both eyes.
5. S: off / 0.011 / 0.0176 / 0.0253 of the picture's width, split between the eyes. `RECOMP_XR_DEPTH_X=<n>` multiplies
   it for tests (at 10x the far street visibly shifts against the car and the HUD; disocclusion smears show at fence
   posts, a tenth of that at NORMAL).

Cost at the race start: 59 fps instead of 60, GPU 82-83 % instead of 78 % (with the texture upscaler on).

Takedawn's High refresh rate (frames blended between the 60 Hz steps, `RECOMP_GFX_HIGH_REFRESH=1`) works in the
headset in races (it leaves menus and the pre-race at the title's own rate, which is why a test standing at the start
line showed only 56-64 frames a second). Racing with the autopilot, 3D NORMAL:

| Headset rate | Game frames/s | GPU |
| --- | --- | --- |
| 90 Hz (`RECOMP_XR_HZ=90`) | 85.5-89.3 | 70 % |
| 120 Hz | 84.7 | 75 % |

90 Hz with high refresh is the smoothest screen today, and the base a head-tracked mode would pace from.

## Textures

Measured standing at the start line of the first Single Event race (scripted by `tools/quest/texrace.sh`):

| Settings | Game fps | Headset fps | GPU | Upscaled |
| --- | --- | --- | --- | --- |
| default (render scale 1.5, aniso 4x) | 60.0 | 121 | 76 % | - |
| render scale 2, aniso 16x | 60.0 | 120 | 83 % | - |
| NNEDI3 2x, aniso 16x | 60.0 | 120 | 78 % | 150 textures, 154 MB |
| FSRCNNX 2x, aniso 16x | 59.5 | 120 | 79 % | 147 textures |
| NNEDI3 4x, aniso 16x | 53.2 | 120 | 78 % | 86 textures |
| render scale 2 + NNEDI3 2x | 58.7 | 120 | 83 % | 158 textures |
| NNEDI3 2x + 3D NORMAL | 58.9-59.3 | 120 | 82-83 % | 158 textures |

- The world texture upscaler's LGPL shader files (toolkit `third_party/upscale_shaders`) were not in Takedawn's APK:
  `build-apk.sh` now packs them as assets and the game unpacks them to `files/shaders/upscale`.
- An AI pack from your own disc: run once with `RECOMP_DEBUG=texrep_dump=<dir>` and play the tracks you want (a
  track's textures are written when it loads), pull the folder, upscale on a computer, put the PNGs in a mod's
  `textures` folder with `pack.toml` `alpha = "original"`:
  ```sh
  realesrgan-ncnn-vulkan -i texdump -o out -n realesrgan-x4plus -f png
  ```
  The first race's 239 textures took 2.5 min on an Apple-silicon Mac (324 MB of PNG). Packs are capped at 256 MB of
  GPU memory on Android: `RECOMP_DEBUG=texpack_cap_mb=1024` raises it. Packs are made from the game and stay with you.

## Playtests (3D on, the AI pack, Takedawn's autopilot driving: `RECOMP_DEBUG=autopilot=1`)

| Run | Game fps | GPU | Notes |
| --- | --- | --- | --- |
| race start, 3D NORMAL, dark room, aniso 16x | 59.1 | 59 % | |
| race start, + AI pack (239 textures, `texpack_cap_mb=1024`) | 60.0 | 61 % | signs and shop fronts sharper |
| 60 s of racing, 3D NORMAL, AI pack | 60.0 | 60 % | 129-145 mph, no crash |
| 60 s of racing, 3D DEEP, AI pack | 60.0 | 63 % | 143-149 mph, 6276 frames in 3D, no crash |

- Passthrough (mixed reality) costs about 20 % of the GPU against the dark room: 78-83 % vs 59-63 % in the same
  scenes.
- A fresh install (`pm clear`, the setup page from an `.xiso`, then the game) with a seed profile in the APK: the
  profile is copied into the empty save folder and the boot reaches the main menu by itself in 15.7 s.
- Known 3D artifact: thin light slivers beside thin near objects (the fence posts at the start of the first race),
  where the per-eye lookup lands on background between posts. A search over candidate shifts would fix it; the
  three-texel depth minimum does little.
- Files pushed with adb cannot make nested folders under folders the app made (`secure_mkdirs ... Operation not
  permitted`): `adb shell mkdir -p` the folders first, push the files into them, then `chmod`.

## Testing on a Quest without wearing it

- Keep it awake: `adb shell am broadcast -a com.oculus.vrpowermanager.prox_close` (`prox_far` to undo).
- A screenshot of the headset view (one eye):
  `adb shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT`, into
  `/sdcard/Oculus/Screenshots/`.
- Frame rate and GPU: the `VrApi` logcat lines (`FPS=`, `App=` GPU ms, `GPU%`).
- Takedawn's `RECOMP_PAD_LIVE=<file>` presses each line appended to the file (`start:1200`, `a:200`, `down:150`): the
  scripted way to a race (`tools/quest/texrace.sh`), with `TAKEDAWN_AUTOLOAD=0`.
- The game's log: `files/takedawn/logs/run.log` (and `run.tail.log`).

## Open

- A head-tracked immersive mode (GT3's model): the RenderWare camera, per-eye view/projection, the HUD on its own
  panel, high-refresh pacing against `xrWaitFrame`.
- The boot auto-load's seed profile is opt-in (`BO3_SEED_SAVE`) and never in a published APK: a fresh install makes
  its profile in the game once.
