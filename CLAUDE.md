# NFSU2 (Xbox) static recompilation — notes for Claude

Xbox NTSC-U Need for Speed: Underground 2, lifted to C with xboxrecomp and
built for Linux and Nintendo Switch (libnx NRO). This repo is
https://github.com/antoxa2584x/nfsu2-sw (`main`, commits as
`Anton Artemov <antoxa2584@gmail.com>`). On 2026-09-29 it replaced the old PS2
port there (history backed up in `/root/nfsu2x/nfsu2-sw-ps2-backup.bundle`).
The toolkit is vendored in `xboxrecomp/`; the default for `XBOXRECOMP_DIR`.

## Rules

- **Renderer work goes into Vulkan only** (nv2a_vk.c, user's call
  2026-10-06). Don't spend time on nv2a_gl: no new features or fixes there.
  Shared shader code (gl_psh.c/gl_vsh.c) may get VK-only paths
  (`nv2a_shader_vk`); keep the GL output unchanged.
- Never commit game data (disc, `default.xbe`, `switch_sd/`) or generated C
  (`gen/`). Ask before committing or pushing anything.
- The Switch build reads the **unpacked** disc at `sdmc:/switch/nfsu2x/game/`,
  never the ISO.
- Don't launch or kill Eden unless the user asked for an Eden check; check
  `tasklist.exe | grep -i eden` first. Eden's `sdmc\switch` is a junction to
  `nfsu2-xbox\switch_sd\switch`, so an Eden run overwrites the log
  there — back up a hardware log before running Eden. Use
  `run_eden.sh` (never a bare `taskkill /F`): it closes Eden politely,
  force-kills only after 15 s, and restores eden.exe/qt-config.ini from
  `/root/nfsu2x/eden_backup/` if a hard kill damaged them. `NRO=x.nro` runs
  another NRO from `switch/nfsu2x/`. The user drops real
  console logs in `switch_sd/switch/logs/`.
- One test at a time on Linux (runs share `fb/`, `gfb/`).
- Never `pkill -f` a pattern that also matches your own command line (it
  kills the shell, and `run_eden.sh` then force-closes Eden).
- Clean up every Linux test run. `pkill -x nfsu2_recomp` does NOT match (the
  process renames itself), and `timeout`/`xvfb-run` leave orphans that keep
  burning CPU; kill by PID:
  `ps -eo pid,args | awk '$2 ~ /nfsu2_recomp$/ || $2 ~ /^Xvfb$/ {print $1}' | xargs -r kill -9`
  (it ignores SIGTERM; `rtk ps` output hides it -- use `rtk proxy ps`)

## Layout and builds

| Where | What |
|---|---|
| `/root/nfsu2x/` (WSL) | `game/` extracted disc, `gen/` lifted C, `build-pr128/` Linux, `build-switch/` Switch, `dis.py ADDR [+N]`, `snap.sh SECS ENV=..` (gdb stacks, `BIN=`), `run_eden.sh SECS` |
| `xboxrecomp/` | vendored toolkit = xboxrecomp main + PR #128 + all port changes (copied from the `/root/nfsu2x/xboxrecomp-pr128` worktree; keep the two in sync) |
| `src/main.c` | boot; defaults RECOMP_VBLANK=1, RECOMP_AC97_READY=plain, RECOMP_USB=1, RECOMP_PB_EXEC=1; APU at 0xFE800000 via `xbox_MmioRegister`; Switch runs the game on a 16 MB pthread |
| `src/recomp_manual.c` | memmove ×2 (0x2A7EE0, 0x2A9450), AC97 reset 0x33518D, DSP ack wrapper 0x32EB65, D3D fence wrapper 0x2E8F20, VP6 movie frames 0x2618F0 (-> src/movie_vp6.c, FFmpeg) |
| `src/switch_nx.c` | log, `nfsu2x_env.txt`, exception handler, t= stamps |

- Regenerate C: `tools/regen.sh` (`LIFT_ONLY=1` after manual-override edits;
  full run after seed changes). Passes `--mmio-sections DSOUND,XPP`.
- Switch: `XBOXRECOMP_DIR=/root/nfsu2x/xboxrecomp-pr128 bash platform/switch/build.sh`
  (without `XBOXRECOMP_DIR` it picks the main checkout and fails on OpenSSL).
  The copy step fails with "Permission denied" while Eden has the NRO open.
- Linux: `cmake --build /root/nfsu2x/build-pr128 -j8`; run with
  `NFSU2_GAME_DIR=/root/nfsu2x/game xvfb-run -a …/nfsu2_recomp`.
- Menu pad script (Linux): `RECOMP_PAD_SCRIPT="10000:start:300,…,70000:start:300,80000:a:200,90000:a:200"`
  (times from the first pad read). `RECOMP_GL_DUMP=<prefix>,N` dumps frames.
- Header changes in `templates/runtime/recomp_types.h` must be copied to
  `gen/recomp_types.h` (regen does it) and rebuild all generated code.

## Switch (Horizon) findings

- **Memory:** `svcCreateSharedMemory` → 0x4201 in Eden and may kill the
  process on hardware; `svcMapPhysicalMemory` → 0xFA01. Guest RAM uses code
  memory (`svcCreateCodeMemory` + `svcControlCodeMemory(MapOwner)`) — the
  default; `RECOMP_NX_SHM=1` opts into shared memory. Code memory cannot
  alias, and the title needs aliasing: it reaches RAM at 0x0 and at the
  0x80000000 contiguous window, and on hardware read a stale 0xAAAAAAAA
  pointer through 0x80xxxxxx after Start (crash, exception 257). Second
  views of a mapping now try `svcMapProcessMemory` on our own process
  (log: `[NX] aliasing views with svcMapProcessMemory` or `... refused
  (0xRC)`). If that is refused, the fallback is folding 0x80000000–0x83FFFFFF
  onto low RAM in `XBOX_PTR` *and* the runtime's translations.
- **Logging / boot time:** the SD log was the boot bottleneck — the runtime
  `fflush(stderr)`s after many lines and each flush was an SD write (console:
  ~33 s to display mode; Linux 0.3 s; Eden 5 s). Now stdout+stderr go to a
  `log:` devoptab (switch_nx.c) that appends to RAM with a `[  t.ttt]`
  timestamp per line; a thread writes it to the card every 0.5 s (crash
  handler drains it). Eden boot to display mode dropped 5 s → 2 s.
- **Loading screen:** NFSU2 logo (`assets/nfsu2_logo.png`, SteamGridDB →
  `tools/make_logo.py` → `src/nfsu2_logo.h`, RLE) + a moving bar, drawn on the
  SDL window/GL context that the renderer then adopts
  (`nv2a_gl_adopt_window`), and kept on presents until the title's first real
  draw (`nv2a_gl_draw_placeholder`, `s_drew_any`). A libnx framebuffer can't be
  used: `SDL_CreateWindow` hangs forever after one was open. `NFSU2_LOADER=0`
  disables it.
- **Unaligned atomics:** x86 `lock cmpxchg`/`xadd` on unaligned addresses are
  legal; AArch64 atomics fault (exception 259 = 0x103 unaligned data). NFSU2's
  XNet `sub_0030A496` ORs a field at …306 after Start. `RECOMP_ATOMIC_*` on
  aarch64 fall back to a spinlock for misaligned addresses. Eden does not model
  the fault — only hardware shows it.
- **Threads/cores:** libnx creates every pthread at priority 59 (the only
  priority Horizon time-slices on cores 0-2) with the process core mask, but
  preferred core = default core 0. `xbox_nx_spread_thread()` deals preferred
  cores 0-2 round-robin (log: `[NX] threads spread over core mask 0x7`).
  Never put threads on core 3: Eden reports mask 0xF, and core 3 does not
  time-slice 59 — busy threads there starved each other (log stopped at 40 s).
