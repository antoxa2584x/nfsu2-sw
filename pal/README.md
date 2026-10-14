# PAL-EU support in the shared port

US and EU builds use the same source tree, Xbox runtime, renderers, audio decoder, Switch platform layer and heap bootstrap. Region-specific hooks live under `src/regions/eu/`; selecting EU does not copy or patch a separate runtime tree.

US and EU are separate NRO builds. Each links its own translation and
region-specific game hooks against the common runtime and platform code.

## Supported discs

The EU build requires the tested European `default.xbe`: 4,038,656 bytes, SHA-256 `34e2a0dbc722302d12b05b12fbaf8d7c3ddd024183f3e33b96fff09156b29604`, entry `0x0021B77E`. The existing US translation uses entry `0x0021B1CE`.

Each NRO contains one region's translation. EU and US NROs require their matching game data. Startup rejects a mismatch before guest memory mapping. Both need extracted disc files (`default.xbe`, `NFSUNDER/`, etc.); neither opens ISO files directly. Other executable revisions are unverified.

The normal game language chooser is retained. There is no forced German selection or language override.

In EU builds, the Switch system language is exposed as the emulated Xbox EEPROM language.
The retail PAL chooser uses it to include the corresponding language row;
it still waits for the player's choice. Unsupported Xbox locales fall back
to English.

The EU Video menu is available in the garage/main menu and during play
through Pause → Options → Video. Both menus put Video directly after Audio.
It includes the original screen-size slider, reflections, resolution scale,
FPS display, FXAA, anisotropic filtering and square pixels. The six port
settings share their saved preferences between both menus.
Its labels follow the game's selected language. German labels include
`Reflexionen`, `Auflösung` and `FPS-Anzeige`. Umlauts use the retail
font's single-byte encoding. This reads the retail language state and
does not select a language or change the US wording.

The added Video rows replace the fixed TV calibration panel; the original
screen-size slider remains. EU picture defaults are FXAA off, texture filter
off and square pixels off, with saved preferences taking precedence. In the
EU build, turning the texture filter off also disables mip sampling; enabling
it restores mipmaps. The existing US picture defaults remain unchanged.
Texture filtering respects the game's minimum and maximum mip levels,
including level-zero atlases and reflection cube chains.

## Performance baseline

Recommended EU Vulkan settings:

| Setting | Recommendation |
| --- | --- |
| Resolution | 1.5x |
| Car reflections | On |
| Anti-aliasing | Off |
| Texture filter | Off |
| Square pixels | Off |
| FPS display | Optional |

The Video page shows a recommendation and changes its caption to a
performance notice when resolution exceeds 1.5x or anti-aliasing, texture
filtering or square pixels are enabled. The notice updates immediately in
either menu and does not block the selected settings. Reflections On at
1.5x and the FPS display do not trigger it.

The default resolution scale is 1.5x in both builds. Actual frame rates
depend on the scene and hardware settings; stable 30 FPS is not guaranteed.
Higher resolutions and additional picture effects increase GPU cost.
Compare builds at the same resolution, renderer, settings and clocks.

## Generation and build

The portable command uses the shared upstream toolkit:

```text
python tools/regen.py --xbe /path/to/EU/default.xbe --region EU --gen-dir /path/to/gen-eu
cmake -S . -B build-eu -DNFSU2_REGION=EU -DNFSU2_GEN_DIR=/path/to/gen-eu -DNFSU2_VULKAN=ON
```

Supply the normal Switch toolchain/SDK arguments for Switch. For US, use `--region US`, `-DNFSU2_REGION=US` and a separate generated directory. `--region auto` identifies the executable during generation; the resulting NRO still contains one translation. Existing complete analysis can be supplied with `--analysis-dir`. Output must be empty; existing translations and inputs are not overwritten.

`switch/build.sh` keeps the US output names and uses an `-eu` suffix for
EU build directories and NRO files (`nfsu2x-eu.nro` or
`nfsu2x-eu-vulkan.nro`). Use separate staging roots for each disc's game data.

Existing verified EU generated files can also be supplied with `NFSU2_GEN_DIR`. A pass-through ABI bridge is selected only when an older translation renamed the original chooser. It invokes that original chooser without selecting a language.

`NFSU2_PACKED_REGISTERS=ON` supports existing translations whose scalar
register header exactly matches the runtime header. The default is off;
ordinary upstream-generated translations retain their separate registers.
The build rejects incompatible packed headers before compiling the runtime.

## Maintenance

Renderer, audio, platform and runtime fixes are maintained once and compiled for both builds. New game-specific hooks may require an EU address/ABI mapping. Callback seeds and bounded CPU/I/O models are explicit; no global address offset is assumed.

The expensive draw-merge survey is opt-in with `RECOMP_DRAW_MERGE_STATS=1`.
Regular draw/culling diagnostics retain their existing switch. This preserves
visibility decisions and instancing while avoiding the survey's index hashes
and full register copies in normal runs.

