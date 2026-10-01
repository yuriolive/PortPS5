# PortPS5 user guide: running a game

How to convert one decrypted PS5 game dump and run it on Windows. For design detail see [spec/relinker.md](spec/relinker.md), [spec/configuration.md](spec/configuration.md) and [spec/verification.md](spec/verification.md).

> **Status.** PortPS5 is pre-1.0. Only the five gate titles in the [PRD](PRD.md) are targets, and most titles will not boot yet. The steps below are what works on `main` today. Rows marked *planned* are tracked in [ROADMAP.md](ROADMAP.md).

## 1. Legal boundary

- You supply your own, **already-decrypted** dump of a game you own. PortPS5 does not decrypt anything and ships no keys, firmware or Sony libraries.
- Never attach dumps, shader bytecode, saves, screenshots, video frames or logs containing game data to issues or PRs. See [.agents/rules/legal-boundary.md](../.agents/rules/legal-boundary.md).

## 2. Requirements

| Item | Requirement |
|---|---|
| OS | Windows 10/11, 64-bit |
| GPU | Vulkan 1.3 capable, current driver |
| Reference tier for the perf bar | 12-core CPU, 12 GB VRAM GPU, 32 GB RAM (PRD §4.4) |
| To build | MinGW-w64 GCC 15.2 (winlibs, ucrt-posix-seh), CMake 3.20+, Ninja, Vulkan SDK |
| Dump layout | A folder containing `eboot.elf` and `sce_sys/param.json` (the title ID is read from it) |

## 3. Build PortPS5

```bash
git submodule update --init --recursive
cmake --preset release
cmake --build --preset release
```

The `release` preset builds the `relinker` and the patched system libraries (`libs`). A plain `cmake --build` without the preset builds no `.prx`. Outputs:

| Output | Path |
|---|---|
| Relinker | `build/release/core/relinker/relinker.exe` |
| Patched `.prx` libraries | `build/release/core/libs/libs/` |

Check the exact paths with `ls build/release/core` if your generator differs. Toolchain and presets: [spec/build-toolchain.md](spec/build-toolchain.md).

## 4. Convert and install a game

Pick an install directory **outside the repository**, for example `D:\PortPS5\TITLE_ID`.

### Option A: `tools/regress.py prepare` (recommended)

```bash
python tools/regress.py prepare \
  --dump  "D:\dumps\TITLE_ID" \
  --install "D:\PortPS5\TITLE_ID" \
  --relinker build/release/core/relinker/relinker.exe \
  --libs build/release/core/libs/libs
```

This runs `relinker --windows eboot.elf game.exe`, copies the `.prx` libraries to `<install>\libs`, and links `<install>\app0` to the dump (a read-only junction; the dump is never copied or modified).

### Option B: by hand

```bash
relinker --windows <dump>\eboot.elf <install>\game.exe
xcopy /E build\release\core\libs\libs <install>\libs\
mklink /J <install>\app0 <dump>
```

Always do the full conversion. Do **not** pass `--skip-sce-module`; it crashes guest libc++ iostream code.

### Config files

The runtime reads config from `<install>\config\`. The relinker does not copy it yet (bean `portps5-c06p`), so copy it yourself:

```bash
xcopy /E config <install>\config\
```

Without it the runtime logs one warning and uses built-in defaults.

### Resulting layout

```
<install>/
  game.exe          converted executable (run this)
  libs/             patched .prx replacement libraries + MinGW runtime DLLs
  app0/             junction to the decrypted dump (read-only)
  config/
    global.toml
    games/<titleId>.toml   optional per-title overrides