- **Hardware boot hang after GPU trap 8** (`NOP(1825046561)`, ~33 s): main
  guest thread (stack 0x00F7Fxxx) stops making kernel calls, GPU idle, never
  reaches D3D's `in al,dx` / `SetDisplayMode`; one run in two. Cause not yet
  found. `nfsu2x_env.txt` with `RECOMP_WATCHDOG_SECS=50` +
  `RECOMP_WATCHDOG_KEEP=1` snapshots the main thread without exiting.
- **Guest threads race (the crash/freeze after Start):** NFSU2's stream
  system hands a freshly allocated block (allocator fill 0xAA) to its worker
  before filling it in; parallel guest threads read 0xAAAAAAAA as a pointer
  (`sub_00072F90`, `sub_0025337B`). Saves live in `game/UDATA`, not `save/`
  (`partition1\UDATA` → game dir). Fix: **the guest lock** (kernel_bridge.c,
  `RECOMP_GIL=0` disables) — one thread runs lifted code at a time; released
  in every kernel call, every 64 poll-loop turns (`recomp_spin_yield`), and at
  lifted function entry when someone waited >1 ms (`RECOMP_PREEMPT`, emitted by
  the translator, skipped in ISR/DPC and at IRQL ≥ DISPATCH). Pinning guest
  threads to one core (`RECOMP_GUEST_ONE_CORE=1`) also fixes it but Horizon's
  10 ms slices cost ~90% speed. Console profile for tests: copied over MTP
  (PowerShell Shell.Application, "Цей ПК\Nintendo Switch\microSD card") into
  `/root/nfsu2x/game/UDATA/4541005a/005413381036`.
- **Atmosphère fatal `std::abort()` in program 010041544D530000 (ams.mitm)**
  and whole-console freezes after save checks: leaked directory handles.
  `NtQueryDirectoryFile` kept a `DIR*` per handle and only closed it when a
  scan reached the end; NFSU2 stops early and closes the handle. Each leak is
  an SD session in ams.mitm, which aborts after enough. Fixed:
  `xbox_dir_forget()` on NtClose (kernel_file.c / bridge_NtClose). Check on
  Linux: `ls -l /proc/<pid>/fd | grep UDATA` stays empty.
- **Hang starting Quick Race / Career** (after the transmission pick or the
  career intro): `PersistDisplay` (0x2EA9C0) waits for D3D's pending flips,
  and `BlockOnTime` (0x2E8F20) waits for a `NOP(5)` trap event. Fixed chain:
  DPC queue locked + `KDPC.Inserted` (ISRs queue from several threads);
  vblank bits held until D3D's DPC reads them; the PGRAPH (0x2F22F0) and
  vblank (0x2F1D80) handlers wrapped in recomp_manual.c at DISPATCH, taking
  traps through a lock handshake with the executor and reporting retired
  flips; `FLIP_STALL` waits for a retire (read != write); `DMA_GET` is
  published as the executor walks. Frame rate is now vblank-locked.
- **Red races / black loading screen** (renderer, nv2a_gl): the race colour
  grade samples two LUTs with DEPENDENT_AR / DEPENDENT_GB texture modes
  (implemented in gl_psh.c, source stage from SHADER_OTHER_STAGE_INPUT), and
  the final combiner's C0/C1 are SPECULAR_FOG_FACTOR0/1, not stage 0's. The
  loading screen's quads sit at z = 1.0 and GL clipped them: GL_DEPTH_CLAMP.
  Headless Linux tests: `SDL_VIDEODRIVER=offscreen` (no Xvfb) works.
- **Grey squares in rain** (lens drops, `sub_000A6530` "Rain Drop"): per
  drop it copies 32x32 of the back buffer into a 32x32 target (0x2BEE080)
  and draws RAINDROPTEST with DEPENDENT_GB into that copy. The copy pass's
  final combiner is FOG.a*C0 + (1-FOG.a)*R0 with fog from specular alpha;
  its FVF has no specular, and D3D sets `SET_VERTEX_DATA4UB(4)=0` for it.
  The renderer fed absent attributes (0,0,0,1) -> fog 1 -> copy = C0.
  Fixed: executor tracks SET_VERTEX_DATA* per attribute
  (`Nv2aRawBatch.attr_const`). Repro on Linux: Career, idle in the open
  world; rain starts after 4-10 min (random). `RECOMP_GL_WATCH=<va>` dumps
  draws into/sampling a VA with pixel readbacks.
- **Lights through walls** (car lights, neon, street lamps): the light
  flare pass (`sub_000AC560`, "eRenderLightFlares") switches colour target
  (0x3E9E0C) but keeps the scene's Z buffer; nv2a_gl kept one depth buffer
  per colour surface, so flares tested against empty depth. Depth is now
  per zeta address and stored size (`depth_get`, `RECOMP_GL_SHARED_Z=0` old
  behaviour). Linux race frames confirmed.
- **Slow-motion races** (below 20 fps): sub_001890C0 caps game time per
  frame at 3.0 (.data float 0x3A4C64, read only there) x 1/60 s = 50 ms and
  drops the rest -> at 14 fps the game ran at ~70% speed. main.c raises the
  cap at boot: `NFSU2_SIM_STEPS` (1/60 s units, default 6 = 100 ms; 3 =
  original). Linux race pinned to one core (~10 fps): race clock 50% -> 90%
  of wall time.
- **FPS drops on crashes** (player/AI hitting traffic or walls): crash
  sounds start many voices, the APU runs behind, and its frame thread held
  `d->lock` without a break (throttle() releases it only when ahead, 1 frame
  in 8; Horizon mutexes are unfair). DirectSound voice commands (fe_method ->
  voice_lock) waited for it holding the GIL and the dispatch lock: console
  profile at a crash had the main thread 48% in GIL waits, the EA mixer 50%
  on those locks. Fix: `mcpx_apu_lock_guest` counts waiters and the frame
  thread hands the lock over after every frame (apu_core.c / apu_state.h).
  That handover then made the APU wait for the caller to be scheduled: race
  profile 71.6% of the APU thread in that wait, APU 1124-1480 frames/s ->
  audio stutter. voice_lock now sets its bits atomically, without d->lock
  (the handover remains only for apu_mixer_play).
- **Light line along the top and left edge** (open world/races, some
  weather; fixed 2026-10-02, GL and VK): the glow passes draw a clip-space
  full-screen triangle, and D3D's viewport offset is 320.53125/240.53125, so
  its edge sat at 0.53125 > GL's pixel centre 0.5 -> row/column 0 of the
  glow accumulator (0x82C17280) were never rewritten, and the composite
  added that stale edge to screen row/column 1. NV2A snaps screen positions
  to 1/16 px by truncation (xemu roundScreenCoords): `nv2a_snap` in
  gl_vsh.c's nv2a_clip.
  Came back with RECOMP_GL_SCALE > 1 (fixed 2026-10-05): the snap is in
  title pixels, but real pixel 0's centre is at 0.5/k there, so the 0.5
  edge still missed it. `u_surf.w` = 0.5 - 0.5*w/pw shifts positions so
  each title pixel's first real centre sits on i + 0.5. A/B at 2x: real
  row/column 1 mean 170 vs 30 without, flat with.
- prof.bin sample times are 16 bits of 10 ms and wrap every 655 s;
  prof_report.py unwraps them (--time on long runs was empty before).
- **Files:** no `open()` on directories (`XBOX_DIR_FD` sentinel); FAT can't
  hold sparse files (partition images created empty, size reported).
- **Save load/create froze the whole console** (log stops right after
  `SaveMeta.xbx` opens; Eden/Linux fine): read()/write() handed guest RAM
  (code memory) to the fs service over IPC; the save code's small unaligned
  reads (2, 280 bytes) jammed it. Fixed (confirmed on hardware 2026-09-28):
  on `__SWITCH__` NtReadFile/NtWriteFile go through a .bss bounce buffer
  (`host_read`/`host_write`, kernel_file.c). Never pass guest memory to a
  Horizon IPC buffer.
- **USB:** the title starts its USB driver late on hardware (25–130 s); the
  OHCI thread must never give up waiting (it used to stop at 30 s → no pad).
  `OHCI_TICK_MS` is 4 ms.