The Vulkan renderer reuses unchanged uniform, constant and index uploads
within one command-buffer generation. Hash candidates require full byte
equality; cached descriptor state is invalidated by the FXAA layout.
`RECOMP_VK_STATE_CACHE=0` and `RECOMP_VK_DATA_CACHE=0` disable these caches.
`RECOMP_VK_ASYNC_PRESENT=1` optionally overlaps CPU recording with the
previous presentation wait. Queue access and the next image acquisition
join the worker first; the default remains synchronous.

Square-pixel sizing depends on surface width, so render targets that share
the same depth buffer keep matching physical dimensions. Texture-cache
entries distinguish pitch, mip count and palette, including lower compressed
mip levels in change detection.

Render passes track color/depth dependencies per attachment and retain shader
read visibility separately. Independent targets avoid a full memory barrier;
uploads retain a conservative full dependency. New depth buffers initialize
with an attachment clear before partial guest clears. `RECOMP_VK_SYNC=0` uses
broad pass barriers and `RECOMP_VK_DEPTH_CLEAR=0` uses transfer initialization.

Both regions share the flip-interval policy: frames can present on the next
vblank, up to 60 FPS. `NFSU2_FPS_CAP=30` retains the game's original interval.

The Vulkan unified-memory fallback treats locality/caching as preferences and retains host visibility/coherence requirements. The optional `NVK_LOCALIZED_DISPATCH` bridge supports SDK archives whose direct Vulkan symbols are localized. It is shared by both builds and defaults off.

EU generation restores the verified audio marker's PUSHFD/POPFD pair at
`0x0027E917`/`0x0027E921`. It validates the caller/callee XBE span and generated
shape before applying a bounded equality-path model. Other flags instructions
remain unsupported; changes to this span stop generation for review.

Vulkan presentation waits use one semaphore per swapchain image, rather than
per frame slot. Frame submission/fence errors stop with a diagnostic instead
of continuing invalid work. Suboptimal surfaces remain usable; surface loss
requires restarting the game until presentation-fenced recreation is supported.
Vulkan initialization is completed before guest entry; failure stops startup
with a diagnostic instead of continuing the loading screen.

## Validation

The full analysis/generation pipeline and Switch builds run natively on
Windows. Existing and freshly generated EU translations produce an ELF and
NRO with the shared runtime. EU driving, menus and the pause Video page have
been tested on a real Nintendo Switch and reported playable. A full
playthrough, US hardware regression testing and final on-device validation
of the reordered menu/performance notice remain pending.

`python tests/regions/run.py --cc clang --eu-xbe /path/to/EU/default.xbe` checks both build modes, detection and modified-input rejection. `python tests/switch_heap/run.py --cc clang` exercises the actual heap bootstrap. `python tests/vulkan_memory/run.py --cc clang` checks the renderer's allocation policy.

The shared native matrix and movie-row helpers are exercised by `tests/native_matrix/run.py` and `tests/movie_row/run.py`, including overlap, signed saturation/wrap and guest register postconditions.

`tests/vulkan_present/run.py` compiles the actual shared renderer against a
typed Vulkan driver model. It exercises nonsequential image acquisition,
delayed presentation waits, suboptimal surfaces and injected API failures.
It requires Vulkan/glslang headers and host glslang libraries; use `--help`
for their paths. This test does not measure GPU output or performance.

`tests/eu_frontend/run.py` exercises the actual EEPROM API and both EU flip
handlers with a competing retirement while raising IRQL, including counter
wrap. With private `--gen-dir` and `--xbe` inputs it also checks the retail
language selector's German branch. Explicit Switch abort/fatal paths commit
their native caller offset to the log before termination.

`tests/text_labels/run.py` checks the actual EU language-state reader and text
patcher in US and EU modes, including recommendation captions, table ordering,
repeatable updates, pool boundaries and malformed chunks. EU captions are
added within the existing language-chunk allocation; disc files are not edited.

`tests/vulkan_textures/run.py` checks texture bytes on a Vulkan GPU, including
pitch, mip-count and palette changes, lower-mip updates, shared garage depth
AA metadata transitions, and runtime mip changes for textures and reflection
cubes. It needs host Vulkan/glslang libraries;
`--help` lists their paths. `tests/frame_rate/run.py` checks the shared flip
policy with the default and original 30 FPS cap.

Options → Video → FPS Display (German: FPS-Anzeige) switches the Vulkan
overlay immediately and persists as `fps_display=0/1` in the options file.
Without a saved preference it uses `NFSU2_FPS_OVERLAY`; default is off.
The counter averages successful presentation calls over half a second.
It is drawn only into the host swapchain, never into game/reflection surfaces.
`tests/fps_overlay/run.py` checks timing and pixel bounds; the Vulkan driver
model checks on/off transitions, swapchain layouts and presentation lifetime.
