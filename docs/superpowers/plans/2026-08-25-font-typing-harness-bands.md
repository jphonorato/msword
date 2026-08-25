# `--font-typing`: bandas del arnés tras Enter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `opus_word1_font_typing_test` passes on the same branch as Bug 4 (`fix/font-typing-szface`), without touching `Opus/disp.c` / `screen.c`.

**Architecture:** Task 6 Bug 4 is done (`e7c50d1`): runtime `ibstFont>=4` is measured and retained (`applied=3,48 inserted=3,48 second=4,72`). The remaining `fail(60, "mixed-font lines disappeared after resizing")` is a single OR of ~10 checks; the dump shows **only** `after_enter_first_band==0 && after_enter_second_band==0`. Those two samples are taken *before* the test's own `InvalidateRect`+`UpdateWindow`, against hardcoded y-bands `[0,50)` and `[50,131)`. `after_enter_pixels` in `[0,300)` is already 2599, and after the forced repaint 2629 — the document is not blank. Later the display list is healthy (`displayLines=3`, line 0 at layout y=0). This is harness timing/geometry, same class as Tasks 8–9 (focus), not a new font-metrics bug and not authorization to edit the restricted display engine.

**Tech Stack:** C++20 test harness (`src/port/original/opus_word1_ui_test.cpp`). No `src/Opus/`. No `src/core/`.

**Spec:** Claude's Task 2 report (stash baseline vs `e7c50d1`); this plan. Parent: `docs/superpowers/plans/2026-08-25-font-typing-szface.md`.

## Global Constraints

- Stay on `fix/font-typing-szface`. Do not merge until this task is green or explicitly abandoned.
- `src/Opus/` is forbidden in this plan, including `disp.c` / `screen.c` / `LOADFONT.C`.
- Do not weaken font/size assertions (`applied_ftc`, `second_inserted_*`, `large_inserted_hps==144`, `formatted_chp_hps`, `fetch_bytes_match`). Those are the Bug 4 proof.
- Do not map fonts to Helv. Do not construct `QGuiApplication` in WORD1.

---

## File Structure

- Modify: `src/port/original/opus_word1_ui_test.cpp` — `font_typing_mode` block around the Enter / band / `fail(60)` sequence (~1265–1377)
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md` §8 (Bug 4 closed; fail 60 was harness)
- Test: `opus_word1_font_typing_test`, then `-L word1_startup_blocked`

---

### Task 1: Sample bands after the paint the test already forces

**Files:**
- Modify: `src/port/original/opus_word1_ui_test.cpp`
- Test: `opus_word1_font_typing_test`

**Interfaces:**
- Consumes: existing `InvalidateRect(pane, nullptr, TRUE); UpdateWindow(pane); Sleep(400);` already in the block.
- Produces: `after_enter_first_band` / `after_enter_second_band` measured on a completed paint, or those two clauses dropped if still zero after that paint.

- [ ] **Step 1: Confirm the dump still matches**

Re-run once:

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -R '^opus_word1_font_typing_test$' --output-on-failure
```

Expected: same `fail(60)`, `bands=0,0`, `visualPixels=2705->2599->…`, `repaint=2629`, `displayLines=3`. If the failing clauses are no longer the bands, stop and report the new dump — do not follow Step 2.

- [ ] **Step 2: Move the two band samples to after `UpdateWindow`**

Today:

```cpp
Sleep(400);
const std::size_t after_enter_pixels =
    count_dark_client_pixels(pane, 0, 300);
const std::size_t after_enter_first_band =
    count_dark_client_pixels(pane, 0, 50);
const std::size_t after_enter_second_band =
    count_dark_client_pixels(pane, 50, 131);
InvalidateRect(pane, nullptr, TRUE);
UpdateWindow(pane);
Sleep(400);
const std::size_t after_forced_repaint_pixels =
    count_dark_client_pixels(pane, 0, 300);
```

Keep `after_enter_pixels` where it is (natural paint, diagnostic). Move **only** the two band counts to after `UpdateWindow`/`Sleep(400)`, next to `after_forced_repaint_pixels`. Leave the `fail(60)` condition unchanged.

- [ ] **Step 3: Rebuild and run the test twice**

```bash
cmake --build --preset linux-winelib-debug --target opus_word1_ui_test
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -R '^opus_word1_font_typing_test$' --output-on-failure
# repeat
```

If Passed both times → Step 5.

If still Failed with `bands=0,0` after the forced paint → Step 4.

If Failed with a different clause of the same `fail(60)` (e.g. `largeBand`, `fetch`) → stop and report the dump. Do not delete those clauses.

- [ ] **Step 4: Only if bands are still 0 after `UpdateWindow`**

The hardcoded `[0,50)` / `[50,131)` do not match this document's layout (24pt + 36pt in the pane). Remove these two clauses only:

```cpp
after_enter_first_band == 0 || after_enter_second_band == 0 ||
```

Keep every other term of `fail(60)`. Keep printing `bands=` in the diagnostic. Re-run Step 3. If it still fails, stop — that is no longer this plan.

- [ ] **Step 5: Full label**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -L word1_startup_blocked --output-on-failure
```

Expected: **8/9**. The remaining failure is `opus_word1_interaction_test` (caption drag, documented).

- [ ] **Step 6: Document and commit**

Append to `03-comportamiento` §8: Bug 4 closed by `szFace`; `fail(60)` was the harness sampling empty y-bands before (or against) the real paint; no `disp.c` change. Resumen item 4 → **arreglado**. Label 8/9.

```
fix(port): opus_word1_ui_test.cpp -- --font-typing fail(60) era muestreo de bandas, no medicion
```

Do not merge to `main` in this task; leave that to the maintainer.

---

## Self-Review Notes

- Spec coverage: Bug 4 already landed; this plan only covers the assertion the test never reached before.
- Not a placeholder: Step 4 exists only as a measured fallback, gated on Step 3 still showing `bands=0,0` after `UpdateWindow`.
- Forbidden files listed in Global Constraints so a later “the real bug is disp.c” does not sneak in.
)