- **Present:** Switch Mesa's scaled, flipped `glBlitFramebuffer` writes outside
  the destination rect (edge columns smeared into the pillarbox bars) → blit
  is scissored to the picture rect. Clear on presents with no surface too.
- **Widescreen (default on, `RECOMP_WIDESCREEN=0` for 4:3):** NFSU2 has a
  real 16:9 mode (anamorphic 640x480, wider FOV, HUD in the 4:3 safe area,
  movies pillarboxed by the game). It needs `XC_VIDEO` in EEPROM layout
  (`XGetVideoFlags` 0x21AEA8 returns `(v >> 16) & 0x5F`, widescreen =
  0x00010000) *and* the same bit in the `AvSendTVEncoderOption(6)` word —
  D3D's mode search (0x2F1B06) fails CreateDevice on a widescreen present
  without it (boot stalls, no window). The presenter then stretches to 16:9.
- **Launch:** title override (hold R on a game) for full memory; applet mode
  has ~400 MB.
- **Rumble:** XAPI sends the 6-byte XID output report (00 06, left/right
  motor LE16) on interrupt OUT ep 2 (or class SET_REPORT); ohci.c hands it to
  `usb_gamepad_output` → `xbox_InputSetState` → libnx HD rumble on Switch
  (`xbox_nx_pad_rumble`: left = low band 160 Hz, right = high band 320 Hz,
  handheld + player 1), SDL rumble on Linux. Only changes are sent.
  `RECOMP_RUMBLE=0` off, `RECOMP_RUMBLE_TRACE=1` logs each change.
- **Render scale:** `RECOMP_GL_SCALE=<k>` (0.5..4, fractions like 1.5 ok;
  nv2a_gl.c and nv2a_vk.c) stores every surface at that multiple, rounded
  (`GlSurf/VkSurf.pw/ph`; `w/h` stay the title's pixels
  for shaders, clips and lookups); viewport, clear scissor, read-backs,
  `RECOMP_GL_DUMP` and the present blit use the stored size. Capped per
  surface by GL_MAX_TEXTURE/RENDERBUFFER_SIZE (Vulkan: maxImageDimension2D/
  maxFramebuffer*).
- **Switch wording** (src/text_patch.c): the English string table (chunk
  0x39000 in ZZDATA2/5: {0x10, count, table, pool}, {hash, offset} sorted by
  hash, packed pool) is rewritten as NtReadFile delivers it
  (`xbox_file_read_hook`): START -> +, replay "Black" -> R, Xbox Live /
  hard disk / Dashboard / Gamertag / Thumbstick -> neutral names. The pool
  is rebuilt and must not grow. On by default on Switch, `NFSU2_SWITCH_TEXT`
  =0/1. Button icons ($JOY_EVENT_...$ tokens) are textures, not covered.
- **Second pad / split screen:** ohci.c now carries two USB pads on HC0
  (root ports 1 and 2), routed by the ED's function address; pad 2 is
  plugged/unplugged as host pad 1 (Switch No2, SDL's second pad) comes and
  goes, after pad 1 is addressed. Switch: player 1 = handheld + No1, player 2
  = No2, but with No2 empty handheld and No1 are two players -- without the
  controller applet a second Joy-Con pair joining a handheld console lands in
  No1 (it used to be player 1 too: "+ on the second pair does nothing").
  Log `[NX] pads: player 1 = ...; player 2 = ...` on every change; `RECOMP_NX_JOYCON=single` = one sideways Joy-Con per player
  (`RECOMP_NX_JOYCON_ROTATE=0` if the stick comes out turned twice).
  `RECOMP_USB_PADS=1` = one pad; `RECOMP_USB_PORT2` moves pad 2.
  `RECOMP_PAD2_SCRIPT` scripts pad 2 (and keeps it plugged); log `PADn: step`.
  NFSU2: Main Menu -> right -> "2 Player Split-Screen"; at car select the
  players are whoever presses **Start** first ("Player One/Two Press START",
  A does not count). **Black 3D in split screen (fixed 2026-10-01, GL and
  VK):** each player's view is drawn with SET_SURFACE_CLIP = its 640x240
  half, the HUD with 640x480. Surfaces were keyed by (address, clip size),
  so every clip change rebuilt -- and cleared -- the back buffer; and the
  clip was not a scissor. Now a surface is clip origin + size and only
  grows, draws are scissored to the clip (like xemu), and a render target
  sampled as a smaller texture gets its texcoords scaled (`rt_scale`).
  Menu dumps run ~60 frames/s: RECOMP_GL_DUMP=<p>,61 is about one a second.
- Buttons map by label (Switch A = Xbox A); `RECOMP_PAD_LAYOUT=position`
  swaps to Xbox positions. Y opens the in-game Help box, closed with B.

## Vulkan build (nfsu2x-vulkan.nro, started 2026-09-29)

- Renderer `xboxrecomp/src/nv2a_vk/nv2a_vk.c` (CMake `NFSU2_VULKAN=ON` ->
  `XBOXRECOMP_VULKAN`, replaces nv2a_gl): Vulkan 1.3, dynamic rendering,
  push descriptors, dynamic vertex input, everything but blend/colour mask/
  depth format/topology class as dynamic state; all images in GENERAL,
  barriers between passes; 2 frames in flight with a 48 MB host ring each.
  Shaders are gl_vsh.c/gl_psh.c with `nv2a_shader_vk = 1` (std140 blocks at
  bindings 0/1, samplers 2..5, Vulkan 0..1 clip z), compiled by glslang on a
  4 MB-stack thread. No threaded submission or loading screen yet.
- Switch: `VULKAN=1 XBOXRECOMP_DIR=/root/nfsu2x/xboxrecomp-pr128 JOBS=6 bash
  platform/switch/build.sh` (build dir /root/nfsu2x/build-switch-vk). Links
  mesa-switch's static NVK from `/root/nfsu2x/mesa-sdk/usr/local` (commit
  1a8c1a66d6f + StevensND/nfsmw-nx `mesa/mesa-switch-nfsmw.patch`, built in
  /root/nfsu2x/ref/mesa-switch: `. /root/nfsu2x/mesa-extra/env.sh; ninja -C
  builddir-switch src/nouveau/vulkan/libvulkan.a; meson install -C
  builddir-switch --destdir /root/nfsu2x/mesa-sdk` -- the cross/native files
  and rustc/bindgen wrappers are in /root/nfsu2x/mesa-extra) and glslang
  15.4 built for the Switch in /root/nfsu2x/glslang-switch. switch-sdl2's
  EGL/glapi/drm_nouveau are filtered out of SDL2::SDL2 (a second Mesa);
  SDL's unused EGL calls are stubbed (switch_egl_stubs.c), libelf too.
- Linux: `-DNFSU2_VULKAN=ON` build in /root/nfsu2x/build-vk-linux; headless on
  lavapipe: `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
  RECOMP_VK_HEADLESS=1` (+ `RECOMP_VK_VALIDATION=1`), frames via
  RECOMP_GL_DUMP. Quick Race renders like GL there.
