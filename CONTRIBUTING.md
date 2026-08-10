# Contributing to Word 1.1a for Linux

This is a historical port of Word 1.1a to Linux via Winelib. Contributions
are welcome, but please understand the project scope and constraints.

## Project Scope

- **Objective:** Run Word 1.1a as a native ELF binary on Linux (x86-64)
- **Method:** Winelib (winegcc, wrc, winebuild) — no MSVC, no separate port from scratch
- **Status:** Phases 0–5 complete (motor compiled, exports linked, LP64 audited)

## Before Contributing

1. Read `docs/port-linux/00-reconocimiento.md` — comprehensive technical history
   - Describes all phases (0–5) and decisions taken
   - Lists known limitations and blocked issues
   - Explains architecture choices (guardas `#if __GNUC__`, etc.)

2. Understand the restricted trees:
   - `src/Opus/` and `src/OpusEtAl/` — Microsoft original code
   - Changes require explicit authorization (file an issue first)
   - All modifications use `#if defined(__GNUC__) && !defined(_MSC_VER)` guards
   - MSVC builds (`x64-debug` preset) must remain unchanged

3. Review the fork model:
   - This is a **unidirectional fork** — no PRs upstream to jmarshall23/msword
   - Linux-specific work stays here; Windows fixes come from upstream (via git fetch)

## Building

From the repository root:

```bash
cd src
cmake --preset linux-winelib-debug
cmake --build --preset linux-winelib-debug --target opus_original_engine
cmake --build --preset linux-winelib-debug --target WORD1
```

Verify:

```bash
ninja -k 0 -C out/linux-winelib-debug opus_original_engine
# Should show: 0 FAILED (or "ninja: no work to do")
```

Requirements (Fedora example): `wine` / `wine-devel`, `cmake`, `ninja-build`, `gcc-c++`.

## Known Limitations

- **WORD1 crashes on startup:** heap corruption in constructors (Phase 6 blocker)
- **dktString 64-bit:** macros with `Declare ... As String` — packing landed in Phase 3; full runtime still needs Phase 6 validation
- **Serialization:** document format changes under LP64 (sizeof differences)

## Opening Issues

Include:

- Phase/area affected (e.g., "Phase 6: e2e startup")
- Expected vs. actual behavior
- Reproduction steps (if applicable)
- Reference to `00-reconocimiento.md` section (if relevant)

## PR Guidelines

- Test: `ninja -k 0` must complete with 0 errors on the motor target
- Document: add a note to `docs/port-linux/00-reconocimiento.md` if architecture changes
- Guard: all `src/Opus/` changes use `#if defined(__GNUC__) && !defined(_MSC_VER)`
- No force-push; history is part of the documentation

## Contact

Questions? Start with `docs/port-linux/00-reconocimiento.md` — it likely covers it.