```

### Relinker flags

| Flag | Use |
|---|---|
| `--windows` | Emit a Windows PE (required for this guide) |
| `--windows-gui` | GUI subsystem instead of console (needs `--windows`) |
| `--windows-diagnostics` | Extra loader diagnostics (needs `--windows`) |
| `--to-intel` | Lower AMD-only instructions (SSE4a etc.) for Intel CPUs. Use it on Intel hosts. |
| `--lazy-binding` | Do not stop at the first unresolved import; bind lazily |
| `unused-filter=0\|1\|2` | Drop unused NID imports (default per tool; level 2 rewrites the PLT) |
| `--registry` | Write the import registry / conversion report |
| `--skip-syscall-check` | Skip the raw-syscall scan (diagnostic only) |

Exit codes: 0 success, 1 usage error, 2 conversion error. Errors are printed with the offset or NID involved.

## 5. Run

Run from the install directory so `libs\`, `app0\` and `config\` resolve:

```bash
cd D:\PortPS5\TITLE_ID
game.exe
```

- **Window:** opens at 60% of the display. `F11` toggles fullscreen.
- **Saves:** stored at `%LOCALAPPDATA%\PortPS5\saves\<titleId>\`. Back this folder up to keep progress.
- **Network / PSN / trophies:** offline and deterministic; they never block the game.
- **Failure at start:** the loader prints `FAIL: unresolved ELF import <nid>` and exits with a status code. Retry the conversion with `--lazy-binding` to get past it, then report the NID (not game data) in an issue.

### Default controls

Controllers (XInput, DualSense over USB) work through SDL with hot-plug. Keyboard and mouse map to a virtual pad:

| Pad | Keys |
|---|---|
| Left stick | `W A S D` |
| Right stick | `T F G H` |
| Cross | `Enter`, `Space` |
| Circle / Triangle | `C` / `I` |
| Square | Left mouse button |
| L1 / R1 | `Q` / `E` |
| L3 / R3 | Shift / Ctrl |
| D-pad | Arrow keys (up/down also mouse wheel) |
| Options | `Esc` |
| Mouse-look toggle | Middle click |
| Fullscreen | `F11` |

Rebinding through `[input.bindings]` is *planned* (bean `portps5-de24`); the table in `config/global.toml` is currently parsed but not consumed.

## 6. Configure

Layers, later wins key by key: `config/global.toml`, then `config/games/<titleId>.toml`, then the `PORTPS5_DEBUG` environment variable. An unknown key, wrong type or out-of-range value is a start-up error reported as `file:line: key: reason`.

Per-title file, `<install>\config\games\TITLE_ID.toml`:

```toml
schema = 1
title_id = "TITLE_ID"   # must match the file name and app0/sce_sys/param.json

[display]
fullscreen = true
window_percent = 80

[input]
deadzone = 0.10
swap_confirm = false
```

| Key | Values | Default | Notes |
|---|---|---|---|
| `display.fullscreen` | bool | false | |
| `display.window_percent` | 25-100 | 60 | Initial window size |
| `display.resolution_scale` | 1.0-2.0 | 1.0 | *Planned wiring* (bean `portps5-dtwf`) |
| `display.present_mode` | `fifo` `mailbox` `immediate` | `fifo` | *Planned wiring*; falls back to `fifo` |
| `input.deadzone` | 0.0-0.5 | 0.08 | Radial stick dead zone |
| `input.mouse_look` | bool | false | |
| `input.mouse_sensitivity` | 0.1-10.0 | 1.0 | |
| `input.swap_confirm` | bool | false | Swap Cross/Circle on controllers |
| `workarounds.*` | per key | off | Game files only. None are registered today ([workarounds.md](workarounds.md)). |
| `debug.*` | see spec | off | Diagnostics only (log level, traces, dumps). A release run sets none. |

Full schema: [spec/configuration.md](spec/configuration.md). There are no behaviour-changing environment variables; any `APS5_*` variable is ignored with a warning.

### Diagnostics

```bash
set PORTPS5_DEBUG=log_level=debug;trace=audio,pad
game.exe
```

Only `[debug]` keys may be set this way. Logs may contain game data: **do not paste them into issues** without checking.

## 7. Record a test result (maintainers)

Local runs are recorded as metrics-only results JSON (`portps5.results/1`), never as footage or logs. Run for a fixed duration and write the result:

```bash
python tools/regress.py run \
  --install "D:\PortPS5\TITLE_ID" --title-id TITLE_ID \
  --region <region> --patch <patch> --name "<title name>" \
  --commit <git sha> --run-type regression --duration-s 1800 \
  --gpu-vendor nvidia --driver-version <ver> \
  --bench-cpu <cinebench r23 multi> --bench-gpu <timespy graphics>
```

`PORTPS5_DEBUG` must be unset, and the install directory must be outside the repo. Protocol and fields: [spec/verification.md](spec/verification.md) §2 and §4, and the `compat-result` skill.

## 8. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `dump has no eboot.elf` | Point `--dump` at the folder that holds the decrypted `eboot.elf` |
| `FAIL: unresolved ELF import <nid>` | Missing PS5 library function. Try `--lazy-binding`; report the NID. |
| `GetLastError 127` on a `.prx` | `libs\` incomplete or stale. Rebuild with the `release` preset and recopy. |
| Illegal-instruction crash on Intel CPU | Reconvert with `--to-intel`. Residual register-form SSE4a sites have no runtime trap yet ([relinker spec](spec/relinker.md) Q9). |
| Start-up config error `file:line: key: reason` | Fix or delete the named key. Unknown keys are errors. |
| Config warning, defaults used | Copy `config\` into the install directory. |
| Crash after upgrading PortPS5 | Reconvert the game; old `game.exe` files lack the current config start-up call. |
| Black screen / hang after boot | Likely an unimplemented GPU path. File an issue with the metric summary only, no frames. |
