# Forza Motorsport 2 — Native PC Port

Forza Motorsport 2 (Xbox 360, 2007) statically recompiled to run natively on
Windows. The game's PowerPC code is translated ahead of time to C++ and
compiled for x86-64 — no CPU emulation, no JIT. Graphics run on Direct3D 12,
audio is decoded natively.

**No game data is included.** You need your own copy of the game: the launcher
imports it from your disc image (`.iso`) and verifies it before you can play.

<p align="center"><img src="docs/img/launcher-play.png" alt="Forza2Launcher" width="720"></p>

## Status

| | |
|---|---|
| Boot, intro videos, menus | Working |
| Profiles (named, separate saves) | Working |
| Exhibition races | Working |
| Career, multiplayer, every car and track | Not fully tested yet |
| Known issues | Some render-target resolves fail (`Resolve region is empty` in the log); a few UI sound banks are opened by relative path and do not load; Xbox Live is not available |

Reports with the log file (`logs\` next to the game) are welcome.

## Playing

1. Download `Forza2-Native-<version>-windows-x64.zip` from
   [Releases](../../releases) and unzip it anywhere.
2. Run `Forza2Launcher.exe` → **Instalación** → *Elegir ISO e importar…* and
   pick your Forza Motorsport 2 disc image. The launcher extracts the files to
   `game\` (about 5 GB) and checks every file against
   [`game_files.sha256`](launcher/data/game_files.sha256).
3. Create a profile, adjust graphics and controls, press **JUGAR**.

The port is built from one specific build of the game (title ID `4D5307EA`,
media ID `3E3180AE`, multi-language edition). Another region or revision has a
different `default.xex` and the launcher will tell you it does not match.

**Requirements:** Windows 10/11 x64, a GPU with Direct3D 12, ~5 GB free disk
space. The VC++ runtime is bundled.

### Launcher

The cover art is not shipped (it is copyrighted artwork). Put any `cover.png`
next to `Forza2Launcher.exe` and it fills the left column; without it the
launcher draws its own panel.

- **Perfiles** — each profile is a gamertag with its own saves, in
  `Documents\forza2\profiles\<name>`. Deleting moves it to `profiles-papelera`.
- **Gráficos** — quality presets (Original / Alta / Ultra) or per-option:
  fullscreen/window, monitor, internal resolution (1×–3×), anisotropic
  filtering, MSAA, FXAA, CAS/FSR upscaling, VSync.
- **Controles** — any SDL controller works; the keyboard can act as a pad and
  every button can be rebound (grouped: driving, buttons, d-pad, camera).
- **Sonido e idioma** — game language (English, Español, Français, Deutsch,
  Italiano) and mute.
- **Instalación** — import from ISO, choose the game folder, verify files.

Settings are written to `forza2.user.toml`; port settings stay in
`forza2.toml`.

## Building from source

Requires Visual Studio 2022 (or Build Tools) with the C++ workload, and LLVM:

```
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install LLVM.LLVM
build.bat release          :: or relwithdebinfo for symbolized crash reports
tools\package_release.ps1  :: builds dist\Forza2-Native-<version>-windows-x64.zip
```

`build.bat` downloads the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)
v0.10.0 into `_sdk\` on first run. The recompiled code in `generated/` is
committed, so a clone builds without the game. To regenerate it, put your
`default.xex` in `assets\` and build again.

### How it was ported

| Piece | Where |
|---|---|
| PowerPC → C++ translation | `rexglue codegen`, driven by [`forza2_manifest.toml`](forza2_manifest.toml) |
| Functions the analysis missed (tail-called thunks, ~320 vtable-only functions merged into neighbours) | `[entrypoint.functions]` in the manifest, found with `tools/` scans |
| XAPI fibers (the title's job system) mapped to host fibers | `[entrypoint.rexcrt]` in the manifest |
| Profile name and per-profile saves | [`src/profile.cpp`](src/profile.cpp) |
| Null-stream crash while loading races | [`src/stream_guard.cpp`](src/stream_guard.cpp) |
| Guest call chain on crashes | [`src/crash_report.cpp`](src/crash_report.cpp) |
| Launcher (SDL3 + Dear ImGui) | [`launcher/`](launcher) |

Every recompiled function is a weak `sub_<address>` that forwards to
`__imp__sub_<address>`, so any of them can be wrapped or replaced from `src/`.

## Legal

This repository contains no game assets: no executables, disc images,
textures, audio or video. `generated/` is a machine translation of the game's
program code; `game_files.sha256` holds only hashes. You must own the game to
use this port. Forza Motorsport is a trademark of Microsoft; this project is not
affiliated with or endorsed by Microsoft or Turn 10 Studios.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) — recompiler and runtime
- [Xenia](https://github.com/xenia-project/xenia) — the emulation research ReXGlue builds on
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) — static recompilation for Xbox 360
- [CrownParkComputing/Xbox360-Native-Ports](https://github.com/CrownParkComputing/Xbox360-Native-Ports) — the approach this port follows
