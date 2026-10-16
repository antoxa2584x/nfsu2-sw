# Building for iPhone, iPad and macOS

This fork adds an Apple host to the NFSU2 Xbox recompilation: a native **Metal** renderer
(MoltenVK/Vulkan as a fallback), touch controls, controller support, in-app game import and
background pause. Everything below happens on **your own Mac with your own copy of the game**:
no game data or lifted code is in this repository, and none may be shared.

What you need:

- An Apple-silicon Mac with **Xcode** (26 or newer) and **Homebrew**, ~15 GB free disk.
- Your own **NFS Underground 2 Xbox disc, NTSC-U** (`default.xbe` SHA-1
  `428b5a70b9400f19cc5be7679db6c72a6f80a1c9`), as an Xbox ISO image.
- For an iPhone/iPad: an Apple ID signed in to Xcode (a free one works; its apps run for 7 days,
  then you reinstall). iOS 17 or newer for the Metal renderer; iOS 16 runs MoltenVK.

## 1. Tools

```sh
brew install cmake ninja sdl2 molten-vk vulkan-loader vulkan-headers vulkan-tools \
             glslang spirv-tools libepoxy openssl@3
python3 -m venv xboxrecomp/.venv
xboxrecomp/.venv/bin/pip install 'capstone<6'
```

## 2. The game files

Extract the disc image into a folder of its own (here `~/nfsu2/game`):

```sh
cd xboxrecomp
.venv/bin/python -m tools.xiso unpack "/path/to/your.iso" -o ~/nfsu2/game
cd ..
shasum ~/nfsu2/game/default.xbe     # must be 428b5a70b9400f19cc5be7679db6c72a6f80a1c9
```

## 3. Lift the XBE to C

Translates the game's executable into C under `gen/` (about 3 minutes; `gen/` is git-ignored and
must never be committed or shared):

```sh
PATH=$PWD/xboxrecomp/.venv/bin:$PATH NFSU2_XBE=~/nfsu2/game/default.xbe \
    NFSU2_GEN_DIR=$PWD/gen tools/regen.sh
```

