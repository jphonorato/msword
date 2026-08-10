# Word 1.1a for Linux (Winelib)

Fork of [jmarshall23/msword](https://github.com/jmarshall23/msword): a historical
port of **Microsoft Word for Windows 1.1a** (codename **Opus**) toward a
**native ELF binary on Linux** via Winelib (`winegcc`, `wrc`, `winebuild`).

This is **not** a reimplementation with a modern editor control, and it is
**not** “Word under Wine as a PE `.exe`”. The goal is a Winelib app
(`WORD1.exe` stub + `WORD1.exe.so` ELF) linked against Wine’s Win32 APIs.

> **Fork model:** unidirectional. Linux/Winelib work lives only in this
> repository. Changes are not contributed back to the upstream Windows/MSVC
> project.

## Status (2026-08-09)

| Phase | Result |
| --- | --- |
| 0–1 | Resource generators and host tools for Linux |
| 3 | Motor compiles to **0 errors** (207 TUs → `libopus_original_engine.a`) |
| 4 | `WORD1` linked with **427** exports (`.spec` + `wrc`) |
| 5 | LP64 audit documented (inventory; selective fixes already in tree) |

**Known blocker:** `WORD1` still hits heap corruption during startup/constructors
(Phase 6 / e2e). Engine link and export smoke tests are green.

Full technical history: [`docs/port-linux/00-reconocimiento.md`](docs/port-linux/00-reconocimiento.md).

## Requirements (Linux)

- Fedora 40+ (developed on Fedora 44) or similar x86-64 distro
- `wine` / `wine-devel` (provides `winegcc`, `wineg++`, `wrc`, `winebuild`)
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

## Windows (upstream capability, retained)

MSVC presets (`x64-debug` / `x64-release`) from the original port remain in
`src/CMakePresets.json`. Linux changes in `src/Opus/` are behind

```c
#if defined(__GNUC__) && !defined(_MSC_VER)
```

so the Windows build path is intended to stay intact. Primary development and
CI for **this** fork target Linux/Winelib.

## Project layout

| Path | Purpose |
| --- | --- |
| `src/Opus/` | Original Microsoft Word/Opus sources (guarded edits only) |
| `src/OpusEtAl/` | Original tools, libraries, and build inputs |
| `src/port/` | Platform layer (Winelib, x64 runtime, probes) |
| `src/cmake/` | Toolchain, `.spec` generation, helpers |
| `docs/port-linux/` | Port history and decisions (source of truth) |
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
- Linux/Winelib port: jphonorato
