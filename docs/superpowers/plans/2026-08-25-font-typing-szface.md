# `--font-typing`: `szFace` en `OpusFontKey` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `opus_word1_font_typing_test` passes because `C_LoadFcid` can measure a runtime font (`Liberation Sans`, `ibstFont >= 4`) instead of taking `LSystemFontErr` when `EraNameFromFtc` returns NULL.

**Architecture:** The nucleus already translates (`docs/port-qt/01-frontera-nucleo-shell.md` §B2.2, open question #3 option a). Today that translation is a 4-entry table *inside the shell*, so a name that only exists in `vhsttbFont` never arrives. Add `const char *szFace` to `OpusFontKey` (NULL = old behavior). `LOADFONT.C` fills it from `PstFromSttb(vhsttbFont, fcid.ibstFont)->szFfn`. WORD1 has no `QGuiApplication`, so widths come from `OpusPortGdiCharWidths` using that face name. The QRawFont/era-file path for ftc 0–3 is unchanged (2660-point oracle stays green).

**Tech Stack:** C++17 (`src/core`), C (`Opus/LOADFONT.C`, Linux guard only), Winelib GDI fallback (`opus_original_startup_probe.cpp`).

**Spec:** `docs/port-linux/03-comportamiento-word1-startup-blocked.md` §8 seventh update (Task 6 Bug 4); `docs/port-qt/01-frontera-nucleo-shell.md` §B2.2 / pregunta abierta #3; `src/core/src/OpusShellFontMetrics.cpp` limitation 1.

## Global Constraints

- Debian 13, preset `linux-winelib-debug`. Do not construct a `QGuiApplication` inside WORD1 (known hang).
- `src/Opus/` is restricted: Linux-only `#if defined(__GNUC__) && !defined(_MSC_VER)`. The MSVC `#else` of `C_LoadFcid` is GDI and stays byte-identical.
- **Rejected:** map `ftc >= 4` to Helv. On this box Helv and Liberation Sans share a TTF by accident; DejaVu/Noto would paginate with the wrong widths.
- **Rejected:** only un-gate the GDI fallback. `OpusPortGdiCharWidths` has its own 4-entry `switch (key->ftc)` and still returns -1 for ftc 4.
- Do not implement bold/italic synthesis (`catr != 0` still fails on the QRawFont path). Do not touch printer/preview. Do not change `OpusShellSubstituteFontFile` (still the 4 era names).
- Do not migrate `res.c` / D / `wLow`. Memory track is closed (`cbb8a42` / `2e33d67`).

---

## File Structure

- Modify: `src/core/include/OpusShellFontMetrics.h` — `OpusFontKey.szFace`
- Modify: `src/core/src/OpusShellFontMetrics.cpp` — resolve name, GDI-fallback gate, QRawFont for non-era when GUI exists
- Modify: `src/port/original/opus_original_startup_probe.cpp` — `OpusPortGdiCharWidths` uses `szFace`
- Modify: `src/Opus/LOADFONT.C` — fill `szFace` (existing GNU block only)
- Modify: `src/core/src/OpusShellFontMetrics_test.cpp` — ftc=4 + szFace succeeds; ftc=99 without szFace still fails
- Modify: `docs/port-linux/03-comportamiento-word1-startup-blocked.md` — Bug 4 closed with the real fix
- Test: `opus_shell_font_metrics_test`, `opus_shell_font_metrics_fidelity_test`, `opus_word1_font_typing_test`, label `word1_startup_blocked`

Existing C++ aggregate inits `OpusFontKey key{0, 28, 0}` remain valid: a new trailing pointer is value-initialized to NULL.

---

### Task 1: Extend `OpusFontKey` and the shell control flow

**Files:**
- Modify: `src/core/include/OpusShellFontMetrics.h`
- Modify: `src/core/src/OpusShellFontMetrics.cpp`
- Modify: `src/core/src/OpusShellFontMetrics_test.cpp`
- Test: `opus_shell_font_metrics_test`, `opus_shell_font_metrics_fidelity_test`

**Interfaces:**
- Consumes: `OpusShellSubstituteFontFile`, existing `EraNameFromFtc` for ftc 0–3 when `szFace` is NULL.
- Produces: `OpusFontKey { int ftc, int ps, int catr, const char *szFace; }` with `szFace` optional.

- [ ] **Step 1: Add the field**

In `OpusFontKey`, after `catr`:

```c
    const char *szFace; /* optional ANSI face from vhsttbFont; NULL => EraNameFromFtc(ftc) */
```

Update the header comment: the contract still covers the 4 era `ftc`s when `szFace` is NULL; a non-NULL `szFace` is the nucleus translation for runtime fonts.

- [ ] **Step 2: Resolve the face name once**

In `OpusShellFontMetrics.cpp`, replace “EraNameFromFtc or die” inside `RawFontFor` with:

```c
const char *FaceNameFor(const OpusFontKey *key) {
    if (key == nullptr) {
        return nullptr;
    }
    if (key->szFace != nullptr && key->szFace[0] != '\0') {
        return key->szFace;
    }
    return EraNameFromFtc(key->ftc);
}
```

`RawFontFor` uses `FaceNameFor(key)` instead of `EraNameFromFtc(key->ftc)`. If that is NULL → `why = "ftc"` (unchanged: ftc 99, no szFace, still controlled failure).

If `OpusShellSubstituteFontFile(name)` returns a path, keep the current `QRawFont(QString::fromUtf8(file), px, QFont::PreferFullHinting)` path. That is the measured era oracle. Do not send era names through `QRawFont::fromFont`.

If there is no substitution file and `CanUseRawFont()`:

```c
QFont qf(QString::fromLatin1(name));
qf.setPixelSize(px);
qf.setHintingPreference(QFont::PreferFullHinting);
QRawFont rf = QRawFont::fromFont(qf, QFont::PreferFullHinting);
```

If `!rf.isValid()` → `why = "qrawfont-invalid"`. This path is for core tests / `opus_qt_shell`, not WORD1.

If there is no substitution file and `!CanUseRawFont()` → `why = "no-gui-app"` (do not return `"ftc"` or `"no-file"`). WORD1 must reach the GDI fallback.

- [ ] **Step 3: GDI fallback is gated on “no QRawFont”, not on a why-code**

Rewrite `OpusShellCharWidths` (and the matching `no-gui-app` branch of `OpusShellFontMetrics`) so that:

1. Invalid args still return -1.
2. `RawFontFor` as today for the QRawFont success path.
3. On failure: if `!CanUseRawFont()` and `g_char_widths_fallback != nullptr`, call the fallback **regardless of `why`** (`ftc`, `no-file`, `no-gui-app` all OK). Fallback already handles `catr` via `LOGFONT`.
4. Else if `why == "no-gui-app"` and `catr == 0`, oracle table for era names only (existing).
5. Else -1.

Do not construct `QGuiApplication`. `catr != 0` on the QRawFont path still fails controlled.

- [ ] **Step 4: Tests**

Keep: `badFtc{99, 24, 0}` with implicit `szFace == NULL` still fails FontMetrics and CharWidths.

Add, inside the existing `QGuiApplication` test (Liberation fonts already required):

```cpp
{
    OpusFontKey key{4, 24, 0, "Liberation Sans"};
    unsigned short w = 0xFFFF;
    Check(OpusShellCharWidths(&key, 'A', 1, &w) == 0,
          "ftc=4 szFace=Liberation Sans deberia medir");
    Check(w > 0 && w != 0xFFFF, "Liberation Sans ancho degenerado");
}
{
    OpusFontKey key{4, 24, 0, nullptr};
    unsigned short w = 0;
    Check(OpusShellCharWidths(&key, 'A', 1, &w) != 0,
          "ftc=4 sin szFace debe seguir fallando controlado");
}
```

- [ ] **Step 5: Build and run core tests**

```bash
cd /home/pablo/msword/src
cmake --build --preset linux-winelib-debug --target opus_shell_font_metrics_test opus_shell_font_metrics_fidelity_test
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -R 'opus_shell_font_metrics' --output-on-failure
```

Expected: both tests Passed. Fidelity still 2660/2660. If fidelity moves, stop; do not “fix” the oracle table.

- [ ] **Step 6: Commit**

```
fix(core): OpusFontKey.szFace -- el nucleo pasa el nombre real; ftc>=4 ya no es un callejón
```

---

### Task 2: GDI fallback and `LOADFONT.C` (the WORD1 path)

**Files:**
- Modify: `src/port/original/opus_original_startup_probe.cpp` (`OpusPortGdiCharWidths`)
- Modify: `src/Opus/LOADFONT.C` (existing GNU block around `shellKey`, ~460–492)
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md` §8
- Test: `opus_word1_font_typing_test`, then `-L word1_startup_blocked`

**Interfaces:**
- Consumes: Task 1 (`szFace`, fallback gate).
- Produces: WORD1 measuring `Liberation Sans` via `GetCharWidthA` on a `LOGFONT` with that face.

- [ ] **Step 1: `OpusPortGdiCharWidths`**

Replace the `switch (key->ftc)` that returns -1 on default with:

```c
const char *face = nullptr;
if (key->szFace != nullptr && key->szFace[0] != '\0') {
    face = key->szFace;
} else {
    switch (key->ftc) {
        case 0: face = "Tms Rmn"; break;
        case 1: face = "Symbol"; break;
        case 2: face = "Helv"; break;
        case 3: face = "Courier"; break;
        default: return -1;
    }
}
```

Keep the rest (hps==0 → 10pt, Courier positive `lfHeight`, `GetCharWidthA` / `GetTextExtentPoint32A`). `lfCharSet`: if `szFace` was used, `ANSI_CHARSET` (runtime ribbon fonts). Era `ftc==1` without szFace stays `SYMBOL_CHARSET`.

- [ ] **Step 2: `LOADFONT.C` — fill `szFace`, memset the key**

Still inside the existing `#if defined(__GNUC__) && !defined(_MSC_VER)` block that sets `shellKey`. `vhsttbFont` is already `extern` in this file.

Before assigning fields:

```c
memset(&shellKey, 0, sizeof shellKey);
```

After `shellKey.ftc = fcid.ibstFont`:

```c
{
    struct FFN *pffn = PstFromSttb(vhsttbFont, fcid.ibstFont);
    if (pffn != NULL)
        shellKey.szFace = pffn->szFfn;
}
```

`szFfn` is a C string (`fontwin.h`: “FFN is a funny st” but `szFfn` is NUL-terminated; do not treat it as a Pascal `st`). The pointer is only live across the `OpusShellCharWidths` call; do not copy into a heap buffer.

Do not change the `goto LSystemFontErr` policy: if CharWidths still returns -1 (printer, preview, catr, OOM), keep degrading. This task only removes the false -1 for a valid runtime face.

- [ ] **Step 3: Rebuild WORD1 and the UI test**

```bash
cmake --build --preset linux-winelib-debug --target WORD1 opus_word1_ui_test
```

- [ ] **Step 4: Verify `--font-typing` and the label**

Needs DISPLAY (Xvfb is fine if that is how this machine already runs the label):

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -R '^opus_word1_font_typing_test$' --output-on-failure
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -L word1_startup_blocked --output-on-failure
```

Expected: font-typing **Passed**. Label **8/9**. The remaining failure is `opus_word1_interaction_test` (Wine/Xvfb caption-drag, documented, out of scope). If font-typing still fails, capture the exact `fail()` message and stop — do not map to Helv.

- [ ] **Step 5: Document**

Append to §8 of `03-comportamiento-word1-startup-blocked.md` a short “octava actualización”: Bug 4 fixed by `szFace` + GDI fallback; Helv-fallback rejected; `EraNameFromFtc` 0–3 unchanged. Update the Resumen item 4 from “parcial / sin arreglar” to **arreglado**.

- [ ] **Step 6: Commit**

```
fix(port): LOADFONT.C, OpusPortGdiCharWidths -- mide fuentes de runtime (Task 6 Bug 4, --font-typing)
```

One commit for Task 2 (LOADFONT is restricted; keep it with the fallback that makes the call succeed). If Task 1 is not yet on main, this commit may include Task 1 files — prefer two commits as written.

---

## Self-Review Notes

- Spec coverage: Bug 4 root cause (ftc≥4 → NULL → LSystemFontErr) → Tasks 1–2. Fidelity 2660 → Task 1 Step 5, era file path untouched. Open question #3 (a) nucleus translates → `szFace` filled in LOADFONT, not a second table in the shell.
- Placeholder scan: no TBD. `fromFont` is specified for the GUI/non-era path only.
- Type consistency: `szFace` is `const char *` in the header, LOADFONT, GDI fallback, and the new test.
- Control-flow trap already paid: gating the fallback on `why=="no-gui-app"` made `why=="ftc"` skip GDI. Task 1 Step 3 exists so that does not recur.
)