This step also runs on Windows: see [No Mac? Lift the code on Windows](#no-mac-lift-the-code-on-windows).

## 4. macOS (to try it on the Mac)

```sh
tools/build_ffmpeg_apple.sh macos              # movie decoder (FFmpeg source: see step 5)
cmake -S . -B build-macos-vk -G Ninja -DNFSU2_GEN_DIR=$PWD/gen -DNFSU2_VULKAN=ON \
      -DCMAKE_PREFIX_PATH=/opt/homebrew -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3) \
      -DNFSU2_FFMPEG_DIR=$HOME/nfsu2/deps/macos
cmake --build build-macos-vk
mkdir -p run && cd run
XDG_DATA_HOME=$PWD/data NFSU2_GAME_DIR=$HOME/nfsu2/game ../build-macos-vk/nfsu2_recomp
```

Leave out `-DNFSU2_FFMPEG_DIR` to skip FFmpeg (movies then use the game's own, slower decoder).
`NFSU2_RENDERER=vulkan` selects MoltenVK instead of Metal. A controller connected to the Mac works;
without one, the keyboard is the controller (`NFSU2_KEYBOARD=0` turns that off):

| Keys | Xbox controller |
|---|---|
| Arrow keys | d-pad and left stick (menus, steering) |
| W / S | right / left trigger (gas / brake, reverse) |
| A / D | steer left / right |
| Enter | A and Start together (Press START, OK, select, pause) |
| Esc or Backspace | B (back) |
| Space / Left Shift | X / Y |
| Q / E | White / Black |
| Tab / P | Back / Start |
| I J K L | right stick |

## 5. iOS dependencies

Download into `~/nfsu2/deps/dl/` and unpack under `~/nfsu2/deps/src/` (one folder each):

| Library | Get | Unpack to |
|---|---|---|
| SDL2 2.32.10 | `https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz` | `src/sdl2/` |
| glslang 16.6.0 | `https://github.com/KhronosGroup/glslang/archive/refs/tags/16.6.0.tar.gz` | `src/glslang/` |
| MoltenVK 1.4.2 (device) | `MoltenVK-ios.tar` from `https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2` | `src/moltenvk/` |
| MoltenVK 1.4.2 (Simulator) | `MoltenVK-all.tar`, same release | `src/moltenvk-sim/` |
| FFmpeg 7.1.1 | `https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz` (sha256 `733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1`) | `src/ffmpeg/` |

Then build them for the device (use `sim` instead of `device` for the iOS Simulator):

```sh
tools/build_ios_deps.sh device       # SDL2, glslang, MoltenVK -> ~/nfsu2/deps/device
tools/build_ffmpeg_apple.sh device   # FFmpeg (vp6 only, LGPL, dynamic frameworks)
```

## 6. Build and install on the iPhone

Pick your own bundle ID and Team ID (Xcode → Settings → Accounts):

```sh
cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_PREFIX_PATH=$HOME/nfsu2/deps/device \
  -DCMAKE_FIND_ROOT_PATH=$HOME/nfsu2/deps/device -DNFSU2_GEN_DIR=$PWD/gen -DNFSU2_VULKAN=ON \
  -DNFSU2_FFMPEG_DIR=$HOME/nfsu2/deps/device \
  -DNFSU2_IOS_BUNDLE_ID=com.yourname.nfsu2x -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=YOURTEAMID \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY="Apple Development"
xcodebuild -project build-ios/nfsu2_recomp.xcodeproj -scheme nfsu2_recomp -configuration Release \
  -destination 'generic/platform=iOS' -allowProvisioningUpdates build
xcrun devicectl list devices                                  # your iPhone's identifier
xcrun devicectl device install app --device <UDID> build-ios/Release-iphoneos/nfsu2_recomp.app
```

The first time, allow the developer on the phone: Settings → General → VPN & Device Management.
After a change to a CMake file, run `cmake build-ios` again before `xcodebuild`.

## 7. Put the game on the phone

Either:

- **In the app:** copy your Xbox ISO or game folder to the phone (Files app, AirDrop, or Finder → your iPhone →
  Files → NFSU2), start the app, and pick the folder when it asks. It checks `default.xbe` before copying, streams extraction for an ISO, and
  installs the game while preserving existing saves; or
- **From the Mac:**
  ```sh
  xcrun devicectl device copy to --device <UDID> --domain-type appDataContainer \
    --domain-identifier com.yourname.nfsu2x --source ~/nfsu2/game --destination Documents/game
  ```

## Playing

- **Controls:** a paired controller (DualSense, Xbox, MFi) works and hides the touch controls; without
  one, the Menu overlay appears. The verified PAL profile automatically chooses Drive during gameplay and Menu during pause/options or frontend/loading. The top-centre toggle remains a manual fallback; `NFSU2_AUTO_CONTROLS=0` disables detection. NTSC builds retain the manual toggle.
- **Customize touch controls:** tap **Edit** at the top of the game screen, select **Menu** or **Driving**, then select a button. Change its horizontal/vertical position, size, visibility or action. Opacity applies to that entire layout. A live preview updates while you edit. Tap the navigation Back button to return to the list, then **Save**; **Cancel** discards changes. **Reset menu/driving layout** restores just that profile. Preferences persist in the app. The game, audio and renderer pause while editing, including when switching away from the app.
- **MP3 music:** Edit → **My Music · Import MP3s** → **Add MP3s from Files** accepts multiple MP3 files. Turn on **My Music** to play the imported playlist, use **Next Track** to skip and adjust its independent volume. Files persist in `Documents/Music`; music settings apply immediately, independently of layout Save/Cancel. Playback pauses in the background and on interruptions/headphone removal. This mixes with game audio; turn down the original soundtrack in the game's audio options to retain engine sounds with only your own music.
- **Controller handoff:** connecting a physical SDL-supported controller releases and hides touch input; disconnecting restores it. Edit stays visible. Custom touch remapping affects the mobile pad; controller mappings follow the game/SDL controller mapping. `NFSU2_TOUCH=2` keeps the overlay visible and reattaches it behind physical controllers so they retain port 0.
- **Settings:** `Documents/nfsu2x_env.txt` on the phone (Files app → NFSU2), one `KEY=VALUE` per line,
  the same names as the Linux/Switch builds. Useful ones:

  | Setting | Effect |
  |---|---|
  | `NFSU2_RENDERER=vulkan` | MoltenVK instead of the native Metal renderer |
  | `NFSU2_TOUCH=0` / `=2` | no touch controls / always show them |
  | `NFSU2_FPS_LIMIT=60` | optional presentation ceiling (default 0 = uncapped; accepts 0–1000, invalid values fall back to uncapped) |
  | `NFSU2_SCREEN=fit` / `=fill` | preserve aspect ratio / fill the screen (iOS default: fill, with stretching on wider screens) |
  | `NFSU2_TOUCH_LAYOUT=drive` | start with the driving overlay rather than Menu |
  | `NFSU2_FULLSCREEN=0` | start windowed on macOS (F11 toggles fullscreen) |
  | `RECOMP_GL_SCALE=4` | iOS default: 4× width/height, 16× pixels; lower values reduce GPU cost |
  | `RECOMP_FPS_LOG=1` | fps, GPU command-buffer execution time, CPU, heat and sound health every 10 s in `nfsu2x_log.txt` |
  | `RECOMP_AUDIO_VOLUME=50` | master volume 0–100 |

- The log of the last run is `Documents/nfsu2x_log.txt`; saves live in `Documents/game/UDATA`.
- Leaving the app pauses the game, its sound and all GPU work.

The ISO reader supports bare Xbox game partitions and common full-disc layouts and validates paths and extents before extraction. Each app accepts the executable matching its translated region profile: NTSC-U by default, or the European En/Fr/De/It/Nl/Sv PAL build below. Other executable revisions need their own verified translation and address mapping. The European disc extracts to about 4.45 GB; keep additional space available for the import.

For Apple builds, new CMake build directories default to `NFSU2_GEN_OPT=-O2`. Existing build directories retain their cached setting; pass `-DNFSU2_GEN_OPT=-O2` explicitly. `-DNFSU2_LTO=ON` now uses ThinLTO on Clang/AppleClang. This is opt-in because the full generated game is large; peak link memory and gameplay gains have not been measured.

## No Mac? Lift the code on Windows

Translating the game's code to C (step 3) doesn't need a Mac: it's plain Python and runs the same
under WSL2 on Windows (tested on Ubuntu 24.04: about 2 minutes, identical output on every run). You
still need a Mac, your own or a rented cloud Mac, for everything from step 4 on: Xcode, signing and
installing on the iPhone.

Set up WSL2 with Ubuntu (PowerShell as administrator, then reboot if asked), open **Ubuntu** from
the Start menu and create your Linux user:

```powershell
wsl --install -d Ubuntu-24.04
```

Everything below runs in the Ubuntu terminal. Work under `~`, not under `/mnt/c/...`: Windows
drives are much slower from WSL. Refresh the package lists first, or an older WSL install fails with
"404 Not Found":

```sh
sudo apt-get update
sudo apt-get install -y git python3-venv p7zip-full     # p7zip only if your image is in a .7z
git clone --branch ios https://github.com/myUdav4iik/nfsu2-ios.git ~/nfsu2-ios
cd ~/nfsu2-ios
python3 -m venv xboxrecomp/.venv
xboxrecomp/.venv/bin/pip install 'capstone<6'

cp "/mnt/c/Users/<you>/path/to/your.iso" ~/              # C: is /mnt/c
cd xboxrecomp
.venv/bin/python -m tools.xiso unpack ~/your.iso -o ~/nfsu2/game
cd ..
sha1sum ~/nfsu2/game/default.xbe     # must be 428b5a70b9400f19cc5be7679db6c72a6f80a1c9

PATH=$PWD/xboxrecomp/.venv/bin:$PATH NFSU2_XBE=~/nfsu2/game/default.xbe \
    NFSU2_GEN_DIR=$PWD/gen tools/regen.sh
```

Don't point the system `python3` at another version (for example with `update-alternatives`):
Ubuntu's own tools, `apt` included, need its default Python.

The result is `gen/`, 83 files and about 130 MB. Pack it and copy it to the Mac over a private
route: a USB drive, your local network, or `scp` to a Mac you control:

```sh
tar czf ~/nfsu2-gen.tar.gz -C ~/nfsu2-ios gen
scp ~/nfsu2-gen.tar.gz you@your-mac:~/
```

On the Mac, clone the same branch and commit, unpack it into the repo
(`tar xzf ~/nfsu2-gen.tar.gz -C <repo>`), install the tools from step 1 except the Python venv, and
continue from step 4. To check that the copy arrived intact, compare
`cd gen && find . -type f | LC_ALL=C sort | xargs sha256sum` on Windows with the same command using
`shasum -a 256` on the Mac. The game folder still has to reach the phone (step 7).

> [!CAUTION]
> **`gen/` is the game's own code, translated.** It is derived from EA's copyrighted executable,
> like the disc itself. Never upload or share it: no GitHub (it's in `.gitignore`; keep it there),
> no public cloud links, chat servers or file-sharing sites. Move it only between machines you
> control, and delete copies you no longer need, including the tarball and anything left on a
> rented cloud Mac when the rental ends. The same goes for a built app: it contains this code, so
> everyone builds their own from their own disc.

## Known issues

- Career free-roam radio speech can drop out on the iPhone (fine on macOS); see
  `docs/REGIONS.md`.

## Licences

See `docs/APPLE_NOTICES.md` for the libraries the Apple builds include and their licences.


## European PAL build

Extract your own `default.xbe` from the Europe En/Fr/De/It/Nl/Sv disc. The supported
SHA-1 is `144deb954e0e9208e0b7e08e09b4e5e67b2f3356`. The preparation script verifies
this exact executable before adapting graphics, audio, memory-copy, movie and
matrix hooks. Keep all translation output private and outside the source repo.

```sh
PYTHON=/path/to/venv/bin/python \
NFSU2_XBE=/path/to/pal/default.xbe \
NFSU2_GEN_DIR=/path/to/private-pal/gen \
NFSU2_PAL_WORK=/path/to/private-pal tools/regen_pal.sh
```

Add these options to the macOS/iOS CMake commands above:

```sh
-DNFSU2_REGION=PAL \
-DNFSU2_GEN_DIR=/path/to/private-pal/gen \
-DNFSU2_MANUAL_SOURCE=/path/to/private-pal/recomp_manual.c
```

Use the VP6-only dynamic FFmpeg frameworks for native movie playback. The PAL
profile retains translated routines for unverified NTSC native math overrides;
frame-lag and simulation-cap address patches remain disabled in this profile.
The app imports PAL ISO/folder data using the PAL hash. The NTSC profile keeps its
own hash and overrides. One app does not mix translated executables from different
regions. See `REGIONS.md` for supported executable profiles and compatibility limits.
