# Android port

**Status: beta.** The port builds with the NDK and runs on a phone: the ISO installer, rendering
and the on-screen controls work on the one device it was tried on. Frame rate has not been
measured and other devices are untested. See [Known risks](#known-risks). **PS Vita is only a
plan** and has no code.

## How it works

The Android build is the Linux host (SDL2) packaged as an APK.

1. A launcher screen finds the game ISO and extracts it, then deletes the ISO. Once the game is
   installed it shows **Start** (nothing starts by itself) and **Settings**.
2. SDL's `SDLActivity` loads `libmain.so`. That library is this project: the lifted C, the
   vendored xboxrecomp runtime and `src/android_host.c`.
3. The renderer is **Vulkan 1.1+** (OpenGL ES has been removed). The Vulkan renderer uses
   dynamic rendering when available (Vulkan 1.3 with `VK_KHR_dynamic_rendering`,
   `VK_EXT_vertex_input_dynamic_state`), and falls back to classic `VkRenderPass` +
   `VkFramebuffer` on older devices (Vulkan 1.1, e.g., Dimensity 920 Mali-G68 MC4).

## Requirements

| | |
|---|---|
| Device | arm64, Android 10 (API 29) or newer, Vulkan 1.1+. 6 GB RAM or more. Free storage of about twice the ISO size while extracting. Dynamic rendering path: Vulkan 1.3 with `VK_KHR_push_descriptor`, `VK_KHR_dynamic_rendering`, `VK_EXT_vertex_input_dynamic_state`. Legacy path: Vulkan 1.1. |
| Build host | Gradle 8.9 (wrapper included), Android NDK r27 or newer, CMake 3.22 or newer, git |
| Game | Your own original Xbox NTSC-U game **ISO**. It is extracted on the phone. |
| Lifted C | The `gen/` directory produced by `tools/regen.sh` from your `default.xbe`. It is host independent, so the one built for Linux or Switch works. |

## Building

### Locally

SDL2 release-2.30.9 in /data/data/com.termux/files/home/nfsu2privaterepo/android/deps/SDL
Submodule path 'lib/linkernsbypass': checked out 'aa3975893d83ef1bc84c321ec60c65fbf1287887'
libadrenotools in /data/data/com.termux/files/home/nfsu2privaterepo/android/deps/libadrenotools

Run `android/tools/build_glslang.sh` once; Gradle then builds it in automatically.
`fetch_deps.sh` also fetches libadrenotools, which lets the Vulkan renderer load a custom driver.

CMake switches worth knowing (add them to `arguments` in `app/build.gradle`):

- `-DNFSU2_GEN_OPT=-O1` if clang runs out of memory on the lifted chunks.
- `-DNFSU2_LTO=ON` for ThinLTO.
- `-DNFSU2_ANDROID_TLS_MODEL=initial-exec` to try cheaper thread-local guest registers.

### With GitHub Actions

`.github/workflows/android.yml` builds the APK on a GitHub runner, which has the NDK
preinstalled. It reads `default.xbe` from the root of a branch named `private-assets`, runs
`tools/regen.sh`, builds, and uploads the APK as a workflow artifact. If that branch does not
exist, the job exits without doing anything, so it is harmless in a public repository.

Use it only in a **private** repository. The branch holds a copy of the game executable, and the
lifted C is derived from it. The tail of each build log is pushed to a `ci-logs` branch.

## Game data

On first start the app creates `Android/data/com.nfsu2x.nfsu2/files/iso/` and shows its path.
Put the ISO there:



Or tap **Choose ISO file** and pick it anywhere (it is read in place, without copying). The app
then:

1. waits until the ISO has stopped growing, so a copy in progress is never read;
2. extracts it into `files/game/` with a progress bar, and resumes if interrupted;
3. **deletes the ISO** (for a file chosen with the picker, only when its storage provider allows
   it; otherwise you are told to delete it yourself);
4. starts the game. From then on the app goes straight into the game.

The reader handles trimmed XISOs and raw dumps (partition offsets 0, `0x18300000`,
`0xFD90000`, `0x2090000`). It was tested on images built by `tools/make_test_xiso.py` at those
offsets (`tools/XisoSelfTest.java`), not on a real disc yet. An already extracted disc can be
pushed to `files/game/` instead.



## Controls

- A Bluetooth or USB gamepad (anything SDL's game-controller layer knows) is player 1 and hides
  the overlay. It can be connected while the game runs.
- Without one, the on-screen controls appear: floating left stick, A/B/X/Y, LT/RT, LB/RB
  (Black/White), Back and Start. Landscape only.
- Buttons map by label, like the Switch build (`RECOMP_PAD_LAYOUT=position` for the Xbox
  layout). There is no second player on touch.

## Launcher settings

Settings are saved on the device and written to `files/launcher_env.txt` when Start is pressed
(the game reads it before your own `nfsu2x_env.txt`, which therefore wins).

| Setting | What it does | Environment variable |
|---|---|---|
| Frame rate 30 / 60 / 120 | NFSU2 flips every second vblank in a race, so the display rate the game sees is raised to 2x the target. 60 and 120 are experimental: the game's clock is time based, but this is not verified, and the phone has to keep up. | `RECOMP_VBLANK_HZ` |
| Stretch to full screen | Fills the whole screen and ignores the aspect ratio. Off by default. | `RECOMP_STRETCH=1` |
| Resolution | 0.5x to 3x of 640 x 480. | `RECOMP_GL_SCALE` |
| Vulkan driver | The system driver, or a Turnip package (.zip) added with "Add driver". Press and hold a driver to remove it. | `RECOMP_VK_DRIVER_DIR`, `_NAME`, `_HOOK_DIR`, `_TMP_DIR` |
| Edit controls | Drag the touch buttons, per-button size, size of all, opacity. | (stored in the app) |

Custom drivers use [libadrenotools](https://github.com/bylaws/libadrenotools): the package is the
usual "adrenotools" zip (a `meta.json` with `libraryName`, and the driver `.so`), unpacked into
the app's private storage. It only works on Adreno GPUs, needs the native libraries extracted at
install (the APK is packaged that way) and is untested on devices so far.

## OpenGL ES port (removed)

The OpenGL ES 3 renderer has been removed. The Android port now uses Vulkan exclusively,
with a legacy `VkRenderPass` fallback for Vulkan 1.1 devices (e.g., Dimensity 920 Mali-G68 MC4)
that lack `VK_KHR_dynamic_rendering` and `VK_EXT_vertex_input_dynamic_state`.

For reference, the removed ES 3 port had these differences from desktop GL 3.3:

| Desktop GL | OpenGL ES 3 (removed) |
|---|---|
| `GL_BGRA` uploads | Bytes go in as RGBA and the texture is swizzled R<->B when sampled. Render targets are plain RGBA. |
| BGRA vertex colours | Fetched as RGBA and swapped in the vertex shader (`u_bgra`). |
| `glClearDepth` / `glDepthRange` (double) | The `...f` variants. |
| `GL_DEPTH_CLAMP` | Not available. The vertex shader already clamps clip z. |
| `GL_CLAMP_TO_BORDER` | ES 3.2 only; edge clamp on 3.0 and 3.1. |
| S3TC (DXT) | Through the extension. If the driver refuses, textures are decoded on the CPU, which was already the fallback. |
| `#version 330 core` | `#version 300 es` with `highp` defaults. |

## What changed in the code

| Where | Change |
|---|---|
| `CMakeLists.txt` | Android branch: `libmain.so`, SDL2 built in-tree, Vulkan only (OpenGL ES removed), 16 KB page alignment. |
| `android/.../LauncherActivity.java`, `XisoReader.java`, `IsoInstaller.java` | ISO detection, extraction, deletion, progress UI. |
| `src/android_host.c` | Storage paths, `nfsu2x_env.txt`, logcat and log file, touch-pad state (JNI). |
| `src/main.c` | `SDL_main`, big-stack game thread (as on Switch), unwinder-based crash report (bionic has no `execinfo.h`). |
| `xboxrecomp/.../win32_compat.c` | `sched_setaffinity` (no `pthread_setaffinity_np`), `memfd_create` syscall, `TerminateThread` without `pthread_cancel`. |
| `xboxrecomp/.../nv2a_vk.c` | Vulkan 1.1 legacy RenderPass fallback, surface/swapchain recreation, identity pre-transform, fullscreen window. |
| `xboxrecomp/.../xinput_device.c` | Touch pad as player 1 when no pad is attached, pad hot-plug rescan. |
| `xboxrecomp/.../CMakeLists.txt` | No OpenSSL, null D3D8 layer, in-tree SDL2 on Android. |
| `tools/regen.sh`, `xboxrecomp/tools/*/output.py` | The toolkit's missing `output.py` modules and the XBE analysis JSON step, needed for a clean regeneration. |
| `.github/workflows/android.yml` | The APK build described above. |

## Known risks

Resolved by testing on a device:

- **Clang on the lifted code.** It builds with the NDK's clang.
- **Guest address space.** The windows above 0x80000000 could not be mapped at first. The runtime
  now reserves the whole 4 GB guest space up front (`w32_reserve_guest_space`).

Still open, and not checked on a device:

- **Thread-local registers.** The guest registers are `__thread`. In a shared library that costs
  a call per function, slower than on the Switch's static executable. Try
  `NFSU2_ANDROID_TLS_MODEL=initial-exec`; `PERF_NOTES.md` covers the register-in-locals work that
  already reduces this.
- **Context loss.** When the app is backgrounded SDL may take the Vulkan surface away. The present
  path recreates the swapchain and surface, but a driver that really destroys it would need every
  Vulkan object rebuilt, which is implemented for the swapchain but not for all resources.
- **16 KB-page devices.** The guest's 4 KB page bookkeeping (`guest_vmem.c`) has never run on a
  16 KB host.
- **Backgrounding.** SDL blocks its event pump while the app is paused, which pauses the game
  too. Audio and the guest clock are not paused.
- **Performance.** The Switch build manages about 20 fps in races. A current Snapdragon core is
  faster than a Cortex-A57, but the GPU path is untested.