- /root/nfsu2x/vktest/ (vktest.nro): minimal NVK clear-screen check.
- Caches (like GL's progcache.bin): progcache.bin records are shared with
  GL; vkspirv.bin (glslang output by GLSL hash), vkpipes.bin (pipeline
  variants), vkpipecache.bin (VkPipelineCache, saved at most every 20 s when
  pipelines were added). All built at boot; RECOMP_PROG_CACHE=0 disables.
- Audio still behind in races after the voice_lock change (console
  2026-09-29: APU 1304-1467 frames/s, frame thread 81% in a cond wait, only
  ~14% working). `[perf] APU pacing:` (switch_nx.c, apu_core.c throttle
  counters) shows waits asked vs slept, clock restarts and lost ms.
  Cause found (2026-09-30 log: 0 clock restarts, ~1950 blocks/10 s = 1560
  frames/s of clock, but only ~1250-1300 real frames): while the front end
  was TRAPPED/HALTED the loop ran silent monitor frames and advanced the
  clock -- ~17% silence in races. Now time stops while trapped (xemu does
  the same) and throttle() catches up; the pacing line counts traps.
- **FPS drop on traffic collisions, part 2** (2026-09-30, Vulkan, APU fixed):
  crash frames ~100 ms vs ~45. Main thread per frame: game code ~59 ms vs
  20 (the crash physics chain sub_001F1260 > sub_001C1510 > sub_001B4CD0 >
  sub_001BAA90 > sub_001BAB00/001B2FF0/001B6700: 2% -> 15-19% inclusive,
  self time spread thin, no single hot leaf) and GIL wait ~32 ms vs 7; the
  EA mixer 37% blocked on the dispatch lock (KfRaiseIrql, GIL released) --
  someone at DISPATCH holds both, probably DirectSound's trap ISR/DPC (382
  traps/10 s). `RECOMP_NX_PROFILE=2` samples threads over 2% to catch it.
  NFSU2_SIM_STEPS=3 A/B: no difference (worst 102 vs 98 ms) -- not the cap.
  Profile with RECOMP_NX_PROFILE=2: kernel_timer_thread ~40% waiting for the
  GIL inside DirectSound's DPC (sub_0032BFF0/0032C012: KfRaise/LowerIrql
  around every voice-list update), holding the dispatch lock meanwhile (the
  mixer's 37%). Two bugs: (1) every IRQL call was a GIL handover; (2)
  recomp_preempt read the *global* IRQL, which the timer thread raises
  before queueing for the GIL, so the holder never yielded to the DPC.
  Fixed: IRQL ordinals 103/129/130/160/161 keep the GIL (irql_transition
  try-locks the dispatch lock, suspends the GIL only if contended);
  per-thread `xbox_thread_holds_dispatch()` for pre-emption; a GIL waiter at
  DISPATCH sets g_gil_contended at once.
- nfsmw-nx is GPLv3: take ideas, not files.
- **Vulkan render bugs (2026-09-30):** (1) front face was swapped: NV2A
  winding is on-screen, top-first rows = Vulkan's framebuffer, so CW -> CW
  (only nv2a_gl swaps, its window y is flipped). Every culled draw lost its
  front faces: headlight glass / grille in the main menu, walls, objects.
  (2) the VK prelude clamped clip z to [0, |w|], so vertices behind the eye
  (w < 0) got z = 0 instead of ~w and skewed depth across the eye plane;
  now clamped only when w > 0. Found on lavapipe by reading one pixel back
  after every draw (GL vs VK, same frame) -- an ad-hoc probe, not in tree.
- **Black loading screen main menu -> Career / Quick Race (fixed
  2026-10-01, GL and VK):** nv2a_pb_exec_method wrote methods of every
  subchannel into the 3D register shadow. The loading screen's draws then
  had culling on (cull FRONT, front CW -- the menu car's state; menu 2D
  draws have it off), and its quads are CW, so all were culled. Likely
  culprit: NV062 SET_OFFSET_SOURCE = 0x0308 = SET_CULL_FACE_ENABLE from a
  blit (not traced per method). The old swapped VK winding had hidden it.
  Only subchannel 0 goes into the shadow now.
- **Instancing (2026-10-05, nv2a_vk.c, `RECOMP_VK_INSTANCE=0` off, `=2`
  alternates per frame for A/B dumps):** ~25-30% of race draws repeat the
  previous one with only the transform constants changed. The first is
  recorded up to vkCmdDrawIndexed and held (`s_pend`); repeats append their
  192 constants behind it in the ring (binding 6, `c[nv2a_ib + i]`,
  nv2a_ib = gl_InstanceIndex * 192, up to 16); any other draw/clear/flip
  issues it with the count. "Repeat" = no register change except the
  upload windows/per-draw methods (executor dirty blocks), same program,
  vertex pointers, indices, textures, and same `Nv2aRawBatch.vtx_epoch`
  (bumped at semaphore releases, traps, inline batches). Drag start line on
  lavapipe: 1790 -> 1250 draw calls, 21.2 -> 24.5 fps; A/B frames identical
  but the timer digits; validation clean. `RECOMP_DRAW_STATS=1` logs
  `[vk] per frame: draws -> draw calls`.
- **Car reflections = cube maps (2026-10-06, Vulkan only):** car paint and
  glass use texture mode 3 (CUBE_MAP) on stage 1 (shader program 0x01061),
  which read black before. SET_TEXTURE_FORMAT bit 2 = cube; faces +X..-Z
  follow each other, each with its mip chain, padded to 128 bytes. Menu:
  static 256x256 cube from disc (0x812E7200). Races: a dynamic 128x128
  cube (0x83095680) the title renders as six 128x128 surfaces (face stride
  0x10000) every frame; nv2a_vk assembles it from those surfaces with
  vkCmdBlitImage when a face's `VkSurf.gen` changed (`cube_from_surfaces`).
  gl_psh.c emits samplerCube + texture(tN, vTN.xyz) only for VK; GL still
  black. Lavapipe menu/race frames show reflections, validation clean.
- **Off-screen batches skipped in the executor** (2026-10-05, nv2a_pb_exec.c,
  `RECOMP_CULL=0` off, `RECOMP_CULL_CHECK=1` verifies each rejection with
  the CPU interpreter, `RECOMP_CULL_TRACE=1` per program): the box of
  attribute 0 through the transform at 8 corners; vertex programs are
  sliced to what oPos.xyw needs and typed (affine / linear-fractional), the
  XDK form (dp4 rows + rcc + mul + mad) evaluated directly. Only ~5% of the
  drag-line draws (the "half draw nothing" are occluded, not off screen);
  ~0.4 us/draw on x86.
- **Eden cannot run NVK** (2026-09-29): instance, device, swapchain (only
  IMMEDIATE; FIFO creation hangs) and command recording work, but no GPU
  submission ever completes -- vktest's first fence times out (also with
  NVK_SWITCH_MAPPED_COMPLETION=false) and present then blocks. The game
  stops at its first flip. Test the Vulkan build on hardware.
  `RECOMP_VK_TRACE=<n>` + `NFSU2_LOG_SYNC=1` log each renderer step.

## Audio (host output)

- Linux/Switch output is SDL2 (`SDL_QueueAudio`) behind the `xa2_*` API
  (POSIX half of apu_xaudio2.c); log `[AUDIO] SDL <driver> output`. The APU
  still paces by wall clock and only feeds the device (waiting on the queue
  crawled under WSLg PulseAudio and slowed boot). `RECOMP_AUDIO=0` off,
  `RECOMP_AUDIO_BLOCKS` queue depth (default 8, Switch 12), `RECOMP_AUDIO_VOLUME`
  0..100. Capture on Linux: `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw`
  (48 kHz s16 stereo, real-time paced); `dummy` for tests without sound.
- **Console audio crackle/hiss (2026-09-30, fix awaiting hardware test):**
  switch-sdl2 plays through audren with two 1024-sample wave buffers; its
  audio thread asks for TIME_CRITICAL, which SDL's Switch port maps to 59
  (time-sliced) -> late refills, audren plays gaps. Linker `--wrap`
  (CMakeLists.txt, switch_nx.c) sets it to 0x2B (`RECOMP_NX_AUDIO_PRIO=0`
  off) and counts gaps: `[perf] audio out: ... device gaps, worst ... ms
  between buffers` (21.3 ms nominal).
- **Still thin/tinny/hiss on the console after that (2026-09-30):** Linux
  capture is clean (no gaps, music spectrum, peaks -10 dBFS). Menu/movie
  audio = one S16 48 kHz stereo ring voice v0F4 (EA mixer's FL/FR, bins 0/1,
  -6 dB; v0F5/F6 = C/LFE, rears; 6-ch interleave, 2400-sample ring, refilled
  in 256-sample chunks 224-736 samples ahead of CBO). RECOMP_APU_TRACE
  itself injects 5-zero runs at frame starts (~100/s) -- never judge audio
  from a traced run. Diagnostics: `[perf] APU ring voices: N of M samples
  stale` (VP read = one lap earlier -> mixer late; Linux ~0.3%, SCHED_RR
  one-core sim 1.5-6%), `RECOMP_AUDIO_DUMP=<path>,<start s>,<secs>` (exact
  PCM given to SDL). Opt-in `RECOMP_NX_GUEST_RT=1`: time-critical guest
  thread (the mixer) at host 0x2D -- did not help in the RR sim (GIL is
  FIFO). Awaiting a console log + dump.
