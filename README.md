# Word 1.1a Linux Port: Qt core + Winelib oracle

Fork of [jmarshall23/msword](https://github.com/jmarshall23/msword): a historical
port of **Microsoft Word for Windows 1.1a** (codename **Opus**) to Linux.
Two fronts, one repo:

- **Winelib port** (`src/Opus/`, `src/port/`; produces `WORD1.exe` +
  `WORD1.exe.so`, a native ELF via `winegcc`/`wrc`/`winebuild`, not Wine-hosted
  PE, not a rewrite) is the active development target since 2026-08-14.
  Its long-standing startup blocker turned out to be specific to an
  unsupported dev machine; it doesn't reproduce on Debian 13, the only
  platform this port targets. Details below and in
  `docs/port-linux/01-heap-corruption-startup-diagnosis.md`.
- **Qt core extraction** (`src/core/`, branch prefix `port-qt`) extracts a
  Qt6-based, platform-independent core from the original Opus source, meant
  to eventually replace parts of the Win32 shell. It's paused while Winelib
  has priority; the Winelib binary keeps serving as its fidelity oracle
  regardless.

Debian 13 (trixie) is the only supported platform, because its GCC 14.2.0 is
what the port's compatibility guards actually target: chasing other distros
or GCC versions wouldn't move the port forward. See Requirements below.

> **Fork model:** unidirectional. Linux work lives only in this repository.
> Changes are not contributed back to the upstream Windows/MSVC project.

## Status (2026-09-01)

### Winelib (current priority)

| Phase | Result |
| --- | --- |
| 0-1 | Resource generators and host tools for Linux |
| 2 | No formal closing commit in this tree |
| 3 | Engine compiles to **0 errors** (207 TUs → `libopus_original_engine.a`) |
| 4 | `WORD1` linked with **427** exports (`.spec` + `wrc`) |
| 5 | LP64 audit documented (inventory; selective fixes already in tree) |
| 6 | Closed for the supported platform: startup blocker traced to an unsupported dev machine, doesn't reproduce on Debian 13 (below) |

**Phase 6 (e2e), closed for the supported platform:** `WORD1` hit heap
corruption during startup for months, deep inside the toolbar's font-combo
sync (`sync_combo`/`CB_ADDSTRING`).
`docs/port-linux/01-heap-corruption-startup-diagnosis.md` tracks the
investigation session by session, including a full audit of that code path
against Wine's own builtin ComboBox/ListBox source that turned up nothing on
either side. On 2026-08-14, attaching `gdb` to the running process in the
project's Debian 13 container settled it: the same build that crashes on
Arch/wine-staging 11.15 runs clean on Debian 13's plain wine 10.0, all the
way to `NtUserGetMessage`'s normal idle wait, with a real
"Microsoft Word - Document1" window on screen. The crash was never
reproduced on Debian 13, neither on the VPS nor in this container, so it
isn't being chased further: Arch was never the target.

**Since then:** the on-disk `.doc` corruption bug (a 768-byte native `FIB`
struct overflowing a 512-byte disk sector page, a real buffer overflow, not
cosmetic) is fixed; a CRLF/`ccpEop` mismatch that silently miscounted
character positions across saves is fixed too. Both are covered by an
end-to-end roundtrip test, and the saved `.doc` files are now checked from
*outside* the engine as well: `doc_inspector`
(`src/port/tools/doc_inspector/`, native C++20, no Wine/Win32) reads the FIB,
the CHPX/PAPX FKPs, and the PLC tables directly off disk. See Tests below.

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

### Qt core (paused)

| Phase | Result |
| --- | --- |
| Qt-0 | Win32 coupling inventory complete (`docs/port-qt/00-win32-inventory.md`) |
| Qt-1 | Core/shell boundary designed and closed (`docs/port-qt/01-core-shell-boundary.md`) |
| Qt-2 | `OpusShellConfig` (settings, `QSettings`-backed) and `OpusShellMemory` (Win16 handle contract) implemented and verified; font-substitution contract (§B2.6) implemented and verified; `WORD1` links against `opus_shell_config` |

Remaining core/shell contracts: `OpusShellFontMetrics` (text measurement,
gates pagination fidelity) and `OpusShellSpine` (message loop, deliberately
last: the Winelib binary keeps serving as the fidelity oracle until then).
Development here is on hold while Winelib has priority. Full design
rationale: [`docs/port-qt/01-core-shell-boundary.md`](docs/port-qt/01-core-shell-boundary.md).

Full technical history: [`docs/port-linux/00-reconnaissance.md`](docs/port-linux/00-reconnaissance.md)
(Winelib) and [`docs/port-qt/`](docs/port-qt/) (Qt core/shell boundary).

## Tests

Registered with CTest, from the build dir:

```bash
ctest --test-dir out/linux-winelib-debug            # all
ctest --test-dir out/linux-winelib-debug -R <name>  # single test
```

Two groups:

- **Gating** (unit-level, no `WORD1` launch required): `opus_original_strtbl_test`,
  `opus_x64_runtime_test`, `opus_original_sttb_test`, `opus_original_plc_test`,
  `opus_sdm_cab_test`, `opus_original_command_test`, and (Winelib builds only)
  `opus_shell_config_test`, `opus_shell_font_substitution_test`,
  `opus_shell_memory_foreign_test`.
- **Labeled `word1_startup_blocked`**: the 10 `opus_word1_*` UI tests driven
  through `opus_word1_ui_test` (smoke, clipboard, typing, interaction,
  selection, font-typing, about, save-as, roundtrip, rich-format) plus
  `opus_doc_inspector_test`, which validates the `.doc` files those tests
  write, from outside the engine, via `doc_inspector`
  (`src/port/tools/doc_inspector/`). All depend on launching `WORD1`;
  registered and run so CI has visibility, but treated as non-gating.

**Current tally (2026-09-01, this VPS + Xvfb, `main`): 19/21 overall,
gating 9/9, `word1_startup_blocked` 10/12.** The two known failures are both
environmental, not code regressions: `opus_word1_interaction_test` (a
Wine/Xvfb caption-drag limitation, reproduces on builtin `notepad` too) and
`opus_word1_font_typing_test` (the ribbon combo dropdown doesn't open under
Wine/Xvfb as of 2026-09-01, cause not yet identified; see
`docs/port-linux/03-word1-startup-blocked-behavior.md` §16). Roundtrip,
rich-format, formatting (bold/italic/alignment survive a real Save As and
reopen), and `doc_inspector`'s binary validation all pass clean. Full history:
`docs/port-linux/01-heap-corruption-startup-diagnosis.md` §22-29 and
`docs/port-linux/03-word1-startup-blocked-behavior.md`.

`src/core` (the Qt-2 core library) has its own CTest registrations gated
behind `OPUS_CORE_BUILD_TESTS`.

## Requirements

**Supported platform: Debian 13 (trixie) only.** This isn't a portability
project: the goal is getting the port working on Linux, not chasing every
distro or GCC release. Debian 13 ships GCC 14.2.0, which is the actual
reference toolchain: both compatibility guards the port needs
(`-std=gnu89 -funsigned-char -fms-extensions -fpermissive` for GCC 14's
default-error implicit-int/implicit-function-declaration crackdown, and
`#if __GNUC__ < 15` in a couple of `Opus/` headers for a real GCC bug,
flexible array members in unions, fixed upstream in GCC 15/PR53548, that
Debian 13's GCC 14 still needs worked around) exist *because of* this
target, not despite it. Verified building clean (0 errors) on a live
Debian 13 box; see `docs/port-linux/00-reconnaissance.md` §6.3 and
`docs/port-qt/01-core-shell-boundary.md`'s "Applied and verified" note.
It happens to also build on newer GCC (the guards are version-gated, not
Debian-specific), but that's incidental, not a maintained target; no
further multi-distro/multi-GCC-version validation work is planned.

- x86-64 Debian 13 (trixie)
- `wine` / `wine-devel` (provides `winegcc`, `wineg++`, `wrc`, `winebuild`)
- Qt6 (`Core` component), found via `qmake6`/`qmake-qt6` if not on the
  default CMake search path
- CMake ≥ 3.25, Ninja, GCC/G++ (GCC 14.2.0, Debian 13's default)
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
| `src/port/` | Winelib platform layer (x64 runtime, probes, `doc_inspector`); temporary compatibility scaffolding |
| `src/core/` | Qt6 portable core library (native compiler); paused, see Status |
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
