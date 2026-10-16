# Disc regions and executable compatibility

ISO extraction reads the Xbox filesystem without a country whitelist. Bare XDVDFS images and supported full-disc partition layouts are accepted. Import verifies the executable before extracting gigabytes or replacing an existing installation.

Static recompilation translates the executable ahead of time. Each app build currently contains **one** executable profile:

| CMake profile | Executable SHA-1 | Entry point |
|---|---|---|
| `NTSC-U` (default) | `428b5a70b9400f19cc5be7679db6c72a6f80a1c9` | `0x0021B1CE` |
| `PAL` — Europe En/Fr/De/It/Nl/Sv | `144deb954e0e9208e0b7e08e09b4e5e67b2f3356` | `0x0021B77E` |

These executables are 4,038,656 bytes. The importer identifies either known hash and reports when the selected disc needs a different build. Other regional releases or executable revisions need their own translated code and verified native overrides; universal playback is not implemented. Do not disable executable checks to claim support for them.

Use `tools/regen.sh` for the matching NTSC-U executable. Use `tools/regen_pal.sh` and `NFSU2_REGION=PAL` with the matching generated source/manual adapter for the verified European executable. Neither executable nor generated game source belongs in a public commit. Adding another profile requires its legally supplied executable, verified addresses/overrides, importer metadata, builds and gameplay tests.

## Extracted directory

The default runtime path is `Documents/game` inside the app's sandbox:

```text
Files → On My iPhone/iPad → NFSU2 → game
  default.xbe
  NFSUNDER/
  UDATA/       # saves, if created
  TDATA/
```

Do not add another game-named folder between `game` and `default.xbe`. ISO import creates this structure automatically. The active absolute read path and compiled profile are visible under **Edit → Game files → Extracted game directory**. `NFSU2_GAME_DIR` can override the runtime path for development; the UI shows that effective path.