- **Music hiss, all platforms (fixed 2026-09-30):** the translator had no
  `pushad`/`popad` (RECOMP_UNIMPL). EA's music resampler `sub_0027CCF0`
  (32 kHz EA-XA -> 48 kHz, 32.32 fixed point with adc, linear interp) is
  bracketed by them, so it returned its scratch registers to the mixer
  (`sub_00279A82`, 512-sample blocks), which then lost the fractional
  position every block: a 1/3-sample jump every 10.7 ms (hiss, worst on
  bass) and music 0.18% slow. Now lifted (lifter.py; translator.py marks all
  registers used; tools/recomp/test_pushad.py). Measured on Linux against
  the disc track (ZZDATA8 stream 38, decoded with a vgmstream-style EA-XA R2
  decoder): 0 jumps, 1 ms windows match at 49 dB. Only 9 gen files change
  (recomp_0023/28/29/36/50/52/53/70/78). `RECOMP_APU_VOICE_DUMP=<voice hex>,
  <path>,<start s>,<secs>` (apu_vp.c) dumps one voice before/after its
  filter as float32 plus its registers once a second; v0F4 = menu music.
  Still open: `cvtss2si` is lifted as a truncating cast (x86 rounds), 41
  sites -- not this bug, but wrong.
- **Engine/speech only on the left (fixed 2026-10-03):** the stubbed-EP
  mixdown (apu_dsp.c) sent even bins left, odd right, so the EA mixer's
  C/LFE voice v0F5 (bins 2/3: engine, speech) was centre -> left only.
  Centre, LFE and I3DL2 (bin 10) now go to both sides at -3 dB (`s_downmix`).
- APU IRQ 5 was raised on Windows only (`#if _WIN32` in apu_core.c) -> no
  DirectSound voice ever started elsewhere.
- NFSU2 mixes in software (EA engine, thread `sub_00274CA0`) into three 50 ms
  5.1 ring voices (v0F4-F6). Its scheduler sleeps `deadline - KeTickCount`
  (+10 ms a turn). KeTickCount was only written by the NV2A ack thread, which
  blocks in the pushbuffer executor (traps, FLIP_STALL) -> clock froze ~5 s
  after boot, scheduler ran at 2 Hz. Now `tick_count_thread` (1 ms).
