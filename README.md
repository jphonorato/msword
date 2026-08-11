# Word 1.1a Linux Port — Qt core + Winelib oracle

Fork of [jmarshall23/msword](https://github.com/jmarshall23/msword): a historical
port of **Microsoft Word for Windows 1.1a** (codename **Opus**) to Linux.
Two fronts, one repo:

- **Qt core extraction** (`src/core/`, branch prefix `port-qt`) is the primary
  development target as of 2026-08-11. It extracts a Qt6-based,
  platform-independent core from the original Opus source, meant to eventually
  replace parts of the Win32 shell.
- **Winelib port** (`src/Opus/`, `src/port/`; produces `WORD1.exe` +
  `WORD1.exe.so`, a native ELF via `winegcc`/`wrc`/`winebuild`, not Wine-hosted
  PE, not a rewrite) is kept active only as the reference oracle. The Qt port's
  hard constraint is byte-identical pagination against this binary, so it stays
  built and runnable for comparison. It is not paused, but it no longer gets
  active development priority.

> **Fork model:** unidirectional. Linux work lives only in this repository.
> Changes are not contributed back to the upstream Windows/MSVC project.

## Status (2026-08-11)

### Qt core (current priority)

| Phase | Result |
| --- | --- |
| Qt-0 | Win32 coupling inventory complete (`docs/port-qt/00-inventario-win32.md`) |
| Qt-1 | Core/shell boundary designed and closed (`docs/port-qt/01-frontera-nucleo-shell.md`) |
| Qt-2 | In progress: `OpusShellConfig` (settings, `QSettings`-backed) and `OpusShellMemory` (Win16 handle contract) implemented and verified; font-substitution contract (§B2.6) implemented and verified; `WORD1` now links against `opus_shell_config` (Winelib build, see below) |

Remaining core/shell contracts: `OpusShellFontMetrics` (text measurement,
next priority, gates pagination fidelity) and `OpusShellSpine` (message loop,
deliberately last: until then the Winelib binary keeps serving as the
fidelity oracle). Full design rationale:
[`docs/port-qt/01-frontera-nucleo-shell.md`](docs/port-qt/01-frontera-nucleo-shell.md).

### Winelib (reference oracle, infrastructure retained, not actively worked)

| Phase | Result |
| --- | --- |
| 0–1 | Resource generators and host tools for Linux |
| 2 | No formal closing commit in this tree |
| 3 | Motor compiles to **0 errors** (207 TUs → `libopus_original_engine.a`) |
| 4 | `WORD1` linked with **427** exports (`.spec` + `wrc`) |
| 5 | LP64 audit documented (inventory; selective fixes already in tree) |

**Known blocker:** `WORD1` still hits heap corruption during startup/constructors
(Phase 6 / e2e). Engine link and export smoke tests are green. This blocker
isn't being worked while Qt has priority; it matters only insofar as the
oracle needs to stay comparable, not launchable end-to-end.

**CI blocker:** `cmake --preset linux-winelib-debug` fails at Configure in a
clean clone/CI, before building anything. `src/CMakeLists.txt` requires
`src/port/tools/host/` (a native-gcc sub-build for `opus_mkcmd_tool`,
`opus_mkdlg_tool`, `opus_bitapp_tool`, `opus_dibapp_tool`; see line ~150,
`ExternalProject_Add(opus_host_tools_build ...)`), but that directory is
excluded by `.gitignore` (`src/port/tools/host/`) and was never committed, so
it doesn't exist outside whichever machine it was developed on. Confirmed
reproducing on every CI run to date:
[run #1](https://github.com/jphonorato/msword/actions/runs/31350308217),
[run #2](https://github.com/jphonorato/msword/actions/runs/31389677288).
This isn't being fixed under current priority either; noted here for accuracy.

Full technical history: [`docs/port-linux/00-reconocimiento.md`](docs/port-linux/00-reconocimiento.md)
(Winelib) and [`docs/port-qt/`](docs/port-qt/) (Qt core/shell boundary).

## Requirements

Development happens across more than one machine (Debian and Fedora seen so
far), so nothing here assumes a single distro. System-specific paths, like
where `Qt6Config.cmake` lives, resolve dynamically at configure time (see
`src/CMakeLists.txt`'s `qmake6 -query QT_INSTALL_LIBS` lookup), not hardcoded.

- x86-64 Linux, any distro with the packages below
- `wine` / `wine-devel` (provides `winegcc`, `wineg++`, `wrc`, `winebuild`);
  header layout varies by packaging, so the build searches known variants
- Qt6 (`Core` component), found via `qmake6`/`qmake-qt6` if not on the
  default CMake search path
- CMake ≥ 3.25, Ninja, GCC/G++
- Wine prefix initialized once (`wineboot` / `~/.wine`)

## Build (Linux / Winelib)

```bash
git clone https://github.com/jphonorato/msword.git
cd msword/src

cmake --preset linux-winelib-debug
cmake --build --preset linux-winelib-debug --target opus_original_engine
cmake --build --preset linux-winelib-debug --target WORD1
```

Artifacts (local, not committed):

- `build/lib/Debug/libopus_original_engine.a`
- `bin/WORD1.exe` + `bin/WORD1.exe.so` (or paths set by the CMake install layout)

Full rebuild / error dump:

```bash
ninja -k 0 -C out/linux-winelib-debug opus_original_engine
```

The Qt core (`src/core/`) builds as part of the same configure via
`ExternalProject_Add(opus_core_build ...)`, always with the native compiler
(not `winegcc`), independent of the top-level toolchain.

## Windows (upstream capability, retained)

MSVC presets (`x64-debug` / `x64-release`) from the original port remain in
`src/CMakePresets.json`. Linux changes in `src/Opus/` are behind

```c
#if defined(__GNUC__) && !defined(_MSC_VER)
```

so the Windows build path is intended to stay intact.

## Project layout

| Path | Purpose |
| --- | --- |
| `src/Opus/` | Original Microsoft Word/Opus sources (guarded edits only) |
| `src/OpusEtAl/` | Original tools, libraries, and build inputs |
| `src/port/` | Winelib platform layer (x64 runtime, probes); temporary compatibility scaffolding |
| `src/core/` | Qt6 portable core library (native compiler); current development target |
| `src/cmake/` | Toolchain, `.spec` generation, helpers |
| `docs/port-linux/` | Winelib port history and decisions |
| `docs/port-qt/` | Qt core/shell boundary inventory and design |
| `out/`, `build/`, `bin/` | Local build outputs (gitignored) |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). File issues against **this** repo only;
do not open PRs against jmarshall23/msword for Linux work.

## License

See [LICENSE](LICENSE). Historical Word 1.1a sources retain their original
provenance; port modifications are under MIT as stated there.

## Credits

- Original Word 1.1a / Opus: Microsoft (1989)
- Windows x64 port baseline: [jmarshall23/msword](https://github.com/jmarshall23/msword)
- Linux port (Winelib + Qt core): jphonorato