- Translator: `lahf` was a comment (fixed: AH from the flags' owner) and a jcc
  after two comiss/ucomiss predecessors fell back to `if (_flags)` (fixed in
  `_merge_flag_states`); tests `tools/recomp/test_flag_sse_compare.py`. EA's
  mixer muted every channel through these. ~1660 other `_flags` fallbacks
  remain in gen/ (je/jo/js...), not audited.
- Debug: `RECOMP_APU_TRACE=1` (FE methods, per-second voice summary). gdb
  hardware watchpoints report only value *changes* -- stamp a nonzero value
  first when the writer stores zeros.

## Performance findings (Switch focus)

- **Start here for performance work: `PERF_NOTES.md`** (state, uncommitted
  patches in `/root/nfsu2x/perf-wip/`, how to apply, measure, next steps).

- Profile on Linux: `/usr/lib/linux-tools-6.8.0-142/perf record -e cpu-clock -F 499 -p $(pgrep -n -x nfsu2_recomp)`
  (the `/usr/bin/perf` wrapper doesn't work on this WSL kernel; `-e cpu-clock` is required).
- `nv2a_flag_thread` / `nv2a_ack_thread` looped on `Sleep(0)` and burned a core
  each. Now they sleep (≤1 ms) when the pushbuffer is idle and are woken by
  `recomp_spin_wake()`, which `RECOMP_SPIN_HINT` calls. A fixed sleep instead
  of the wake slowed the game (every kickoff waited) — don't go back to that.
- `tools/recomp/spin_hint.py` marks the title's poll loops (45 in NFSU2: D3D
  fence 0x2E9057, PGRAPH polls, DirectSound's APU-clock polls on 0xFE820010)
  with `RECOMP_SPIN_HINT()`: pause, wake hardware threads every 16, yield
  every 64. Poll = single block, back-edge to itself, no stores/calls, fixed
  addresses, no loop-carried registers. Tests: `tools/recomp/test_spin_hint.py`.
- Movie frames are 640x480 linear A8R8G8B8 (fmt 0x12) textures replaced every
  frame; they were decoded per texel through `sample_texture`. The GL
  renderer now uploads 0x12 straight from guest memory (`glTexSubImage2D`,
  `GL_UNPACK_ROW_LENGTH`). `RECOMP_TEX_STATS=1` lists decoded formats/sizes.
- Movies are paced by DirectSound's play cursor = the APU clock. With no
  host audio (Switch, Linux) `throttle()` in apu_core.c paces by wall clock;
  it used to reset after any block >5.3 ms late, and Horizon's 10 ms slices
  made the clock lose time. Now late blocks are caught up (`EP_CATCHUP_US`,
  100 ms). Hardware `[perf] APU n frames/s` reads 1500 = real time.
- **Clock overflow:** `qemu_clock_get_us/ns` (apu_shim.h, nv2a/qemu_shim.h)
  did `count * 1e6 / freq`; QPC on POSIX/Switch is ns since *host boot*, so
  it wrapped after 2.6 h of uptime (1e9 variant: 9 s) and APU pacing +
  XGSCNT went to garbage (`APU 0 frames/s`). Now `qemu_qpc_scale()`. Results
  that depend on audio pacing from a long-running console/WSL before this
  fix are suspect.
- **Movies are VP6, decoded by FFmpeg** (2026-09-30, idea from nfsmw-nx
  docs/audio-and-video.md): EA `MVhd` streams inside ZZDATA0-2.BIN (not the
  B3/*.xmv files), 640x480 ~30 fps, `MV0K`/`MV0F` chunks. The player
  (sub_0025F909) passes each chunk payload to `sub_002618F0(dec, data,
  size, w, h)` = On2's VP6 decoder; overridden in recomp_manual.c to decode
  with FFmpeg into `[dec+0x244]` and swap it with `[dec+0x254]` (the output
  sub_0026144D reports). Planes are bottom-up, Y stride +0x1B8 / UV +0x1BC,
  offsets +0x21C/+0x220/+0x224, 48/24-pixel border (left unfilled).
  `NFSU2_NATIVE_VP6=0` lifted decoder, `=2` both + compare (Linux: 2100
  frames bit-exact). The YUV -> A8R8G8B8 row converter `sub_0025ECB4`
  (MMX tables at 0x3D0810/1010/1810, called by sub_0025F0B7 per row) stays
  after FFmpeg; native in recomp_manual.c since 2026-10-01 (bit-exact,
  RECOMP_NATIVE=0 / RECOMP_NATIVE_CHECK=1). FFmpeg is a minimal **LGPL** build, vp6 decoder only
  (`tools/build_ffmpeg_vp6.sh switch|linux` -> /root/nfsu2x/ffmpeg-vp6-*;
  CMake `-DNFSU2_FFMPEG_DIR`, platform/switch/build.sh `FFMPEG_DIR`). devkitPro's
  switch-ffmpeg is `--enable-gpl` -- don't link it. Log: `[movie] VP6: n
  frames, x ms average`. Linux x86 (plain C): ~1 ms/frame.
- Movies: ealogo, THX_LOGO, PSA, FMVOpening (trailer before Press Start);
  names logged by the sub_00129610 wrapper (`[movie] MOVIES\\...`). In 16:9
  src/movie_crop.c (via `nv2a_raw_batch_hook`, executor -> GL/VK) scales
  the player's quad (clip x +-1, vertex program, 0.675 of the width) by
  1/0.675 so every movie fills the screen (since 2026-10-05; before, only
  FMVOpening). The disc has 28 MVhd streams (ZZDATA0-2); only FMVOpening is
  letterboxed, the rest are full 4:3 and lose ~65 rows top/bottom (user's
  choice: crop, not stretch). Quads not at x +-1 are left alone.
  NFSU2_MOVIE_CROP=0 off, NFSU2_MOVIE_TRACE=1 logs movie draws.
- **White flashes in FMVOpening** (2026-10-06): the trailer cuts between
  its clips with 1-3 flat white frames (luma ~235, in the VP6 data itself;
  no white between the movies on Linux). movie_vp6.c shows a frame whose
  middle half is all luma >= 200 as luma 16 (title's buffers only, FFmpeg's
  reference untouched). `NFSU2_MOVIE_FLASH=1` keeps them; off in
  NFSU2_NATIVE_VP6=2. `RECOMP_VK_CUBE=0` turns the car reflections off.
- **Version** (CMakeLists.txt `NFSU2_SWITCH_VERSION`) is a plain variable
  since 0.5: as a CACHE default the 0.4.5 NRO still said 0.4.3.
- (Before the FFmpeg decoder) movie decoding ran on the game thread: MMX IDCT `sub_0026EB34`, YUV->RGB
  `sub_0025ECB4`, `sub_0026FBB1` (Linux perf of the movies). The translator
  keeps registers of MMX *leaf* functions in shadowing C locals
  (`_localize_leaf_registers`, `recomp_leaf_ld_*`/`st_*`; 21 functions,
  `RECOMP_LEAF_LOCALS=0` at regen disables): IDCT ~7.8x, MC ~2.5x on Linux,
  output unchanged (frame dumps).
- Switch log: floats printed from inside the log device's `%f` timestamp
  shared newlib's dtoa buffer, so every `[perf]` fps figure was a copy of its
  timestamp's digits. The timestamp is integer-formatted now.
- `NFSU2_NO_LOG=1` (or `NFSU2_LOG=0`) in nfsu2x_env.txt: the log device
  drops everything after the settings lines, no flusher/[perf], no
  profiler, no xbox_kernel.log (`RECOMP_NO_LOG=1`, kernel_thunks.c).
- Switch defaults: `RECOMP_QUIET=1` (kernel summaries, [READ], DMA_PUT, GPU
  stats each flushed stderr = an SD write); lifted code built `-O2`
  (`NFSU2_GEN_OPT`, others `-O1`); build with `JOBS=6` so -O2 fits in RAM.
- **Races (2026-09-29, Linux):** NFSU2 renders races at 30 fps (every other
  vblank) and issues ~1300-1600 draws a frame (~45k/s): world ~460, two
  reflection passes (320x240, 4x 128x128), post-processing, HUD ~190. The
  executor thread is the likely Switch limit (~10 us per draw on x86). Done:
  only present attributes uploaded (was 256 B/vertex), vertex/index data
  streamed into two orphaned ring buffers (glMapBufferRange unsynchronized),
  render state / program / uniforms / 192 VS constants / sampler state set only
  on change (`state_dirty()` after clears, presents, new surfaces), D3D's
  vblank handler no longer spins for the ack thread (`xbox_Nv2aVblankTaken`),
  executor waits for traps and flip retires on an event, not Sleep(0).
  Next candidates: merge consecutive draws with identical state, cheaper
  vertex fetch in the executor. Measure fps from memory: D3D flips at
  device block (+0x2F7798 -> +0x1C28) +0x1CC; read /proc/<pid>/mem unbuffered.
- **Vulkan? (2026-09-29, Eden measurement):** devkitPro ships only Mesa
  GL/GLES (nouveau) and deko3d (its shader compiler `uam` is an x86 host
  tool; our shaders are generated at run time), but Mesa's NVK has been
  ported outside devkitPro: mesa-switch (danfromtico, used by nfsmw-nx) and
  NXVK (PalindromicBreadLoaf). Race on Eden: 8-18 fps, ~22k draws/s, GL backend
  = ~44% of the executor thread (19 us/draw), game waits on D3D fences
  (BlockOnTime) ~45% of the time. With every GL draw skipped
  (instrumented build, `RECOMP_GL_NODRAW`) the race only reaches 17-23 fps
  and fence waits drop to 13-25%: the lifted game code + executor decode are
  the next limit. So any graphics API is worth at most ~2x, not 30 fps.
  Done since: texture and program lookups are hash chains (`tex_get`,
  `prog_get`, last-program fast path), texture binds and enabled attribute
  arrays cached, the vertex program hash and the 3 KB constants memcmp
  skipped via executor generation counters (`vp_prog_gen`/`vp_const_gen`),
  16-bit indices, and vertices uploaded as stored
  (`NV2A_BACKEND_RAW_DIRECT`, `Nv2aRawBatch.attr_direct`; only CMP normals
  still go through float4; `RECOMP_GL_DIRECT=0` for the old path). Eden,
  heaviest race stretch: ~31k -> ~36k draws/s, GL 18 -> 14-15 us/draw
  (Eden noise is +-15%). Executor then busy ~75-85% (GL ~50%, decode ~30%);
  trap and FLIP_STALL waits are only 1-3%.
- **Hardware race (2026-09-29, handheld, perf build):** 6-12 fps, 12-16k
  draws/s, GL 32-37 us/draw (Mesa 20.1 nouveau, 2x Eden). Executor
  (`nv2a_ack_thread`) wall busy = its CPU ticks (66-87%): CPU-bound, not
  waiting on the GPU, so an apm GPU clock bump is not the fix yet. Thread
  entries in `% of a core` are absolute addresses (`__start__` is 0 in the
  ELF): solve the load base from their spacing against `nm`.
- **Frame serialisation (fixed, opt-in `RECOMP_FRAME_LAG=1`):** the main
  loop `sub_000AEA90` calls BlockOnFence (0x2E9530, return 0x000AEDDB) on
  the fence of the frame it just built, before Present (0x2EB7F0), so game
  and executor took turns. The wrapper in recomp_manual.c waits on the
  previous frame's fence instead. Eden race 9-18 -> 21-24 fps, executor
  then 99% busy; remaining game waits are pushbuffer ring space (0x2E9184).
  Linux race frames unchanged. Hardware test pending.
- **Hardware with RECOMP_FRAME_LAG=1:** race 7-16 fps, executor 90-97%
  CPU (GL ~35 us + decode ~18 us per draw), ~17k draws/s; fps = draws/frame
  / 17k. 30 fps at ~2000 draws/frame needs ~16 us/draw.
- **RECOMP_GL_THREAD=1** (nv2a_gl.c, "Threaded submission"): the executor
  queues draws/clears/flips (shadow, program, constants as diffs against the
  last record; vertex ranges, indices copied) and a GL thread replays them;
  at most 2 flips queued. Needs everything a back end reads to be private or
  pre-resolved: `Nv2aRawBatch.tex_va/pal_va`, and `sample_texture` takes its
  Texture (nv2a_backend_decode_texture used to borrow `s_gpu.tex`; racing
  it gave magenta textures). Linux race frames correct; Eden 21-24 fps
  (same as frame lag alone). Hardware test pending.
- **Hardware with GL thread (15:42/15:59 runs):** race 11-20 fps. Executor
  30-50%, GL thread 56-89% but idle 9-42% (queue empty), executor almost
  never held back: the game main thread paces. Its race time: 61% game code,
  23% waiting for the guest lock (GIL), ~12% in D3D KickOff (sub_002E8D40)
  spinning on the PFB write-combine flush bit (+0x100410 bit 16) until the
  flag thread cleared it, then waiting for the GIL after the spin yield.
  Fixed: recomp_spin_wake clears that bit on the polling thread. The EA
  mixer worker (sub_0021C8E6, code at 0x27xxxx) does ~7% work but ~12% GIL
  contention. q_reg_diff (8 KB compare twice per draw, 14% of the executor)
  replaced by executor dirty blocks (`nv2a_pb_reg_dirty`).
- **16:14 run (flush fix):** race steady 15.6-19.5 fps; KickOff wait 12% ->
  6%, GIL wait still 24%, game code 63%, `__aarch64_read_tp` 3.8% self
  (guest registers are RECOMP_TLS; -mtp=soft makes every TLS access a call;
  138k call sites, ~10 per hot function). Opt-in `RECOMP_GIL_EAGER=1`: the
  main guest thread (xbox_gil_mark_main in main.c) gets the lock at once --
  the holder yields at its next function entry -- and clears the flag on
  taking it, so the pre-empted thread waits its 1 ms again (no ping-pong).
- **16:28 run (RECOMP_GIL_EAGER=1):** race 16.6-22.3 fps (~19.5 avg, was
  ~17). Main thread: game code 72.5%, GIL wait 24% -> 10%, NtWait 11%
  (handle 0x48000008 via XAPI WaitForSingleObject 0x21B475, callers include
  the EA audio code 0x27Exxx: likely the main thread blocking on a guest lock
  the mixer holds; `[perf] main thread waits by caller` in the log). The APU
  fell to 1283-1475 frames/s: SetThreadPriority is only tracked in
  win32_compat, so its HIGHEST request never reached Horizon; now
  `xbox_nx_raise_host_thread` puts the APU frame thread at 0x2C
  (`RECOMP_NX_AUDIO_PRIO=0` off).
- **Audio uneven with the first RECOMP_GIL_EAGER** (main thread pre-empted
  everyone): the EA mixer (thread entry 0x00274CA0) runs at base priority
  +16 = TIME_CRITICAL and must pre-empt the main thread. RECOMP_GIL_EAGER
  now hands the lock over by guest priority (GetThreadPriority of the
  waiter vs. the holder; per-priority waiter counts; the flag stays up while
  a higher-priority thread waits). Priorities seen: main 0, stream workers
  +1/+2/-2, mixer +15. Linux race audio (SDL disk capture): no dropouts.
- **16:52 run (priority handover for everyone):** audio better, race 12-17.7
  fps: stream workers (+1/+2) now pre-empted the main thread too (GIL wait
  31%). Narrowed: only time-critical waiters (the mixer) and the main thread
  pre-empt at once (GIL_CRITICAL). APU 83% asleep / 12% working, so its
  1350-1495 frames/s is its wall-clock pacing, not CPU.
- **Game code is the limit now** (60-72% of the main thread). The hottest
  functions (sub_0009A330, sub_000A3CA0, sub_002A68EC) are x87 math, and the
  lifter emits every fld/fstp as a read-modify-write of RECOMP_TLS
  g_fp_stack[8]/g_fp_top (lifter.py ~3375): TLS call on Horizon, and guest
  MEM stores may alias it, so nothing stays in registers. Candidate: map the
  x87 stack to C locals where the depth is static (like
  _localize_leaf_registers), spilling at calls; plus RECOMP_TLS registers ->
  globals swapped at GIL handover.
- **x87 top in a local (translator `_localize_x87_stack`, RECOMP_X87_LOCALS=0
  off):** every function using the x87 stack (2985) shadows g_fp_top with a
  local int and g_fp_stack with a pointer to this thread's array
  (recomp_fp_base/top_ld/top_st in recomp_types.h), storing the index
  before every call/ICALL/ITAIL/return and reloading after calls. Copying
  all 8 slots at each call instead was slower (x86 menu 1.18-1.74 vs 0.97
  ms/frame); the index-only version is 0.92 (-5%) on x86, where TLS is
  cheap. Linux race and Eden fine. So far regenerated only into a private
  gen (scratch gen2 + toolkit copy), not /root/nfsu2x/gen.
- **Registers in locals (translator `_localize_registers`,
  RECOMP_REG_LOCALS=0 off):** eax..edi and esp shadowed by locals in 19394
  functions (accessors recomp_leaf_ld/st_*, esp added), stored right before
  every call/ICALL/ITAIL/UNIMPL/SPIN_HINT (`_wrap_calls`: after the
  argument and return-address pushes on the same line) and reloaded right
  after the call's statement; stored at every return and at the end. Needed
  header changes: RECOMP_ABI_CALL's check reads the real registers
  (recomp_leaf_ld_*), and the ICALL failure paths store esp/eax through
  (RECOMP_ICALL_FAIL_SYNC). Registers as plain globals instead is NOT
  possible: kernel_thunk_dispatch releases the GIL before the bridges read
  g_esp and write g_eax. x86 menu: slightly slower (1.06 vs 0.94 ms/frame,
  TLS is cheap there); Eden main menu (ARM code): 38-40 vs 29-32 fps. NRO
  4.6% smaller.
- **Native culling (src/recomp_manual.c in the scratch repo copy so far):**
  sub_0009A330 (box vs 6 frustum planes, returns 0 out / 1 straddle / 2 in)
  and sub_0009A250 (box by matrix, Arvo) in C with the lifted code's exact
  arithmetic (doubles, float rounding where it stores to memory, NaN takes
  the jp branch). RECOMP_NATIVE=0 off, RECOMP_NATIVE_CHECK=1 runs both:
  0 mismatches in 8.4M + 3.1M calls over a Linux race. ~40% of the
  culling calls carry a matrix.
- **FPS drop at some map points (2026-10-01; easiest: Quick Race -> Drag ->
  Coastal Express start line):** a long sightline, ~2300 draws a frame (vs
  ~930 once the camera moves, ~1500 in circuit races), about half of them
  0 samples (behind walls / off screen; game culling, not ours:
  RECOMP_NATIVE=0 gives the same counts). Game thread, executor and GL
  thread all ~2x per frame. The track is locked in the test profile: the
  console save (005213381338) is in /root/nfsu2x/game_drag (symlinks + own
  UDATA). Path: Main Menu a, right x2 (Drag) a, right x3 a, a x5; without
  throttle the car stays at the line = static, repeatable scene.
  `sub_002213EA` (SSE 4x4 matrix multiply, stdcall out = a * b; twice per
  object from sub_000A2EA0) was 15-18% of the game thread: every xmm
  register is TLS. Native in recomp_manual.c (no FMA contraction, same sum
  order; 0 mismatches in 7.3M calls); in-run A/B at the start line: main
  thread 6.42 -> 5.42 ms/frame on x86. Also: calls to *wrapped* overrides
  (sub_X around sub_X_gen) were emitted as RECOMP_ICALL_SAFE (trace write +
  dispatch binary search per call, sub_0009A330 included); the lifter now
  calls the wrapper directly (`Lifter.wrapped_functions`, test in
  test_manual_call_dispatch.py). The renderer side (per-draw cost) is still
  ~2x there.
- **More native leaves (2026-10-02, src/recomp_manual.c, Linux race perf
  of the main thread):** `sub_002A68EC` = MSVC `_ftol2` (589 call sites,
  top self time 4.4%), `sub_000A3CA0` (colour grade LUT, 64x64 x2, rebuilt
  every race frame by sub_000A3FA0 when its params at 0x39D304 change; ~10x
  faster), `sub_0004BC20` (point x 4x3 matrix), `sub_0004B260` (sine of a
  16-bit angle, result on the x87 stack), `sub_001C6900` (sphere/plane
  fade, int via cvttss2si), `sub_000113A0` (4x4 copy). All exact incl.
  registers, g_fp_cc, g_ebp/g_seh_ebp, xmm0; RECOMP_NATIVE_CHECK=1: 0
  mismatches (millions of calls each). Gotcha: `fcomp; test ah,5; jp`
  jumps when C0 == C2, i.e. on >= or unordered (the "if (v < K)" idiom).
  Then: `sub_001C3430` (max edge function, 3/4-gon), `sub_0004C6E0` (2D
  overlap with margin), `sub_001C65D0` (int16 rows -> floats),
  `sub_002EEA80` (D3D's SSE 4x4 multiply, xmm0-5 as left), `sub_0004B940`
  (t = b * a, per-element sum order generated from the disassembly, then
  sub_000113A0). On x86 the small leaves only gain ~30% (TLS is cheap
  there); integer code that calls back into lifted code (sub_000ACB30 mesh
  submit, sub_000AD7D0) is not worth it. Remaining by self time:
  sub_001C69C0 (big, branchy), sub_002EC140 / sub_002E8D40 (D3D).
- **Missing function 0x000930D0** (thiscall method after int3 padding,
  called every race frame, never lifted): every Linux race logged ~10k
  `[ICALL] Failed to resolve VA 0x000930D0` (each with a trace dump) and
  skipped it; one run then fell to 4 fps. Seeded in
  config/seed_functions.json (full regen: exactly one function added).
- **LTO (2026-10-02):** CMake `NFSU2_LTO=ON` (+ `NFSU2_LTO_JOBS`) puts the
  lifted code and recomp_manual.c through LTO; `LTO=1 bash platform/switch/build.sh`
  builds in `<build dir>-lto` and stages `nfsu2x[-vulkan]-lto.nro` next to
  the normal NRO. Cheap: ~3 min, ~2 GB. On its own it inlines almost
  nothing (1.3k of 63k direct calls): lifted functions exceed -O2's
  max-inline-insns-auto. Declared `inline` + slow modes moved to a cold
  noinline helper, `_ftol2` (sub_002A68EC) is inlined into all but 12 of
  its 581 sites. x86 main-menu idle: LTO 0.92 vs 0.95 ms/frame median,
  inside the +-15% noise. Linux LTO race in check mode clean. Console A/B
  pending. Thread pointer: GCC calls __aarch64_read_tp once per function
  (mrs tpidrro_el0 + ldr), so TLS is not a per-access cost.
- **Vblank was slow (fixed 2026-10-02, kernel_bridge.c):** the tick was
  `now + 16 ms` checked on the timer thread's 10 ms wait -- late, drifting,
  ~46 Hz -- and NFSU2 flips every 2nd vblank: Linux races ran at 23 fps.
  Now 59.94 Hz drift-free on a us clock, the timer thread wakes when it is
  due (kernel_vblank_wait_ms): Linux race 23 -> 30 fps. `[fps]`/`[perf]`
  lines show `vblank N Hz` (dips to ~50 Hz only at loading stalls, with
  the old FLIP_STALL 250 ms timeouts). No VRR needed: D3D's flip queue
  (sub_002F2080) flips a frame that missed its vblank as soon as it is
  queued (immediate-when-late flag), so frame times are not quantised --
  an adaptive vblank hold was tried and never triggered (trace: retires
  8 ms after a vblank, no vblank between).
- **Clocks (switch_nx.c, opt-in):** NFSU2_CPU_MHZ / NFSU2_GPU_MHZ /
  NFSU2_MEM_MHZ via clkrst (8.0+) or pcv: highest listed rate <= the
  request, caps 1785 / 921.6 / 1600, old rates restored on exit (atexit +
  switch_shutdown), re-applied every second while focused (dock/sleep
  reset them). Log `[clock] CPU 1020 -> 1785 MHz`. Not hardware-tested.
- **Draw merging is not possible as is:** RECOMP_MERGE_STATS (removed
  again) over a race: 0% of draws have state identical to the previous
  one; ~45% differ only in vertex-program constants (per-object matrices),
  11-15% in constants + vertex arrays. Merging would need instancing with
  per-draw constants in the shaders.
- **Race hitches (Eden, `[hitch]` lines: frames > 50 ms with programs
  compiled / textures uploaded and their time):**
  - race start: ~40 programs compiled in two frames (~40 ms each in Mesa,
    1.5 s). Fixed by a program cache: every compile appends its inputs
    (xform, program slots, combiner key) to sdmc:/switch/nfsu2x/progcache.bin
    (`RECOMP_PROG_CACHE=<path>|0`); ready() precompiles the file (66 programs,
    2.7 s in Eden, at boot). What is left is the first race frame (12.8 MB of
    first-time textures).
  - mid-race 0.5-1.5 s frames with nothing compiled or uploaded: the EA mixer
    (time critical) polled DirectSound positions, which call
    KeQuerySystemTime, ~400k kernel calls/s, each a guest-lock handover it
    won against the main thread. Fixed: KeQuery*Time/PerformanceCounter
    (ordinals 125-128) keep the GIL (`kernel_call_keeps_gil`,
    RECOMP_KERNEL_FAST=0 off). Worst mid-race frame 1540 -> 77 ms.
- **Console 22:36 run (registers/x87/native culling + kernel-fast):** race
  19.5-22.7 fps (was 11-20). GL thread now the limit (87-93%, idle 1-6%);
  shader compiles cost 90-150 ms each on the console (52 at race load =
  4.5 s: progcache.bin was not on the card); new textures ~70 ms/MB.
  GL thread profile: constants re-sent whole (memcmp/_mesa_uniform/
  nvc0_constbufs_validate ~7%), nouveau_bo_new per ring map (~3%).
  Fixed: DXT1/3/5 (0x0C/0x0E/0x0F, nearly all race textures) uploaded
  compressed (glCompressedTexImage2D, RECOMP_GL_DXT=0 off), swizzled
  A8R8G8B8 unswizzled in a loop, only changed constant rows sent (runs,
  when c[191]'s location is u_c+191), ring maps without
  GL_MAP_INVALIDATE_RANGE_BIT. Eden after 90 s: hitches 133 -> 33, worst
  153 -> 61 ms, textures 6.6 MB/151 ms -> 0.2 MB/9 ms.
- Profiler: samples carry a 10 ms timestamp (`prof_report.py --time A-B`);
  in Eden it samples only the main thread (pausing every thread each ms
  hung Eden's boot once the buffer was larger).
- **Eden race fps is not a measure of game code**: ~22 fps for every build
  since the frame-lag fix; menus do move (+25-30% with register locals).
- **RECOMP_MEM_VOLATILE** (recomp_types.h): `-DRECOMP_MEM_VOLATILE=` builds
  guest memory accesses non-volatile; on x86 no measurable gain (menu 1.0
  vs 0.97 ms/frame). Default unchanged. Linux A/B: races are not
  repeatable (15.0 vs 10.9 ms/frame for the same build), nor is a paused
  race; the main menu idle is (`RECOMP_FPS_LOG=1` + /proc main-thread utime).
- **Profiler (RECOMP_NX_PROFILE=1, switch_nx.c):** 1 kHz samples of busy
  threads (svcSetThreadActivity + svcGetThreadContext3) into
  sdmc:/switch/nfsu2x/prof.bin; `tools/prof_report.py prof.bin
  build-switch/nfsu2_recomp.elf nfsu2x_log.txt`. Base is `&_start`
  (`__start__` is an unrelocated absolute 0). Eden has no thread tick
  counts, so there every tracked thread is sampled (buffer fills in ~2 s).
- **StevensND/nfsmw-nx** (NFS Most Wanted 360 port, 32-35 fps on Switch at
  stock clocks) is the reference for what works: native Vulkan renderer on
  NVK (mesa-switch + their patch), ~16 us/draw at first on the console,
  then caches/uploads without duplicates, LTO + PGO + direct calls +
  function ordering (~+12%), native rewrites of the game's hottest renderer
  functions, `apm` performance configuration 0x92220008 (GPU 460.8 MHz
  handheld). Docs: docs/performance-history.md, measuring.md,
  platform-notes.md (thread priorities: only 0x3B time-slices; A57 atomics
  are slow).
- Regen from the worktree needs `tools/{disasm,func_id,abi_analysis}/output`
  — symlinked from the main checkout (don't commit the links).

## Status (2026-09-28)

- Linux: boot → movies → title → profile → Main Menu → Quick Race race and
  Career explore mode, loading screens and race colours correct (2026-09-29).
- Eden: reaches Main Menu.
- Hardware: boot → movies → profile load/create → Main Menu. Movies were
  slow (APU clock losing time + decoder cost); APU clock fixed and confirmed
  at 1500 frames/s, decoder speed-up awaiting a hardware test.
- Audio plays on Linux (2026-09-29); Switch audio awaiting a test.
- Open: cube maps on GL (done on VK), bump/dot-product texture modes (dependent AR/GB done),
  fixed-function lighting, APU performance on Switch.
