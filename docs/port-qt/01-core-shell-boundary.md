# Phase Qt-1: Core/shell boundary design

**Status:** design closed; Qt-2 implementation in progress since 2026-08-11/12.
The original text of this field ("design, not implemented") described the
phase in which this document was opened; it is corrected here because the
rest of the document (§B3.5, §B4.4, §B5.1/§B5.2) already records real
implementation that line contradicted. See "Actual status as of today"
below before anything else.
**Input:** `docs/port-qt/00-win32-inventory.md`, in its version after the
exclusion of comments and literals.
**Closed scope decisions:** byte-identical pagination fidelity against the
Winelib oracle; `Opus/interp/` is core; `OpusEtAl/` by individual verdict
(54 exclude, 4 defer); `Opus/debug/` is ported.
**Boundary API:** four headers in `src/core/include/`, all four with real
implementation today (none remained at "declaration only"):
`OpusShellConfig.h`/`OpusShellMemory.h` (§B5/§B3, verified with real
cross-toolchain linking and call sites migrated in `Opus/`),
`OpusShellFontMetrics.h` (§B2, verified with 2660 fidelity data points),
`OpusShellSpine.h` (§B4.4, only the two fragments with a concrete signature,
`OpusShellReportError`/`OpusShellAlert`, not the full inversion of the
message loop, which is still not started).

---

## Actual status as of today, for anyone not going to read 1900 lines

Added 2026-08-14 from reading the code, not this document. The question
that motivated it was "is this a real port to Qt, or is it smoke tests?".
Short answer, on two axes that cannot be merged into a single verdict: as a
runtime dependency that `WORD1` actually executes on every startup, yes,
it already is Word on top of Qt (see below). As architecture (who owns the
window and the event loop), not yet: that is still Win32/Wine end to end.
And the tests: those of `src/core` are not smoke tests, the `WORD1`
startup/interaction ones (label `word1_startup_blocked`) are. Verifiable
detail:

### What is real and is actually linked into the `WORD1` that is shipped

These are not just headers or an unused library. `src/CMakeLists.txt`
links the `.a` files from `src/core` directly into the `WORD1` target
(lines ~214-218, 280+), with real call sites already committed inside
`Opus/` (restricted tree) that call them:

| Contract | Real Qt backend | Call sites in `Opus/` | Commit |
|---|---|---|---|
| `OpusShellConfig` (§B5) | `QSettings` | 41, `OpusShellProfile*` in 12 files (e.g. `quit.c`) | `e298420` |
| `OpusShellMemory` (§B3) | malloc/realloc/free with a pin counter | 3 in `catalog.c` (`HGrabFarMem`, `FAllocDMFarMem`, `FreeDMFarMem`); ~198 remaining `Global*` sites not migrated | `c4e9ff0`, `2a36b1a` |
| `OpusShellSpine` (§B4.4) | `QMessageBox` / `QApplication::beep()` | 1, `wordtech/error.c:1630`; `editspec.c`/`undo.c` (`OpusShellAlert`) not connected | `ea5f908` |
| `OpusShellFontMetrics`/`FontSubstitution` (§B2) | `QRawFont` | 1, `Opus/LOADFONT.C:187 C_LoadFcid`; no runtime verification against real `WORD1` (§B2.7) | (none) |

The `src/core` tests are not smoke tests either: `OpusShellConfig_test.cpp`
verifies the documented Win16 Profile semantics point by point (not just
"compiles and returns 0"), and `OpusShellFontMetrics_fidelity_test.cpp`
compares against 2660 measurements captured from the real Winelib oracle
(§B2.3): 2660/2660 match.

### Corrected the same day: "Word on top of Qt" is true on the runtime-dependency axis

The first version of this section verified source code (call sites, CMake)
but not the resulting binary, and drew too flat a conclusion from that.
Verified with `ldd bin/WORD1.exe.so`:

```
libQt6Widgets.so.6 => /usr/lib/libQt6Widgets.so.6
libQt6Gui.so.6     => /usr/lib/libQt6Gui.so.6
libQt6Core.so.6    => /usr/lib/libQt6Core.so.6
libQt6DBus.so.6    => /usr/lib/libQt6DBus.so.6
```

Not a cosmetic detail: `WORD1.exe.so` does not start without these
libraries present and at the correct version, the gotcha already
documented in `CLAUDE.md` (host's Qt 6.11.1 versus the Debian 13
container's 6.8.2, `dlopen` failing with `version 'Qt_6.11' not found`,
masked as a misleading `ShellExecuteEx failed: File not found`) is proof
that this dependency is real and gets resolved on every startup, not a
test artifact. In that concrete sense, Qt6 as a runtime dependency that
the binary actually executes, both on the startup path and in the calls
in the table above, **"this is Word on top of Qt" is correct, and the
original conclusion of this section ("neither of the two is Word running
on Qt yet") was overgeneralizing.**

### The axis that remains unresolved: who controls the window and the event loop

This is a different axis and must not be collapsed with the previous one.
`WORD1.exe.so` still runs the full Win32 message loop (`GetMessage`/
`DispatchMessage` via Winelib) end to end; no `QWidget` is part of its
visible interface; the four contracts are purely backend (persistence,
memory, text measurement, a modal dialog triggered from inside that Win32
loop, not the other way around). It is the code itself that says so, not
an inference of this document (`src/core/src/OpusShellSpine.cpp`):

> "Real WORD1 today runs with the Win32 message loop (GetMessage/
> DispatchMessage), not with QApplication -- the control inversion (step 7
> of the Qt-2 sequence) is still not done."

There does exist a binary that runs under a real `QApplication::exec()`,
`opus_qt_shell` (`src/core/src/opus_qt_shell_main.cpp`), but its own
header comment is explicit: *"This is NOT Word under Qt. There is no
document engine, wordtech/ is not connected."* It is a mold that
demonstrates that the dispatch pattern of §B4.2 (`SendMessage` to direct
call, `PostMessage` to `QMetaObject::invokeMethod` with
`Qt::QueuedConnection`) works against the contracts already closed, to be
reused the day `wordtech/` is really connected, not a second
implementation of Word.

And the tests that are actual smoke tests, under that same name in the
project itself, are others: the `word1_startup_blocked` label
(`word1_port_smoke_test` plus the 8 `opus_word1_ui_test`) requires `WORD1`
to start and respond to real interaction via Wine/Winelib, today they are
still at 0/9 truly passing (they reach interaction logic, fail with
app-level messages; see `README.md`, Tests section). Those are the
project's smoke tests, and they are unrelated to `src/core` work.

### In one sentence

There are two real efforts coexisting in the same repo, and both are true
at once without contradicting each other: the Winelib port remains, today,
the sole owner of the event loop and the `WORD1` window, but that same
`WORD1` already **is** Word on top of Qt in the literal sense that it does
not start or run without Qt6, and it executes real Qt code (`QSettings`,
an allocator, `QRawFont`, `QMessageBox`) in every one of the calls in the
table above. What is missing for "Word on top of Qt" in the full
architectural sense, Qt owning the window and the loop, not just a backend
linked underneath, is the control inversion of step 7, still untouched in
`Opus/wproc.c`. `opus_qt_shell` is a third artifact, an isolated
demonstrator without a document engine, not a second implementation of
Word. See "Open questions" #4 for whether this changes the argument for
separating repositories.

---

## Inventory changes this design forced

While inspecting the two *GDI text/fonts* sites within `Opus/wordtech/`,
one turned out to be a false positive: `format.c:2165` had `TextOut`
inside a comment. One occurrence of `GetTextExtent` in `dispspec.c:539`
was inside a string literal. Twice the same failure mode via independent
paths, so the scan was fixed at the root: `audit_win32_v2.py` now strips
comments and literals before tokenizing.

Effect on the figures this document uses:

| Magnitude | Before | After |
|---|---|---|
| *GDI text/fonts* sites | 200 | **170** |
| TUs with *GDI text/fonts* | 27 | **24** |
| *Message spine* sites | 323 | **287** |
| *Win16 memory* sites | 206 | **201** |
| *Configuration* sites | 44 | **43** |
| ABI sites resolved by Winelib | 2407 | **2058** |
| `Opus/wordtech/` portable | 21/40 | **22/40** |
| Portable / boundary / presentation TUs | 58 / 43 / 86 | **60 / 44 / 83** |

`wordtech/format.c` moved from *presentation* to *portable*, and with that
**`Opus/wordtech/` has only one TU that touches text measurement:
`layoutap.c`.** The detail of the sweep is in the corresponding section of
the inventory.

---

## B1: Qt core / shell classification

The 83 *presentation* TUs are shell by default and are not analyzed one by
one, except those in the core region (§B1.3). The 60 *portable* and 44
*boundary* TUs are classified.

### B1.1 The 60 portable TUs go to core, no exceptions

They only touch primitive types, ABI conventions already neutralized by
Winelib, or Win16 constants. They enter the core with the typedef layer,
without rewriting. Distribution: `Opus/` root 27, `Opus/wordtech/` 22,
`Opus/interp/` 3, `Opus/debug/` 6, `port/original/` 2.

### B1.2 The 44 boundary TUs: 32 core, 10 shell, 2 diagnostic

Their coupling is only to handle types, Win16 memory, geometry, or
configuration: nothing that demands rewriting. But the technical verdict
does not decide placement alone, the file's role also matters. A dialog
file whose only coupling is `HWND` remains a dialog.

**Core (32).** Document logic, behind the boundary API:

| Region | TUs |
|---|---|
| `Opus/wordtech/` (4) | `clsplc.c`, `curskeys.c`, `ihdd.c`, `outline.c` |
| `Opus/interp/` (4) | `exp.c`, `main.c`, `sym.c`, `to.c` |
| `Opus/` root (24) | `catalog.c`, `cmd2.c`, `cmdcore.c`, `compare.c`, `customiz.c`, `docman1.c`, `elfile.c`, `elsubs2.c`, `elsubs3.c`, `etcmd.c`, `fieldclc.c`, `fieldpic.c`, `fieldsc2.c`, `fieldspc.c`, `filecvt.c`, `glsy.c`, `hddwin.c`, `index1.c`, `replace.c`, `sort.c`, `spelcore.c`, `style.c`, `stylesub.c`, `toc.c` |

`curskeys.c` deserves a note: it translates cursor keys into document
movement. It stays in the core and receives events already translated by
the shell; it does not read keyboard state.

**Shell (10).** Dialogs, windows, and menu help, whose role is
presentation even though their measured coupling is light: `curswin.c`,
`dialog3.c`, `dlglook1.c`, `dlglook2.c`, `dlgmisc.c`, `dlgopen.c`,
`dlgrec.c`, `dlgtable.c`, `filewin.c`, `menuhelp.c`. They are rewritten in
Qt-5, not ported.

**Diagnostic (2).** `Opus/debug/debugcmd.c` and `debugdde.c`: by project
decision `Opus/debug/` is ported, but it is instrumentation, not document.
They go into a diagnostic component of the shell, without entering the
core or blocking any phase.

### B1.3 The 14 TUs that require extraction before deciding

They are in the core region (`wordtech/`) but classified *presentation*:
they contain document logic mixed with presentation. They are not "shell
by default"; they must be separated. It is the most delicate work of Qt-2
and the reason Qt-1 cannot close the classification at 100%.

| TU | Coupling that anchors it | Nature of the mix |
|---|---|---|
| `layoutap.c` | GDI text + drawing | *Autotext* layout that also paints. Contains the only `TextOut` in the core |
| `pagevw.c` | messages + GDI drawing + input | Page view: pagination (core) and presentation of that view |
| `disp3.c` | messages + GDI drawing | Display computation and painting in the same file |
| `scroll.c` | messages + GDI drawing | Scrolling: what is visible (core) versus how it is repainted |
| `disp2.c`, `disptbl.c`, `printsub.c` | GDI drawing | Document structure traversal with drawing emission interleaved |
| `block.c` | GDI drawing + input | Block operations with visual feedback |
| `insert.c`, `select.c`, `selecttb.c` | input/cursor | Editing and selection that query cursor state |
| `editspec.c`, `undo.c` | messages | Change notification via messages; replaced by callbacks |
| `error.c` | messages | Real `MessageBox` on line 1618, plus `Yield` x2 |

Recommended strategy: split by function, not by file. Each one is split
into a `*_core.c` (no Win32) and a `*_shell.cpp` (Qt), keeping the
original name as a prefix so history stays legible. They are not touched
until Qt-2, with the `CONTRIBUTING.md` restriction on `src/Opus/` in
force.

---

## B2: Text measurement contract (top priority)

### B2.1 Why the restriction concentrates at a single point

The project's stated risk assumed that text measurement was entangled
with document logic and was not trivially abstractable. The evidence says
otherwise: **the layout engine already works against a metrics cache, not
against GDI.**

`struct FTI` (`Opus/wordtech/format.h:379-410`) is that cache:

```c
struct FTI {
    int  dxu;              /* fixed width if !fPS && fHeap */
    int  chFirst;
    int  cch;              /* chLast + 1 - chFirst */
    struct FONTREC far * far *qqftr;
    long bmpchdxu;         /* offset of the width table in fontrec */
    uns  dxuFrac;          /* 16-bit fraction kept between characters */
    uns  wNumer, wDenom;   /* scaling */
    int  dypAscent, dypDescent, ftc, catr, ps;
};
```

The core does integer arithmetic over those fields, with an explicit
fraction accumulator (`dxuFrac`) and its own multiply-and-divide function:
`NMultDiv`, used in `Opus/wordtech/layout.h:467-469` and
`Opus/wordwin.h:252-255`. `NMultDiv` is the project's own, not GDI's
`MulDiv`, that only appears in `port/original/opus_sdm_runtime.cpp` and
`opus_win95_chrome.cpp`, which are port layer.

The one that calls GDI to **fill** the cache is the display level:
`Opus/dispspec.c:787` and `:796`, in the form
`GetTextExtent(hdc, &ch, 1) - tm.tmOverhang` character by character, plus
`GetTextMetrics`.

If the shell fills the width table with the same integers GDI produced,
all of the core's subsequent arithmetic is integer and reproduces the
byte-identical pagination by construction. The restriction is not
distributed across 170 sites: it concentrates at a single fill point.

### B2.2 Contract

Declared in `src/core/include/OpusShellFontMetrics.h`. Interface in C,
implemented by the shell, consumed by the core:

```c
/* Font identity as the core knows it: the same fields FTI already keeps.
   No Qt or Win32 type is exposed. */
typedef struct OpusFontKey {
    int ftc;    /* typeface code */
    int ps;     /* size in half-points, as in FTI */
    int catr;   /* attributes: bold, italic, ... */
} OpusFontKey;

typedef struct OpusFontMetrics {
    int dypAscent, dypDescent;
    int dxpOverhang;      /* equivalent of TEXTMETRIC.tmOverhang */
    int dxpInch, dypInch; /* target device resolution */
    int dxuFixed;         /* != 0 if the font is fixed-pitch */
} OpusFontMetrics;

/* Returns 0 on success. The shell does not know FTI; the core translates. */
int OpusShellFontMetrics(const OpusFontKey *key, OpusFontMetrics *out);

/* Fills rgdxu[0..cch-1] with the integer advance of each character in the
   range, in the same units as the table pointed to by FTI.bmpchdxu. */
int OpusShellCharWidths(const OpusFontKey *key, int chFirst, int cch,
                        unsigned short *rgdxu);
```

Two functions. The core does not acquire an `HDC`, does not select fonts,
does not ask for string extents: it requests the table once per font and
does its own arithmetic, exactly as it does today. **This contract is
validated by the §B2.3 experiment and does not change.** What changed is
the implementation strategy behind it.

### B2.3 How the rounding is reproduced: resolved empirically

An earlier version of this document proposed an arithmetic wrapper
starting from design units and applying Microsoft's conversion formula
(`DeviceUnits = DesignUnits/unitsPerEm * PointSize/72 * DeviceResolution`),
while also leaving open whether strict fidelity was reachable with a
different rasterizer. Those were two contradictory answers to the same
question. It was resolved by measuring.

**Setup.** Oracle side: a Winelib program that reproduces the exact shape
of `dispspec.c`: `CreateDCA("DISPLAY")`, `CreateFontIndirectA` with
`lfHeight = -MulDiv(pt, dpiY, 72)`, `GetTextMetricsA`, and
`GetTextExtentPoint32A` with `cch = 1` minus `tmOverhang`, for the 95
printable ASCII characters. Qt side: `QRawFont` over the same physical
font file, to isolate rounding from font substitution. Fonts: Liberation
Serif, Sans, and Mono, at 8, 10, 12, 14, 18, 24, and 36 points, 96 dpi.

**Result.** Arithmetic over design units **does not reproduce GDI**:

| Strategy | Match rate |
|---|---|
| Design units x `dpi*pt/72` without rounding (the proposed formula) | from 0/95 to 95/95 depending on font and size; inconsistent |
| Design units x em size rounded to an integer | 95/95 in 14 of 15 cases; fails on Liberation Serif 14 pt |
| **Integer advances requested from the rasterizer at the same integer ppem, with `QFont::PreferFullHinting`** | **95/95 in 21 of 21 cases (1995 comparisons, all exact)** |

The isolated failure of the second strategy explains why the first could
not work. In Liberation Serif at 14 pt (19 px), the character `@` has
1886 design units out of 2048 per em: 1886/2048 x 19 = 17.497, which rounds
to 17. GDI returns 18. Measuring the same glyph by hinting mode:

```
NoHinting         @=17.484 -> 17
VerticalHinting   @=17.484 -> 17
FullHinting       @=18.000 -> 18      <- matches GDI
Default           @=17.484 -> 17
```

The difference is grid-fitting, not rounding: the font's hint program
snaps the advance to the pixel grid. No wrapper arithmetic can close a
half-pixel offset that comes from the font's own hinting instructions.

**Why exact match is in fact achievable, and why that confirms the scope
rather than limiting it.** Because the project's oracle is **Wine's** GDI,
and Wine rasterizes with FreeType, just like Qt. Requesting the same
hinting mode at the same integer ppem produces the same integers because
the same engine runs underneath, on both sides.

It is worth being explicit about how to read this. The restriction fixed
when the branch was opened was "byte-identical pagination **relative to
the Winelib oracle**", not relative to real Windows. That the equivalence
rests on both sides rasterizing with FreeType is not a caveat on the
result: it is the definition of the goal, as decided before this
experiment. The experiment confirms that goal is achievable and with what
concrete strategy.

From this follows, and stands as scope rather than as a pending item: the
equivalence holds against the Winelib binary. Nothing is claimed about
Microsoft's GDI on real Windows, whose rasterizer is different, because
reproducing that is not, and was not, a goal of this branch. If that were
ever wanted, it would be a scope change with its own decision, not a
defect of this design.

**Strategy in force.** The shell obtains the advances like this:

```
px = MulDiv(ps/2, dypInch, 72)                    /* same integer rounding as GDI */
QRawFont rf(fontData, (qreal)px, QFont::PreferFullHinting);
rf.advancesForGlyphIndexes(...)                   /* already grid-fitted */
```

Two requirements, both necessary: round the pixel size to an integer
**before** constructing the font, and request `PreferFullHinting`.
Omitting either reintroduces the discrepancy.

The table captured from the oracle stops being the mechanism and remains
only as a regression test: it is captured once for the supported font set
and compared on every shell change.

### B2.4 Which Qt API sits behind it

**`QRawFont`**, built with an integer pixel size and
`QFont::PreferFullHinting`. It gives access to a physical font instance
and returns advances already grid-fitted, which is exactly what GDI
delivers.

**Ruled out: `QFontMetricsF`.** Returns `qreal` with Qt's own conversion
and rounding applied; it does not allow fixing integer ppem nor hinting
mode with the precision the equivalence requires.

**Ruled out for the document path: `QTextLayout`.** It does its own
shaping, line breaking, and cursor positioning per Unicode. Word 1.1a has
its own line-breaking algorithm inside `wordtech/`, and that is precisely
the logic the Qt branch wants to preserve. Using it would put two layout
engines in competition and break fidelity by design. Qt is reduced to a
glyph rasterizer and advance provider.

### B2.5 Residual risk, measured

- **`tmOverhang`: ruled out as a risk.** Measured at 14 pt for Liberation
  Serif in normal, bold, italic, and bold italic, and for the era names
  `Helv`, `Tms Rmn`, `Script`, and `Modern`, including synthesized bold
  and italic. `tmOverhang = 0` in all eight cases: with TrueType fonts
  under Wine the concept does not come into play. It was the most likely
  candidate for discrepancy and it is not one.
- **Font substitution: the real risk, and it is concrete.** The oracle
  substitutes the era names, and not intuitively. Measured: `Helv`,
  `Tms Rmn`, `Script`, and `Modern` all resolve to Liberation Sans,
  including `Tms Rmn`, which is a serif name. Since the advances depend on
  the physical file, the shell must reproduce **the same substitution
  table Wine applies**, not simply have fonts available. If the shell
  resolves `Tms Rmn` to a serif and the oracle to Liberation Sans, all the
  advances differ and fidelity is lost entirely, with the arithmetic
  having nothing to do with it.
- **Pixel fidelity versus pagination fidelity.** The integer advances
  match, which is enough for identical line and page breaks. Pixel-for-
  pixel equality of the painted shape is not verified and is not what the
  restriction requires.
- **Conditional compilation.** The inventory does not evaluate `#if`, so
  some counted site may be disabled in the real build configuration. It
  affects sizing, not the contract.

### B2.6 Font substitution table

Measured with the probe `docs/port-qt/scripts/fidelity/font_substitution.c`
(same environment as §B2.3: Wine 10.0, Debian trixie) for the 4 era names
that `Opus/initwin.c` loads into the startup master table (`vhsttbFont`,
`ftc` 0-3):

```
Tms Rmn    -> Liberation Serif         charset=0 overhang=0
Symbol     -> Liberation Sans          charset=0 overhang=0
Helv       -> Liberation Sans          charset=0 overhang=0
Courier    -> Liberation Mono          charset=0 overhang=0
```

**Correction to an earlier measurement in this section:** the first
version of the probe built the `LOGFONTA` with `ZeroMemory` and never set
`lfPitchAndFamily`, leaving it at 0 (`DEFAULT_PITCH | FF_DONTCARE`) for
all four names, which made Wine ignore the typeface family when resolving
and made all four collapse to `Liberation Sans`. The real engine always
sets that field: `Opus/LOADFONT.C:864`,
`plf->lfPitchAndFamily = (pffn->ffid & maskFfFfid) | fcid.prq;`, with
`ffid`/`prq` taken from the startup master table
(`Opus/initwin.c:1543-1583`):

| Era name | `ffid` | `prq` |
|---|---|---|
| `Tms Rmn` | `FF_ROMAN` | `VARIABLE_PITCH` |
| `Symbol` | `FF_DECORATIVE` | `DEFAULT_PITCH` (0) |
| `Helv` | `FF_SWISS` | `VARIABLE_PITCH` |
| `Courier` | `FF_MODERN` | `FIXED_PITCH` |

With the probe fixed to set `lfPitchAndFamily` to `ffid | prq` per name
(same as the engine does), **not all four resolve to the same family**:
`Tms Rmn` resolves to Liberation Serif and `Courier` to Liberation Mono;
only `Symbol` and `Helv` agree on Liberation Sans, because both request a
`sans-serif` family (`FF_DECORATIVE` and `FF_SWISS` respectively) under
Wine/fontconfig in this environment.

On `Symbol` in particular: the notable point is not the returned charset,
asking for `ANSI_CHARSET` and getting back `tmCharSet=0` (ANSI) is
exactly what is expected, not a contradiction, but that the *name*
`Symbol` would suggest a symbol font while the engine instead deliberately
requests `ANSI_CHARSET` for that entry: `Opus/initwin.c:1559` sets
`ChsPffn(pffn) = ANSI_CHARSET;` with the original Microsoft comment
("weirdly, this is correct for Postscript; other printers will have to
tell us what they do; enumeration will override these settings if
necessary"), and `Opus/LOADFONT.C:880` (`plf->lfCharSet = ChsPffn(pffn);`)
carries that charset unmodified into the `LOGFONTA`. The probe, by
requesting `ANSI_CHARSET` for `Symbol`, faithfully reproduces the engine's
behavior; it is not an artifact of the probe. There is no special
handling to preserve on the shell side beyond that: `Symbol` is
substituted by an ordinary sans-serif font, no symbol font or separate
glyph table is needed.

Family-to-physical-file resolution, cross-verified (the `fc-match` answer
is not accepted without confirming it against the file's own `name`
table), for the 3 distinct families involved:

```
$ fc-match -f '%{file}\n' "Liberation Serif"
/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf
$ fc-scan --format '%{family}\n' /usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf
Liberation Serif

$ fc-match -f '%{file}\n' "Liberation Sans"
/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf
$ fc-scan --format '%{family}\n' /usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf
Liberation Sans

$ fc-match -f '%{file}\n' "Liberation Mono"
/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf
$ fc-scan --format '%{family}\n' /usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf
Liberation Mono
```

Matches in all three cases: the family reported by each file's own `name`
table is the same one `fc-match` resolved. Final table:

| Era name | Resolved family | Physical file |
|---|---|---|
| `Tms Rmn` | Liberation Serif | `/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf` |
| `Symbol` | Liberation Sans | `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf` |
| `Helv` | Liberation Sans | `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf` |
| `Courier` | Liberation Mono | `/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf` |

**Note looking forward (B2, ppem selection):** `Opus/LOADFONT.C:829-837`
has a documented "HACK" that, specifically for `Courier`, keeps
`lfHeight` positive instead of negative as with the rest of the names: it
selects the font by cell height, not by character height, because
Windows' Courier font had annoying internal pixels in its "leading" area.
Verified that this does not change which family `Courier` resolves to
here (it is still Liberation Mono), but any later ppem-selection work
(B2) will have to reproduce that different sign for Courier, not just the
family mapping.

Implemented as a static table in
`src/core/include/OpusShellFontSubstitution.h` /
`src/core/src/OpusShellFontSubstitution.cpp`, see that header for the
contract. It only covers these 4 names; `Script` and `Modern` are out of
scope for this measurement (they are not in the startup master table, see
`01-core-shell-boundary.md`, "Recommended sequence for Qt-2", step 2).

### B2.7 Real connection to the first caller: `C_LoadFcid`

`Opus/LOADFONT.C:187 C_LoadFcid`, screen path (`!pfti->fPrinter`), no
preview (`!vfPrvwDisp`), variable pitch
(`tm.tmPitchAndFamily & maskFVarPitchTM`), calls `OpusShellCharWidths`
instead of `GetCharWidth`/`OurGetCharWidth` to fill
`FCE.hqrgdxp`/`FTI.rgdxp`, guarded under
`#if defined(__GNUC__) && !defined(_MSC_VER)`, MSVC keeps using GDI
unchanged. Field mapping, verified against the real site:

- `OpusFontKey.ftc` from `fcid.ibstFont`. The startup master table
  (`Opus/initwin.c:1541-1583`, already cited in §B2.6) registers Tms Rmn,
  Symbol, Helv, Courier in that order as `ibstFont` 0-3, the same order
  `EraNameFromFtc` in `OpusShellFontMetrics.cpp` already assumed. No new
  translation table was needed: `ibstFont` **is** the `ftc` the contract
  expects, for these 4 entries.
- `OpusFontKey.ps` from `fcid.hps` (half-points, the same field
  `C_FGraphicsFcidToPlf`, `Opus/LOADFONT.C:832`, already uses to build
  `lfHeight`).
- `OpusFontKey.catr` from `(fcid.fBold ? 1 : 0) | (fcid.fItalic ? 2 : 0)`.
  It only matters whether it is `0` or not: the contract does not know
  how to measure bold or italic (limitation 2 of
  `OpusShellFontMetrics.cpp`), so any active attribute must fail cleanly,
  not approximate.

Clean failure: if `OpusShellCharWidths` returns an error (unsupported
font, `catr != 0`, or any other case outside the contract), the new path
jumps to `LSystemFontErr`, the same destination already used by a
`CreateFontIndirect` failure earlier in the same function. `fFallback`
stays `fTrue`, the font falls back to stock/system, and when re-entering
the variable-width block, the already existing `!fFallback` guard skips
it entirely: it degrades to fixed width (`tm.tmAveCharWidth`) just like
any other font failure in this code, with no silent GDI approximation. No
new recovery path was implemented; the one that already existed was
reused.

Overhang: `dxpOverhang = 0` is not forced at the integration site;
`pfce->dxpOverhang` still comes from `tm.tmOverhang` (real GDI, the same
`GetTextMetrics` call that already ran before this change, untouched).
The overhang subtraction of the GDI path (`LOADFONT.C:482-490` in the
current numbering) stays intact but outside the new path, which does not
need it: §B2.5 measured `tmOverhang = 0` in the 8 style cases under
TrueType/Wine, so in practice both paths agree on the value (0), not
through a correction applied twice.

**What is verified end to end and what is not.** The 5 `src/core` tests
(including `opus_shell_font_metrics_fidelity_test`, 2660/2660) remain
green after this change, but they do not exercise `Opus/LOADFONT.C`, only
the native library that file now calls. The Winelib-side connection
(`Opus/` compiled with `wineg++`/`winegcc` against the contract) could not
be compiled in this session: `Opus/wordtech/disp.h:248` (`struct DR
rgdr[]` inside a `union`) is rejected by GCC 14 with "flexible array
member in union", a preexisting error, confirmed with `git stash` against
the same commit before this change, in a file this work does not touch.
It blocks compilation of `loadfont.c.o` (and of `opus_x64_layout.c`, and
therefore of `WORD1` entirely) in this environment, independent of §B2.7.
It is an environment/toolchain blocker, not a contract or integration one,
but it means the real link `WORD1` -> `opus_shell_font_metrics` (wired in
`src/CMakeLists.txt` in this same session, see the wiring commit) was not
tested by compiling end to end, only that `find_package(Qt6 ... Gui)` and
the `IMPORTED` declarations resolve (`cmake --preset linux-winelib-debug`
configures clean) was verified. Aside from that, `WORD1` already starts
with a known heap corruption before reaching a usable state
(`word1_startup_blocked`, see `CLAUDE.md`), so even if the `disp.h`
blocker did not exist, this change alone could not have been verified
"against real layout" by running the binary.

**Conclusion on the unblocking criterion for `scroll.c`/`disp3.c`/
`pagevw.c`:** the code is connected (§B2.7 closes the missing call path),
but the document's criterion, "B2 implemented and verified against real
layout", calls for runtime verification, not just type-level compilation.
That verification did not happen this session due to two blockers
unrelated to this work (`disp.h`/GCC 14 compilation, `WORD1` startup).
`scroll.c`/`disp3.c`/`pagevw.c` **remain out of scope** until one of
those two blockers is resolved and B2 can be observed producing real
pagination.

---

## B3: Win16 memory contract

201 sites, 21 TUs. Of those, 6 are boundary sites in the core (`catalog.c`,
`elfile.c`, `elsubs2.c`, `etcmd.c`, `filecvt.c`, `spelcore.c`), 1 is
diagnostic, and 14 are presentation.

### B3.1 What is already resolved, and is not repeated here

`src/port/original/opus_x64_compat.h` already resolved pointer packing for
the Winelib port, and this design rests on that without reimplementing it:

- `LOWORDX` / `HIWORDX` / `MAKELONGX` (lines 305-312) operate on
  `uintptr_t`, not on `WORD`, so a 64-bit pointer is not truncated when
  packed.
- The point-unpacking macros (lines 319-333) extract coordinates with sign
  extension from a value packed at pointer width.
- The `dkt`/`dktString` handling for typed parameters is annotated at line
  34.

Qt-2 uses those macros as they stand. What follows is what is **not**
resolved.

### B3.2 Background correction: `HANDLE` is not 16 bits wide in this build

An earlier version of this section claimed, citing
`Opus/lib/qwindows.h:630` (`typedef WORD HANDLE`), that "a Win16 handle is
a 16-bit integer" and that this is where the contract's problem came from.
Measured before designing anything further: **false for this build.**

```c
#include "word.h"
printf("sizeof(HANDLE)=%zu\n", sizeof(HANDLE));   /* -> 8 */
```

`qwindows.h` is the vendored Win16 SDK, but that branch of `word.h` sits
behind `#ifdef OPUS_X64 ... #else ... #include "qwindows.h" ... #endif`,
and `OPUS_X64` is defined in this build. The branch that actually
compiles includes `opus_x64_compat.h`, which pulls in Wine's real
`windows.h` (`winnt.h: typedef void *HANDLE`). `qwindows.h:630` is dead
code for this target; the original citation was never checked against
what actually compiles.

This does not make the contract trivial, it changes why it exists. An
opaque allocator is not needed because a handle does not fit in a 16-bit
field, it already fits, it is the same size as a pointer. It is needed
because a handle is still runtime indirection that the core should not
fix by design (shell independence) nor ever serialize (§B3.3).

### B3.3 Phase 1: persisted structures with a handle field: none found

Every field of type `HANDLE`/`GLOBALHANDLE`/`HGLOBAL`/`LOCALHANDLE` in
`Opus/` was swept, before a single line of the header was written. Of
those that are struct fields (not local variables or parameters, most of
the ~90 `HANDLE` matches in the tree are those), these are the ones that
turned up and why none is serialized:

| Struct | Field(s) | What it is | Why it is not serialized |
|---|---|---|---|
| `Opus/core.h` | `rghcdModules[]` | code-module handle cache (`GetCodeHandle`, `wproc.c`) | lives and dies with the session; no file write touches it |
| `Opus/dde.h` (`DDLI`) | `hData` | last DDE message | live inter-process IPC, there is no "saved DDE" |
| `Opus/dde.h` (`DRVDATA`) | `hrgbKeyState` | key-state log for macro playback | session state of `eldde.c`/`quit.c`, plain `GlobalAlloc`/`Free` memory |
| `Opus/filecvt.h` (`EXCR`) | `hLib`, `ghszFn`, `ghszSubset`, `ghBuff`, `ghszVersion` | loading an external converter DLL | scratch of `filecvt.c` for the duration of the conversion, plain `GlobalAlloc`/`Free` |
| `Opus/el.h` (`CABX`) | `rgh[]` | generic "private SDM" container | the code's own comment: "necessary for CAB access... internal"; no file write |
| `Opus/el.h` (`DKD`) | `hLib` | loaded add-in library | not referenced by name in any `.c`; orphaned |
| `Opus/grstruct.h` (`PICT`) | `hbm` | GDI bitmap for repainting | its only real use is `SelectObject` (`rsb.c:601`); the on-disk image format (`PIC.H`) has no handle fields |
| `Opus/wordtech/file.h` (`ELOF`) | `hFile` | OS handle of a file opened by the macro language | by definition does not survive a restart; never writes itself out |

Cross-checked against the structures that actually are the real file
format, FIB, FIB30, DOP, STSH, PAP, CHP, SEP, BKF, FFN, STTB, **none has a
handle field.** This matches the practice already expected of the era:
the Word 1.1a format saves indices (FTC, STC, FKP offsets) precisely
because a handle does not survive a save, it is not a precaution this
port introduced.

**Phase 1 verdict: no persisted structure has a handle field.** It does
not change the shape of the contract, it remains the opaque handle of
§B3.2, and it does not block Phase 2.

**Side finding, out of scope for this document but not something that can
go unmentioned:** while verifying `struct FTI` for this phase it was
found that the central citation of §B2.1 (`Opus/wordtech/format.h:379-410`,
with `dxuFrac`/`bmpchdxu`/`struct FONTREC far * far *qqftr`) describes a
structure that is **inside `#ifdef MAC`**, dead in this build, just like
`qwindows.h` was. The `struct FTI` that actually compiles under
`WIN`/`OPUS_X64` lives in `Opus/fontwin.h:126-152`: no fraction
accumulator, with `int rgdxp[256]` (an inline width table, not a pointer
to an external table) and `HFONT hfont`. The text measurement contract
(B2) is not touched in this document (out of scope for this prompt), but
it is noted here: **B2.1 describes the wrong structure** and needs its
own review before implementation.

### B3.4 Contract

Declared in `src/core/include/OpusShellMemory.h`, with `OpusMemHandle`
(the equivalent of `GlobalHandle`) added in Phase 2 of this contract:

```c
/* Fully opaque handle. Never packed into 16 bits, never written to disk,
   never compared against a literal. */
typedef struct OpusHandleImpl *OpusHandle;

#define OPUS_MEM_ZEROINIT 0x0001u

OpusHandle    OpusMemAlloc(unsigned long cb, unsigned flags);
void         *OpusMemLock(OpusHandle h);     /* pins and returns pointer */
void          OpusMemUnlock(OpusHandle h);   /* releases the pin */
OpusHandle    OpusMemRealloc(OpusHandle h, unsigned long cb, unsigned flags);
unsigned long OpusMemSize(OpusHandle h);
OpusHandle    OpusMemHandle(void *ptr);      /* pointer -> owning handle */
void          OpusMemFree(OpusHandle h);
```

A single contract covers `Global*` and `Local*`: under this port both
Win16 heaps are already the same native heap, so the distinction has
nothing to preserve. `LocalHandle` does not appear (0 matches by an
independent grep across all of `Opus/`+`OpusEtAl/`); the rest of the
family does (`GlobalHandle`: 11, `LocalAlloc`: 5, `LocalReAlloc`: 3,
`LocalLock`/`LocalUnlock`/`LocalSize`: 1 each) despite not being in the
Qt-0 inventory's symbol table, `debugwin.h` does not intercept those
specific names, so the dictionary-derivation mechanism did not capture
them; confirmed by direct grep, not assumed absent.

Design notes, in order of risk:

1. **Movable memory and the pinning discipline.** The Win16 model allowed
   the manager to move an unlocked block; `GlobalLock`/`GlobalUnlock`
   form pairs the code respects today. Qt has no equivalent. The core
   allocator can pin everything permanently, a 64-bit process's memory
   allows that, but **the pairs must be kept in the code**: eliminating
   them as no-ops would make it impossible to detect a use-after-move
   pointer, should compaction ever be introduced later. Verified in the
   implementation (§B3.5): `OpusMemLock` after `OpusMemFree` returns
   `NULL` instead of a dangling pointer.
2. **`GlobalLockClip`.** A locking variant specific to the clipboard. It
   goes to the Qt-6 clipboard contract, not this one. Noted here so it is
   not resolved twice.
3. **Every serialized struct field with handle type needs indirection.**
   Resolved by Phase 1 (§B3.3): there is none today. If one is ever
   added, that is the moment to design the indirection, not before.

### B3.5 Implementation and verification: closed

Core side implemented in `src/core/src/OpusShellMemory.cpp`: an opaque
handle over `malloc`/`realloc`/`free` with a pin counter and a "freed"
marker kept after `OpusMemFree`, deliberately, to be able to detect
misuse instead of reading already-recycled memory. `OpusMemHandle` uses a
`pointer -> handle` registry (`std::unordered_map`), not pointer
arithmetic, because the address `malloc` returns has no fixed relation to
the `OpusHandleImpl` that owns it. It does not depend on Qt: it is the
first implementation of the three remaining contracts, and it did not
need Qt for anything.

**Compiling and linking is not enough**, `link-check/` already proved
that for passing values by copy (commit `7760a28`). What this contract
crosses is a real handle/pointer, so the test has to demonstrate that it
survives the boundary, not just that it can be passed.

Probe in `docs/port-qt/scripts/handle-check/` (`handle_check.c`), compiled
and linked with **wineg++**, the real driver of `WORD1`, not `winegcc`,
against `libopus_shell_memory.a`:

1. `OpusMemAlloc` -> real handle.
2. `OpusMemLock` -> valid pointer.
3. Writes the pattern `"OpusMemHandleRoundTrip-2026"` through the pointer.
4. `OpusMemUnlock`, `OpusMemLock` again: **the pattern is still there**,
   not just "the pointer is non-null".
5. `OpusMemHandle(pointer)` returns the same original handle.
6. `OpusMemFree`, then `OpusMemLock` on the freed handle: `NULL`, clean
   failure, not a dangling pointer.
7. A separate `--double-free` mode: `OpusMemAlloc` -> `OpusMemFree` ->
   `OpusMemFree` again. The second call does `abort()`. Under Wine this
   shows up as WineDbg's dump catching the `SIGABRT`, noisy, but it is
   exactly Wine reacting to a real abnormal termination, not a probe
   error; the process exits with 134 (128+SIGABRT), never with 0.

The seven checks passed in the real run (`run.sh`, over the library that
`opus_core_build` actually builds, not a scratch copy); the double-free
process exited with 134, not 0. No regression: `ctest -R
opus_shell_config_test` stays green after adding `opus_shell_memory` to
the same `CMakeLists.txt`.

**Consequence:** the physical boundary has now proven passing values by
copy (configuration) and passing ownership of a real memory block
(handles). The two crossing modes the remaining contracts need, message
spine (callbacks, no state crossing the boundary beyond the return value)
and text measurement (width arrays passed by value, same as
configuration), do not introduce a third new category.

---

## B4: Message spine and windows contract

287 sites, 47 TUs: `Opus/` root 38, `Opus/wordtech/` 6, `Opus/debug/` 3.
The two fragments with a concrete signature today (§B4.3) are declared in
`src/core/include/OpusShellSpine.h`; the rest of this section remains a
conceptual table, not API.

### B4.1 Control inversion is the major structural change

Today Word's code **owns** the message loop: it calls `GetMessage` and
`DispatchMessage` from `Opus/` root. In Qt, the loop belongs to
`QCoreApplication` and calls inward.

That inversion, not symbol mapping, is the real work of this boundary.
The core goes from driver to library: it exposes entry points the shell
invokes, and notifies outward via callbacks instead of posting messages.

### B4.2 Conceptual mapping

| Win16 | Qt | Note |
|---|---|---|
| `GetMessage`, `PeekMessage`, `DispatchMessage`, `TranslateMessage` | `QCoreApplication` loop | Disappear from the core; the shell owns the loop |
| `RegisterClass` | `QWidget` subclass | No direct equivalent, it dissolves |
| `CreateWindow`, `DestroyWindow` | `QWidget` construction and destruction | |
| `SendMessage` | direct call through the boundary API | Synchronous semantics, preserved |
| `PostMessage` | `QMetaObject::invokeMethod` with `Qt::QueuedConnection` | Deferred semantics, preserved |
| `DefWindowProc`, `CallWindowProc` | `QWidget` default handling | |
| `ShowWindow`, `UpdateWindow`, `MoveWindow`, `SetWindowPos`, `EnableWindow` | `QWidget` methods | Direct mapping |
| `MessageBox` | `QMessageBox` | Only from the shell; the core never opens UI |
| `SetTimer` | `QTimer` | |
| `MakeProcInstance`, `FARPROC` | function pointers | Already neutralized by Winelib |
| `Yield` | removed | Win16 cooperative multitasking artifact |

### B4.3 The 6 `wordtech/` TUs with message spine

They require extraction, not mapping. In increasing order of difficulty:

- **`error.c`**: the simplest, and the first to do. Real `MessageBox` on
  line 1618, plus two `Yield`. Replaced by an error callback in the
  boundary API: the core hands over a code and context, the shell decides
  presentation.
- **`editspec.c`, `undo.c`**: **correction to an earlier reading of this
  document.** They do not notify document changes via messages: the
  symbol that puts them in this category is `MessageBeep(MB_OK)`
  (`editspec.c:1855,2075`, `undo.c:97`), verified independently by grep,
  not a notification mechanism. In all three sites it is the same
  pattern, empty undo stack (`undo.c:97`, `vuab.uac == uacNil`) or an
  invalid block range (`editspec.c`, `LRetFalse`), an audible "operation
  rejected" signal, with `Beep()` under the `!OPUS_X64` branch already
  present in the file itself. Replaced by a trivial alert callback, no
  text, of the same kind as `error.c`'s but with no message to resolve.
  The real document-change-notification mechanism, if it exists as a
  distinct one, is not located yet; it is not claimed to exist just
  because Qt-1 anticipated it in the abstract.
- **`scroll.c`, `disp3.c`, `pagevw.c`**: they mix computing what is
  visible (core) with repainting (shell). They require the split-by-
  function described in §B1.3 and should not be attempted before the text
  measurement contract is implemented and verified.

### B4.4 Real connection of the first site: `error.c:1618`

`Opus/wordtech/error.c`, function `ErrorEidStartup`, replaces the real
`MessageBox` with `OpusShellReportError(eid, szMsg)`, guarded under
`#if defined(__GNUC__) && !defined(_MSC_VER)`, same pattern as §B2.7.
MSVC keeps the real `MessageBox`, unchanged.

Verified before the change, `git grep -n 'MessageBox' src/Opus/wordtech/
error.c`: line 1618 is the only direct `MessageBox` call in the file, the
other three matches (1214, 1549, 1674) are `IdMessageBoxMstRgwMb`, Word's
own wrapper, not the Win32 API, and the one at 1582 is a comment.

Parameter mapping against the signature
(`src/core/include/OpusShellSpine.h:46`,
`void OpusShellReportError(int eid, const char *message);`):

- `eid`: the same parameter `ErrorEidStartup(eid)` already receives, no
  transformation.
- `message` from `szMsg`, the text the function itself already resolves
  via `IemdFromEid`/`CopyEmdSt` before the call, the error content loses
  nothing.
- `szApp` (the window title) **has no place in the contract on purpose**:
  `OpusShellSpine.cpp:20` sets the title to `"Word"` translated by Qt, not
  to the value of `szApp`. `szApp` is a global app constant (`extern CHAR
  szApp[]`, the same value at any call site, not data specific to this
  error), so there is no loss of information *about the error*: it is a
  presentation decision already explicit in the header
  (`OpusShellSpine.h:17`, "the shell decides presentation"), not an
  improvised trim in this change.
- `MB_OK|MB_SYSTEMMODAL`: already fixed inside `OpusShellReportError`
  (`QMessageBox::Ok`, `Qt::ApplicationModal`), the real call in `error.c`
  never varies those flags between invocations (it is the only call), so
  there is no flag combination the contract needs to parameterize.

Clean failure: no behavior changes in the error case, the path is still
the same function, the same two `Yield()` calls around it (a documented
Windows bug in the original comment), with no `try`/`catch` or silent
fallback added.

**Real difference from §B2.7: this time it did compile against Winelib
end to end.** `error.c` does not include `disp.h` or `rsb.h`, either
directly or transitively (verified by `git grep`, two levels, see the
previous reconnaissance session), it does not run into the GCC 14 blocker
in `Opus/wordtech/disp.h:248`. `ninja
CMakeFiles/opus_original_engine.dir/Opus/wordtech/error.c.o` under real
`wineg++`/`winegcc` compiled without errors (only preexisting warnings,
none on the touched lines) and produced the object (`error.c.o`, 41784
bytes). This is real compilation against the Winelib tree, not just the
5 native `src/core` tests, but **it is not execution**: `WORD1` as a
whole still cannot link/start in this environment (the rest of the tree
does hit `disp.h`/GCC 14, and separately `WORD1` has the known startup
blocker, `word1_startup_blocked`), so the real modal dialog from
`OpusShellReportError` at this specific call site was not triggered or
observed running this session. What is verified at runtime is the
contract itself (`opus_shell_spine_test`, a real modal dialog via
`QMessageBox`/`QTimer`, commit 79b181f), not the full path starting from
the real `ErrorEidStartup`.

**This does NOT unblock `disp.h`/GCC 14.** It remains a separate
environment/toolchain blocker, untouched this session, that still affects
`editspec.c`/`undo.c` (the other two sites of §B4.3, both include
`disp.h`) and most of the tree. That `error.c` compiled clean is a
property of that particular file (it does not include `disp.h`/`rsb.h`),
not a resolution of the underlying problem.

---

## B5: Configuration persistence contract

42 sites, 12 TUs (figure corrected in §B5.2 while migrating; the
inventory's *Configuration persistence* category sums to 43 because it
also includes `GetEnvironmentVariableA`, out of scope for this contract).
Symbols: `GetProfileString`, `GetProfileInt`, `WriteProfileString`.

The simplest of the four, and it is kept simple. Declared in
`src/core/include/OpusShellConfig.h`. The `WIN.INI` semantics, section,
key, default value, map one to one to `QSettings`:

```c
int OpusShellProfileString(const char *section, const char *key,
                           const char *deflt, char *out, int cbOut);
int OpusShellProfileInt(const char *section, const char *key, int deflt);
int OpusShellProfileWrite(const char *section, const char *key,
                          const char *value);
```

Three functions, translated directly to `QSettings::value` and `setValue`
with `beginGroup(section)`. No own cache, no schema layer, no migration:
`QSettings` already resolves format and location per platform. Confirmed
as the complete contract; there is nothing else to design here.

### B5.1 Implementation (Qt-2, step 1): closed

Shell side implemented in `src/core/src/OpusShellConfig.cpp`, native
sub-project `src/core/` (always real gcc/g++ and Qt6, never
winegcc/wineg++; built under `OPUS_WINELIB_BUILD` via
`ExternalProject_Add`, same scheme as the host tools in
`src/port/tools/host/`). When this step closed, nothing in `Opus/` was
touched yet: the call sites still used
`GetProfileString`/`GetProfileInt`/`WriteProfileString`. Migrating those
sites is the work of §B5.2, closed separately.

**Test:** `src/core/src/OpusShellConfig_test.cpp`, registered as
`opus_shell_config_test` in ctest. Verified with `ctest -R
opus_shell_config_test` on the real `linux-winelib-debug` preset (not
only in an isolated build): 9 checks, all green. `QSettings::setPath`
redirects to a temporary directory before the first call, so the test
does not touch the real configuration of whoever runs it, confirmed by
inspecting `~/.config` after running it.

**What the test covers, and why that way:** before writing it, a search
was made in the existing test tree (`src/port/original/opus_*_test.c*`)
for any site already exercising these three functions, as the task asked.
There is none, zero matches for
`GetProfileString`/`GetProfileInt`/`WriteProfileString` in the current
test files. There is, then, no prior specific behavior to match; the test
verifies the implementation against the documented Win16 `Profile`
semantics that `Opus/*.c` still calls today:

- String round trip, with the truth about the default value when the key
  does not exist, and correct truncation when the output buffer is
  smaller than the value (same contract as `cbMax` in
  `GetProfileString`).
- Integer round trip, including negatives.
- The distinction that really mattered to verify: `GetProfileInt` uses
  the default value **only when the key does not exist**; if it exists
  but is not numeric, the real value is `0`, not the default. `QString::
  toInt()` requires the whole string to be a valid number and does not
  reproduce that, so the implementation uses its own `atoi`-style
  conversion (`AtoiLike`, `OpusShellConfig.cpp`). The test pins this case
  explicitly so a future simplification does not silently lose it.

**Found while reviewing the real call sites, not invented:**
`Opus/print2.c:833` calls `GetProfileString(..., key = NULL, ...)` to
enumerate all keys of the `"devices"` section at once, a usage form the
three-function contract of §B5 does not cover. It was not touched in the
migration step because it requires extending the contract, not just
translating the call.

### B5.2 Call site migration: closed (issue #2)

**Correction to the figure in B5.1**, made while migrating against the
report's symbol table instead of manual grep: `GetProfileString`(17) +
`GetProfileInt`(14) + `WriteProfileString`(11) = **42** real sites, not
43. The remaining symbol the report groups under *Configuration
persistence* is `GetEnvironmentVariableA` (`dlgmisc.c:2145`), an API
unrelated to `WIN.INI`/`QSettings`, out of scope for this contract.
`profwin.c` does not appear in the inventory because it is not in
`OPUS_ORIGINAL_ENGINE_SOURCES`, it does not compile in this build, and
its 3 sites (`GetProfileIntPR`/`GetProfileStringPR`/`WriteProfileStringPR`,
the target of `debugwin.h`'s redirection under `DEBUG`, none defined
here) were never part of the count. `print2.c:848` is not enumeration,
it has a real key (`pchPrinters`), unlike `print2.c:833`: only that one
stays excluded.

**41 of 42 sites migrated**, across 12 files: `ddesub.c`(1), `dlgmisc.c`
(1), `elmisc.c`(2), `fieldpic.c`(4), `filecvt.c`(6), `filewin.c`(3),
`init2.c`(1), `initwin.c`(7), `print2.c`(4 of 5), `quit.c`(8), `wproc.c`
(2), `debug/debugcmd.c`(2). Each site is wrapped in
`#if defined(__GNUC__) && !defined(_MSC_VER) / #else / #endif` per
`CONTRIBUTING.md`: the GNUC branch calls `OpusShellProfile*`, the MSVC
branch keeps the original call unchanged, verified by diff against the
prior tree, zero deltas beyond incidental retyping whitespace.
`src/core/include` was added to `OPUS_ORIGINAL_INCLUDE_DIRS` so that
`#include "OpusShellConfig.h"` resolves.

**Real verification, not just of the complete target.** `ninja -k 0 -C
build/linux-winelib-debug opus_original_engine` does not reach 0 FAILED,
but because of the already documented blocker that predates this work:
`Opus/wordtech/disp.h:248` and `Opus/rsb.h:38,73`, flexible array member
under GCC 14.2, unrelated to this migration. To avoid leaving the
verification hanging on that blocker, each of the 12 files was also
compiled in isolation with the real command from `ninja -t commands`: all
12 reach the same point (`ddesub.c`, `filewin.c`, and
`debug/debugcmd.c` compile clean end to end; the other 9 fail exactly at
`disp.h`/`rsb.h`, never on code from this change). Zero errors and zero
warnings mention `OpusShellConfig`/`OpusShellProfile*` in any log.

**Conditional compilation states surveyed, not assumed:** of the 41
sites, 6 sit inside branches inactive in this build today,
`initwin.c:530` and `wproc.c:473,497` under `#ifdef DEBUG`/`HYBRID`
(neither defined), `initwin.c:578-579` under `#ifdef HYBRID`,
`initwin.c:1136` under the `#else` of `#ifdef OPUS_X64` (which is
defined, so that `#else` never compiles), `init2.c:570` under `#ifdef
MKTGPRVW`, and the whole `debug/debugcmd.c` block under `#ifdef DEBUG`
besides not being in any target. They were migrated anyway, for
consistency and so as not to leave a mix of old and new API if they are
ever activated.

**What remains:** the fourth issue for `print2.c:833` (extend the
contract with an enumeration function, or handle it separately). Whether
`opus_shell_config` actually links against a Winelib binary remained open
at the close of this step, it was resolved separately, see below.

---

## Qt-3 reconnaissance: clean `disp.h`/`rsb.h` candidates with no real site

Three `wordtech/` TUs identified in an earlier reconnaissance as clean of
`disp.h`/`rsb.h` (directly and transitively, same method as §B2.7/B4.4)
but not yet assigned to any B section:
`src/Opus/wordtech/sttb.c`, `src/Opus/wordtech/inssubs.c`,
`src/Opus/wordtech/prl.c`. The purpose of this reconnaissance was to find
the next real connectable site while `disp.h`/GCC 14 remains blocked.
**Result: negative in all three, there is no site that requires (or
already has) a Qt core contract.** It is documented as such, the same as
a positive result would be, so as not to repeat the search.

Verified with `git log --oneline --all -- <file>` that none of the three
was touched by work from any earlier B section (they only appear in
`a1c4a1f Initial commit`, and `inssubs.c` also in two commits from the
original port phase, `c40d4e0 Fase 3: compilar el motor a 0 errores` and
`27a2f60 Full operating system font support`, neither of which touches
`MessageBox`/`Global*`/GDI).

### `src/Opus/wordtech/sttb.c`

Four grep families, each empty:

```
$ git grep -n 'MessageBox' -- src/Opus/wordtech/sttb.c
$ git grep -n 'MessageBeep' -- src/Opus/wordtech/sttb.c
$ git grep -nE 'Global[A-Z][a-zA-Z]*' -- src/Opus/wordtech/sttb.c
$ git grep -nE 'GetDC|ReleaseDC' -- src/Opus/wordtech/sttb.c
$ git grep -nE 'HFONT|SelectObject|CreateFont|GetTextMetrics|GetTextExtent|GetCharWidth' -- src/Opus/wordtech/sttb.c
```

(no output on any of the five)

It does have one real API family, found by inspecting the file:
`HqAllocLcb`/`FreeHq`/`UnlockHq`/`HpOfHq` on the `HQ` type, 8 sites
(lines 70, 111, 131, 177, 286, 494, 546, 736).

| Site (line) | API | Existing/missing contract | Complexity |
|---|---|---|---|
| 70, 111, 131, 177, 286, 494, 546, 736 | `HQ`/`HqAllocLcb`/`FreeHq`/`UnlockHq`/`HpOfHq` | **None is needed.** `HqAllocLcb` is not Win16 `GlobalAlloc`, it is a macro already resolved by the x64 port (`src/port/original/opus_x64_heap.h:41`, `#define HqAllocLcb(cb) ((HQ)OpusHAllocateCb((size_t)(cb)))`), backed by a proprietary native allocator (`OpusHAllocateCb`/`OpusDerefH`, stable pointer-to-pointer handles, unrelated to Win16 segmented memory). Out of scope for B3: B3 is the ~201 real `Global*`/`GMEM_*` sites (the `GlobalAlloc`/`GlobalLock`/`GlobalFree` Win32 API), not Opus's internal `HQ`, which is already portable as is. | **N/A, not pending work.** Already ported, nothing to replace. |

`sttb.c` is a disp.h-clean candidate, but not a *work* candidate: no real
Win16/GDI site remains inside the file.

### `src/Opus/wordtech/inssubs.c`

Same six families, all empty:

```
$ git grep -n 'MessageBox' -- src/Opus/wordtech/inssubs.c
$ git grep -n 'MessageBeep' -- src/Opus/wordtech/inssubs.c
$ git grep -nE 'Global[A-Z][a-zA-Z]*' -- src/Opus/wordtech/inssubs.c
$ git grep -nE 'GetDC|ReleaseDC' -- src/Opus/wordtech/inssubs.c
$ git grep -nE 'HFONT|SelectObject|CreateFont|GetTextMetrics|GetTextExtent|GetCharWidth|CreateDC' -- src/Opus/wordtech/inssubs.c
$ git grep -nE 'HqAllocLcb|LpLockHq|UnlockHq|FreeHq|HAllocateCw|HAllocateCb' -- src/Opus/wordtech/inssubs.c
```

(no output on any of the six)

Additional inspection (heuristic sweep of every call starting with a
capital letter, discarding Opus's own obviously internal functions) found
no other Win16/GDI API, the file is field/page-number insertion logic and
file-byte conversion (`ReadRgchFromFn`, `WriteRgchToFn`, `PnAlloc`,
`MapStc`, etc.), all internal to Opus, none crosses into Win32.

| Site (line) | API | Existing/missing contract | Complexity |
|---|---|---|---|
| (none) | (none) | (none) | **No sites.** Nothing to connect. |

### `src/Opus/wordtech/prl.c`

Same six families, all empty:

```
$ git grep -n 'MessageBox' -- src/Opus/wordtech/prl.c
$ git grep -n 'MessageBeep' -- src/Opus/wordtech/prl.c
$ git grep -nE 'Global[A-Z][a-zA-Z]*' -- src/Opus/wordtech/prl.c
$ git grep -nE 'GetDC|ReleaseDC' -- src/Opus/wordtech/prl.c
$ git grep -nE 'HFONT|SelectObject|CreateFont|GetTextMetrics|GetTextExtent|GetCharWidth|CreateDC' -- src/Opus/wordtech/prl.c
$ git grep -nE 'HqAllocLcb|LpLockHq|UnlockHq|FreeHq|HAllocateCw|HAllocateCb' -- src/Opus/wordtech/prl.c
```

(no output on any of the six)

Same heuristic sweep: only Opus-internal functions on `PRL`/tabs
(`AddPrlSorted`, `ApplyPrlSgc`, `ApplySprm`, `DeleteTabs`, etc.) plus the
already-ported macro `LpFromHp` (same `opus_x64_heap.h` as in `sttb.c`,
not a new site). No Win16/GDI API.

| Site (line) | API | Existing/missing contract | Complexity |
|---|---|---|---|
| (none) | (none) | (none) | **No sites.** Nothing to connect. |

### Conclusion of this reconnaissance

None of the three is a viable Qt-3 work candidate, despite being clean of
`disp.h`/`rsb.h`: `sttb.c` only touches already-portable memory (`HQ`,
out of scope for B3), and `inssubs.c`/`prl.c` have no Win16/GDI surface
at all. The next real connectable site while `disp.h`/GCC 14 stays
blocked is not among these three, the search needs to be repeated over
another subset of the already-inventoried list of clean candidates
(`src/Opus/debug/debugdde.c`, `debugdlg.c`, `debuggdi.c`, `debugrep.c`,
`debugwin.c`, or the rest of the clean TUs in `Opus/` root listed in the
earlier reconnaissance), not assumed that "clean of `disp.h`" implies
"has pending work".

---

## Verifying the physical boundary: does it actually link?

Up through B5.2, only `opus_shell_config` compiling in isolation (§B5.1)
and the 41 call sites compiling under GNUC inside `opus_original_engine`,
a **static** library that does not force external symbol resolution, had
been confirmed. Real linking had never been tested: a Winelib binary
(winegcc/wineg++) linking against a native gcc/g++ library that depends
on Qt6. That test is what underpins the entire boundary architecture, not
just the configuration contract, if it did not link, the other three
contracts (B2, B3, B4) would inherit the same problem the day they get
implemented.

**Verdict: it links, with two minor adjustments, neither structural.**
Probe in `docs/port-qt/scripts/link-check/` (`link_check.c`, compiled
with winegcc, calling the real
`OpusShellProfileWrite`/`OpusShellProfileString` against the
`libopus_shell_config.a` that `opus_core_build` produces).

1. **`-fPIC` on the native library.** The first direct-link attempt gave
   `relocation R_X86_64_PC32 ... can not be used when making a shared
   object; recompile with -fPIC`. Cause, verified with `winegcc -v`:
   winegcc's final link step is literally
   `gcc -m64 -shared -Wl,-Bsymbolic -o foo.exe.so ...`, that is how
   Winelib builds its "executables" (the `.exe` is a stub Wine loads, the
   real code lives in `.exe.so`). A `-shared` binary does not accept
   objects without position-independent code. This is not a header
   detail: it is how Winelib builds binaries, with or without Qt in the
   picture. Adjustment: `opus_shell_config` now compiles with
   `POSITION_INDEPENDENT_CODE ON` (`src/core/CMakeLists.txt`), with a
   note for the three remaining contracts to inherit the same property
   when implemented.
2. **Explicit `-lstdc++`, only with plain `winegcc`.** With `-fPIC`
   applied, the second attempt gave `undefined reference to
   __gxx_personality_v0` (the C++ exception-handling routine). `winegcc`
   is a C driver: it does not link `libstdc++` by default, and
   `opus_shell_config.cpp` is C++ (it uses `QString`, which can throw
   internally). With `-lstdc++` added to the link command, it resolves.
   **Additional fact that reduces the real impact of this point:**
   `WORD1` does not link with `winegcc` but with `wineg++`, forced by
   `target_compile_features(WORD1 PRIVATE cxx_std_20)`, because the
   target already mixes `.cpp` sources (`port/original/opus_asm_*.cpp`,
   etc.). Explicitly tested: with `wineg++` as the linker, linking works
   **without** adding `-lstdc++` by hand, because the C++ driver already
   links it by design. The `-lstdc++` adjustment stays documented in case
   some future target links against these libraries with plain
   `winegcc`.

**Runtime proof, not just a link proof.** The resulting binary runs under
Wine and executes the real call:
`OpusShellProfileWrite("LinkCheck", "Saludo", "hola desde winegcc")`
followed by `OpusShellProfileString` on the same key returns `cch=18`,
value `"hola desde winegcc"`, the full string, intact, back across the
boundary. Confirmed with both linkers (`wineg++` and `winegcc +
-lstdc++`) and with the real library `opus_core_build` builds, not a
scratch copy.

**Ruled out as the cause:** C++ name mangling (the four `OpusShell*`
headers already declared `extern "C"` since they were written, verified
before touching anything) and the Win32 calling convention/ABI (the
object `winegcc -c` produces is standard ELF x86-64 SysV, same as
`gcc`'s; Winelib does not change the calling ABI in x64 mode, it only
provides Win32 headers and types, the only real friction was code
generation (`-fPIC`) and linked runtime (`libstdc++`), not calling ABI).

**Consequence for B3/B4/B2:** the boundary architecture, native Qt6/gcc
core plus winegcc/wineg++ shell linked into the same binary, is now
confirmed, not just assumed. The three remaining contracts can proceed on
this same scheme without redesign; when each gets its first
implementation, apply `POSITION_INDEPENDENT_CODE ON` to its library in
`src/core/CMakeLists.txt` from the first commit, and if the consuming
target ends up linking with plain `winegcc` instead of `wineg++`, add
`-lstdc++` to its link line.

---

## Recommended sequence for Qt-2

The order is not arbitrary: each step leaves the next one verifiable.

1. **Configuration (B5): closed.** 42 real sites (not 43, see §B5.2), a
   trivial contract. It served to establish the boundary mechanism, how
   the core calls the shell, on something whose failure is instantly
   visible and whose risk is nil. 41 migrated; 1 (`print2.c:833`,
   enumeration) awaits a contract extension in a separate issue.
2. **Font substitution table (§B2.5).** Extract from the oracle the real
   mapping of era names to physical files. It is cheap, it is a
   prerequisite for any fidelity test, and today it is not written down
   anywhere.
3. **Memory (B3): implementation and verification closed, call site
   migration pending.** The opaque allocator exists
   (`src/core/src/OpusShellMemory.cpp`) and proved, with a real handle,
   that it survives Alloc/Lock/write/Unlock/Lock/Free across
   winegcc/wineg++ (§B3.5). Still to do: replace the 201 `Global*` sites
   in `Opus/` with `OpusMem*`, a prior issue per `CONTRIBUTING.md`, same
   as configuration.
4. **Enumeration of serialized handles (§B3.3): closed.** No persisted
   structure has a handle field. It did not stay as a pending inventory
   item: it was done before designing the header, not after.
5. **Text measurement (B2): initial implementation closed, verified with
   2660 data points, not just one.** `src/core/src/
   OpusShellFontMetrics.cpp` implements the contract with the §B2.3
   strategy. `opus_shell_font_metrics_fidelity_test` compares against a
   table captured from the real Winelib oracle
   (`docs/port-qt/scripts/fidelity/capture.py` ->
   `opus_shell_font_metrics_oracle_table.h`): 4 era names x 7 sizes
   (8-36pt) x 95 printable ASCII characters = 2660 widths. **2660/2660
   match exactly** the oracle, not approximate, not "close". Ascent/
   descent, which §B2.3 did not cover, are also compared (+/-1px,
   different rounding between Qt's `ascent()`/`descent()` and GDI's
   integer `tmAscent`/`tmDescent`). Covers the 4 known `ftc` values,
   regular weight only (`catr != 0` fails cleanly, GDI synthesizes bold/
   italic, `QRawFont` does not), screen at fixed 96 dpi (no printer).
   Still the piece the fidelity restriction depends on, and the one that
   lets `wordtech/` compile without GDI.
5b. **First real caller connected (§B2.7).** `Opus/LOADFONT.C:187
   C_LoadFcid`, screen/variable-pitch path, calls `OpusShellCharWidths`
   instead of GDI. `src/core` tests green (including the 2660-point
   fidelity test), but with no runtime verification against real
   `WORD1`, two blockers unrelated to this work prevent it (compiling
   `Opus/wordtech/disp.h` under GCC 14, `WORD1` startup already broken
   before this). `scroll.c`/`disp3.c`/`pagevw.c` remain unblocked: the
   document's criterion calls for verification against real layout, not
   just type-level connection.
6. **`error.c`, then `editspec.c` and `undo.c` (§B4.3): contract
   implemented.** `src/core/src/OpusShellSpine.cpp`: `OpusShellReportError`
   (`QMessageBox::Critical`, `Qt::ApplicationModal`, the real equivalent
   of `MB_SYSTEMMODAL`) and `OpusShellAlert` (`QApplication::beep()`).
   Tested with a real modal dialog, auto-closed from
   `QTimer::singleShot` once `QApplication::activeModalWidget()` confirms
   it active, not a stub. Not connected yet to any call site in `Opus/`
   (those three files still remain unmigrated, still use real
   `MessageBox`/`MessageBeep`) nor to `opus_qt_shell` (deliberate: a
   modal dialog triggered automatically on every scaffold startup would
   be noise, not a useful check).
7. **Message loop inversion (§B4.1): pattern demonstrated, moved ahead of
   sequence by explicit maintainer decision 2026-08-11 (B2 verified in
   isolation, not against real `wordtech/`; risk accepted knowingly, not
   an oversight).** `opus_qt_shell` runs under the `QApplication` loop,
   with no `GetMessage`/`DispatchMessage` anywhere in the binary, and
   already calls inward with the two dispatch patterns of §B4.2: "Dispatch
   (B4.2)" menu, direct action (`SendMessage` -> synchronous call in the
   same cycle) and deferred action (`PostMessage` ->
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, runs on a
   later cycle). An on-screen log makes the difference observable, not
   just claimed. **What this is NOT:** the inversion of `Opus/wproc.c`,
   which remains the real driver of the document engine, and `Opus/` is
   a restricted tree, untouched. This is the dispatch mold to reuse the
   day `wordtech/` gets connected, not the migration itself. The Winelib
   oracle remains necessary to verify fidelity; moving this step ahead
   does not replace or invalidate it, it only stopped being a hard block
   while waiting for B2 against a real document.

---

## Open questions

The two this document had about fidelity were closed in §B2.3 and §B2.5.
The two that remained after that round, boundary API location and the
`opustlbx/` verdict, are closed here:

1. **Boundary API location: `src/core/include/`.** Not `port/`: that
   directory is temporary compatibility scaffolding (LP64 over Winelib,
   host build), semantically distinct from the new core's stable API.
   The four contract headers (`OpusShellFontMetrics.h`, `OpusShellMemory.h`,
   `OpusShellSpine.h`, `OpusShellConfig.h`) live there; see the header
   inventory in the following section.
2. **`opustlbx/` resolved: exclude, not include.** There is a real
   relationship with `port/original/toolbox.h`, `toolbox.h`'s own header
   comment says so: it is the hand-written successor of the *generated*
   `toolbox.h`, and `opustlbx.c` is precisely that generator (it reads
   `Opus/resource/toolbox.txt` and emits the `.h` with the `tlbx`
   far-inter-segment call mechanism plus the `.asm` with the
   `mptlbxpfn`/`tlbxMac` table that `Opus/asm/int3f.asm` and `CkTlbx` in
   `Opus/debug/debugstr.c` consume). But the relationship is one of
   textual lineage, not active coupling: `opustlbx` has no CMake target,
   generates nothing the current build uses, and what it generates
   belongs to the `Opus/asm/` call mechanism, already out of scope for
   this branch. `toolbox.h` is indeed an active port layer, `Opus/
   windows.h` and thirteen `wordtech/`/`interp/` TUs include it directly,
   but that does not drag `opustlbx` along with it: the successor does
   not depend on its predecessor's generator. Consequence: `opustlbx.c`/
   `.h` move from "defer" to "exclude" in the triage; `cashmere/fldexp/`
   (4 files) stays "defer" unchanged, nothing turned up in this
   investigation to justify otherwise. Triage closed: 4 defer, 54
   exclude.

   Side note, not a new task: `CkTlbx` (`Opus/debug/debugstr.c:2039`)
   references `tlbxMac`/`mptlbxpfn`, symbols defined only in
   `Opus/asm/int3f.asm`. `Opus/debug/` is in scope by project decision
   and `Opus/asm/` is not; that TU already had a known link gap before
   this investigation, independent of the `opustlbx` verdict.

**New, open, not resolved in this document, found while doing B3 Phase 1
(§B3.3), not sought on purpose:**

3. **§B2.1 describes the wrong structure. RESOLVED 2026-08-11, does not
   invalidate §B2.2/§B2.3, corrects the narrative.** The central citation
   of the text measurement contract (`Opus/wordtech/format.h:379-410`,
   `dxuFrac`/`bmpchdxu`/`struct FONTREC far * far *qqftr`) is `#ifdef MAC`
   code, dead in this build. The real `struct FTI` that compiles under
   `WIN`/`OPUS_X64` is in `Opus/fontwin.h:126-152`, and its cache twin is
   `struct FCE` (`Opus/fontwin.h:96-124`, first `cbFtiFceSame` bytes
   identical to `FTI` by design). Neither has a fraction accumulator,
   **the real width table does not live inline in `FTI`, it lives in
   `FCE.hqrgdxp`**: a Win16 handle (`HQ`, the same handle family the B3
   contract already covers) to an array of **256 `int`** (one per byte
   value 0-255, width already in integer pixels, no fraction), allocated
   with `HqAllocLcb(256*sizeof(int))` (`Opus/initwin.c:2946`). The layout
   consumer does not read `FTI`/`FCE` character by character directly: it
   copies widths into `vfli.rgdxp[]` (formatted-line result struct,
   `Opus/disp1.c:761` and surrounding code) during `FormatLine`, and that
   copy is indeed plain integer arithmetic, no fraction, confirming, not
   contradicting, the absence of an accumulator.

   **Consequence for the §B2.3 strategy: none.** The strategy (integer
   ppem plus `QFont::PreferFullHinting`, measured against the observable
   behavior of `GetTextExtent`/`GetTextMetrics`) never depended on the
   internal layout of `FONTREC`/`FTI`, it was validated against GDI's
   *behavior*, not against a data structure. A 256-integer pixel array
   is, if anything, simpler to fill than the fraction-accumulator model
   §B2.1 described: it does not change what `OpusShellCharWidths` asks
   for (integer advances), only the name of the internal target
   structure, which the contract no longer exposes.

   **Consequence for the contract (`OpusShellFontMetrics.h`):
   `OpusFontMetrics`/`OpusShellFontMetrics()` remain intact** (ascent,
   descent, overhang, the same fields exist in `FCE`/`FTI` with the same
   names `dypAscent`/`dypDescent`/`dxpOverhang`). `OpusShellCharWidths(key,
   chFirst, cch, rgdxu)` **remains the correct signature**, but in this
   build the real caller will always request `chFirst=0, cch=256` (the
   full range of `FCE.hqrgdxp`), no partial-range support is needed in
   the first implementation, though the contract already allows it if
   some future consumer needs it. **New, not covered by today's
   contract:** `FCE`/`FTI` have fields `OpusFontMetrics` does not expose
   (`dypXtraAscent`, `fVisiBad`, `fPrvw`, `dxpBorder`/`dypBorder`,
   `dxpExpanded`), it is not yet known which of these the layout engine
   reads besides ascent/descent/overhang/widths; audit before declaring
   the first implementation of `OpusShellFontMetrics()` complete.

   **Located 2026-08-11: `C_LoadFcid()` in `Opus/LOADFONT.C:187` (under
   `#if defined(DEBUG) || defined(OPUS_X64)`, live in this build).** It
   is the full font load/cache function (header comment: "last `ifceMax`
   fonts requested through LoadFcid are kept in a LRU cache"), not
   something hidden in `dispspec.c`. Real fill path for variable-pitch
   fonts, `LOADFONT.C:391-434`:

   1. `CreateFontIndirect(&lf)` (`:315`) selects the physical font.
   2. `GetTextMetrics(hdc, &tm)` (`:340`).
   3. `pfce->hqrgdxp = HqAllocLcb(256 * sizeof(int))` (`:398`, `OPUS_X64`
      branch), the same 256-`int` array already identified.
   4. Bulk fill: `GetCharWidth(hdc, chDxpMin, chDxpMax-2, lpdxp)`
      (`:421`, real Win32 API, not a project function) for characters
      0-253; 254 is filled separately (`OurGetCharWidth(hdc, chDxpMax-1,
      chDxpMax-1, ...)`, `:426`). Two cases fall back to the per-
      character path instead of the bulk one: preview mode (`vfPrvwDisp`,
      `:415-416`) always, and any driver that fails `GetCharWidth`
      (`:421-425`, with `ReportSz("Driver does not support
      GetCharWidth!")`).
   5. **`OurGetCharWidth()` (`LOADFONT.C:976-991`) is literally the
      character-by-character `GetTextExtentPoint32A(hdc, &ch, 1, &size)`
      loop the §B2.3 probe already reproduces**, the same mechanism, not
      an analogous one. For the screen (not printer, not preview),
      everything goes through `GetCharWidth`, not this fallback, but both
      are thin wrappers over the same underlying GDI measurement.
   6. Overhang correction: if `pfce->dxpOverhang != 0`, it is subtracted
      from **the whole** already-filled table (`:428-434`), once over the
      array, not per character during fill. Net identical to the
      `GetTextExtent(hdc,&ch,1) - tm.tmOverhang` per-character subtraction
      the §B2.3 probe uses (subtraction distributes over sum), so **the
      strategy measured in §B2.3 already reproduces this step without any
      further change**, there is no new overhang correction to
      incorporate.
   7. `LLoadFce:` (`:523`) is the convergence point with the stream-
      restore path (`initwin.c:2909-2965`): it copies `pfce->hqrgdxp` into
      `pfti->rgdxp[256]` via `bltbh` (`:544-547`) regardless of the
      array's origin. Fixed-pitch font: no table, `pfce->dxpWidth =
      tm.tmAveCharWidth` (`:386`) replicated to all 256 positions with
      `SetWords` (`:539`).

   **Consequence:** closed. §B2.3 needs no adjustment, it measured
   exactly the mechanism `C_LoadFcid` uses (GDI per-char / bulk
   `GetCharWidth`, same net result after overhang). The one real new
   finding: **`OpusFontKey.ftc` is not enough as the contract's input
   key without the `ftc -> era name` table that today only exists inside
   `Opus/initwin.c` (`vhsttbFont`, verified order: `ibstFontDefault` =
   Tms Rmn, `+1` Symbol, `+2` Helv, `+3` Courier,
   `Opus/initwin.c:1541-1583`).** `OpusShellFontMetrics()`/
   `OpusShellCharWidths()` as declared receive `ftc` (an integer with no
   meaning outside that table), but the shell
   (`OpusShellFontSubstitution`) only knows era names (strings). Before
   writing the real implementation a decision is needed: (a) the core
   translates `ftc -> name` before calling the shell (the contract
   already says "the core translates", §B2.2, so this can be as simple
   as adding that 4-entry table to the core side of the wrapper, not to
   the boundary header); or (b) the contract changes to receive the name
   directly. (a) requires no changes to `OpusShellFontMetrics.h`.

4. **Should the Qt core (`src/core`) and the Winelib port live in
   separate repositories, so the core ends up as a real Qt application?
   Asked by the maintainer 2026-08-14, not separated for now:**

   Today `src/core` is not an application: it is five narrow static
   libraries (`opus_shell_config`/`memory`/`font_substitution`/
   `font_metrics`/`spine`) whose only reason to exist is to be linked
   inside `WORD1` (Winelib) and be called from call sites inside `Opus/`,
   a restricted tree of this same repo (see "Actual status as of today"
   above). Separating now would move half the coupling, the contract
   headers, the `ExternalProject_Add(opus_core_build ...)` in `src/
   CMakeLists.txt`, across a repository boundary without eliminating it:
   `Opus/` would still need to link against that code on every `WORD1`
   build, now via a submodule or an installed package instead of a
   subdirectory of the same checkout. And the §B2.3/§B2.5 fidelity
   strategy depends on capturing real behavior from the Winelib oracle
   (`docs/port-qt/scripts/fidelity/capture.py` ->
   `opus_shell_font_metrics_oracle_table.h`): the already-captured table
   travels fine across repos (it is a generated header, not a binary),
   but regenerating it when the Wine version changes requires having the
   Winelib oracle alongside, in the same checkout or a manually
   synchronized sibling.

   Separation starts to make sense the day a Qt executable exists that no
   longer depends on `Opus/`/Winelib for anything, that is, when step 7
   of "Recommended sequence for Qt-2" stops being a demonstration
   (`opus_qt_shell`, no document engine) and becomes the real migration
   of `Opus/wproc.c`. Before that, splitting repos buys no independence:
   it only changes where the same coupling lives, and adds cross-version
   friction (which `src/core` commit pins which `WORD1` commit?) that a
   single `git log` over one tree resolves for free today.

   Not closed forever, reopen when `opus_qt_shell` (or a successor) has
   its own document engine and depends on `Opus/` in zero places, not
   before.

   **Addendum, same day:** the finding that `WORD1.exe.so` already links
   at runtime against `libQt6Widgets/Gui/Core/DBus.so.6` (see "Actual
   status as of today" above) reinforces this recommendation rather than
   changing it, the coupling between `src/core` and `WORD1` is not just a
   build-time one (`ExternalProject_Add`) but a load-time one on every
   binary startup. That is more reason, not less, not to split it into
   two repos while that link exists.

---

## Build blocker: `disp.h:248` on GCC 14 (Debian VPS): diagnosis only

**Actual status: reproduced and characterized, not resolved.** It is not
a bug in this fork: also reproduced with `git stash` before any change of
its own, so it predates this branch's work. Not reproducible on Fedora
44/GCC 16.1.1 (no access to that machine this session, reported data, not
re-verified here). `src/Opus/` and `src/OpusEtAl/` were not touched: only
reading, isolated test compilation, and searching.

### The exact code

`Opus/wordtech/disp.h:235-250`:

```c
struct PLDR
	{
	int     idrMac;
	int     idrMax;
	int     cbDr;   /* set to cbDR */
	int     brgdr;  /* set to point to rgdr */
	int	fExternal;
	struct PLDR **hpldrBack;
	int     idrBack;
	struct PT ptOrigin;
	int     dyl;
	union   {
		HQ	hqpldre;    /* when fExternal true */
		struct DR rgdr[];   /* when fExternal false */
		};
	};
#define cwPLDR   (sizeof(struct PLDR) / sizeof(int))
```

The construct is a flexible array member (`struct DR rgdr[]`) as a member
of a `union`, alongside `HQ hqpldre`. Present since the fork's initial
commit (`a1c4a1f`, `git blame` shows no later commit touching these
lines), not a regression introduced on this branch.

### The error, verbatim

Real compilation via `cmake --build --preset linux-winelib-debug --target
opus_original_engine`, triggered by `port/original/opus_x64_layout.c:6`
(`#include "wordtech/disp.h"`):

```
/home/pablo/msword/src/Opus/wordtech/disp.h:248:27: error: flexible array member in union
  248 |                 struct DR rgdr[];   /* when fExternal false */
      |                           ^~~~
winegcc: /usr/bin/gcc failed
```

Real flags for that TU (captured with `ninja -t commands`):

```
winegcc -DCRLF -DNOMINMAX -DNONATIVE -DOPUS_X64 -DWIN -DWIN23 [...] \
  -g -std=gnu89 -funsigned-char -fms-extensions -fpermissive -MD [...] \
  -c src/port/original/opus_x64_layout.c
```

**Key fact: `-fms-extensions` is already active in the real build** (an
existing CMake line) and the error persists. It is not a `-Werror`, it is
a direct `error:` from the C front end, with no `[-W...]` prefix, so no
warning-to-error flag controls it.

### Exact cause: confirmed, not assumed

Reproduced in isolation (`gcc -std=gnu89 -fms-extensions -fpermissive`,
same result). With `-pedantic-errors` the same case also separately
triggers `ISO C90 does not support flexible array members
[-Wpedantic]`, but that is a *different* diagnostic (gateable via
`-Wpedantic`); the one that actually blocks the build (`flexible array
member in union`, no `-W` suffix) does not appear under any warning name:
it is an unconditional `error_at()` in GCC 14's C front end, in
`c/c-decl.cc` (message located at `#: c/c-decl.cc:9556` in the source
tree of `gcc-14_14.2.0-19.debian.tar.xz`, confirmed by downloading the
real Debian source package, not by inspecting memory).

**Why GCC 16 does not reproduce it: confirmed by search, not assumed:**
GCC officially accepted PR53548 ("allow flexible array members in
unions") as commit `r15-209` (GCC 15 development branch, May 2024), which
turned that unconditional `error_at()` into a `pedwarn` (a pedantic
warning, not a hard error) and documented the construct as a supported
extension ("Flexible Array Members in Unions" in the official GCC
documentation:
<https://gcc.gnu.org/onlinedocs/gcc/Flexible-Array-Members-in-Unions.html>,
confirms "GCC permits a C99 flexible array member (FAM) to be in a
union"). Debian's `gcc-14.2.0` predates that change (GCC 14 branched
before May 2024); Fedora 44 with GCC 16.1.1 inherits it. **It is a
compiler version difference, not a matter of flags or language mode**,
that is why no `-std=`/`-fms-extensions`/`-fplan9-extensions` flag moves
it: support is not gated behind a flag, it is gated behind the front end
having the patch applied.

References used (web search, not model memory):
- <https://gcc.gnu.org/pipermail/gcc-cvs/2024-May/401756.html>: commit
  `r15-209`, "C and C++ FE changes to support flexible array members in
  unions and alone in structures."
- <https://www.mail-archive.com/gcc-patches@gcc.gnu.org/msg364477.html>:
  a later patch series (PR119001, February 2025) refining initialization
  cases, confirms the support keeps being active and evolving in
  branches after GCC 15.
- <https://gcc.gnu.org/onlinedocs/gcc/Flexible-Array-Members-in-Unions.html>

### Flags tested: none resolve it, in this order

Isolated repro (`/tmp/.../repro.c`, exact same pattern: `union { HQ;
struct DR rgdr[]; }`), each with identical literal output (`error:
flexible array member in union`, exit 1):

| Flag tested | Result |
|---|---|
| `-fms-extensions` (already active in the real build) | fails |
| `-fplan9-extensions` | fails |
| `-std=gnu17 -fms-extensions` | fails |
| `-std=gnu2x -fms-extensions` | fails |
| `-Wno-error=pedantic -fms-extensions` | fails (confirms it is not a `-Werror`; the error carries no `-W` tag, so there is no warning to downgrade) |

None of the five change the result. Expected given the above: GCC 14's
`error_at()` is unconditional, it does not depend on C dialect or on any
GNU/MS/Plan9 extension enabled, only on the front-end patch that arrived
in GCC 15.

### Minimal code candidate: NOT applied, proposed for review only

There is no compiler flag that resolves this on GCC 14. The minimal
change would be in `disp.h`, guarded as project discipline requires for
Linux-only code inside `Opus/`:

```diff
--- a/src/Opus/wordtech/disp.h
+++ b/src/Opus/wordtech/disp.h
@@ -244,7 +244,11 @@ struct PLDR
 	union   {
 		HQ	hqpldre;    /* when fExternal true */
+#if defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)
+		struct DR rgdr[1];  /* when fExternal false -- GCC <15 rejects FAM in union (PR53548, r15-209) */
+#else
 		struct DR rgdr[];   /* when fExternal false */
+#endif
 		};
 	};
```

Why `rgdr[1]` and not something else: it is not a newly imported pattern,
it is exactly the idiom `struct WWD` already uses in the same file
(`disp.h:471`, original comment `/* WWD is a pldr */`), for the same
purpose (a variable-size `struct DR` tail after a fixed header). Verified
before proposing it: `cwPLDR` (the only macro depending on
`sizeof(struct PLDR)`, `disp.h:251`) has no usage site anywhere in
`src/Opus/**/*.c` (empty `grep`), so the only observable effect of moving
from `rgdr[]` to `rgdr[1]` (that `sizeof(struct PLDR)` grows by
`sizeof(struct DR)`) never reaches any real allocation calculation. All
real access to the array is via pointer (`&(pwwd)->rgdr[0]` in the
`PdrGalley` macro, same file) or via hand-computed offsets using `cbDr`,
not via `sizeof` of the whole struct, the pre-C99 "struct hack" pattern
`rgdr[1]` implements is semantically neutral here, not just "it
compiles".

**Not implemented.** Pending explicit authorization (a GitHub issue) to
touch `src/Opus/`, per project discipline.

### Applied and verified 2026-08-12: explicitly authorized

The change above was applied as is (same guard, same `[1]` idiom), **plus
a second finding of the same kind, exposed only once the first one was
unblocked:** `Opus/rsb.h:38` (`struct BMS rgbms []`, inside `struct
RSBI`) and `Opus/rsb.h:73` (`struct ZPP rgzpp[]`, inside `union GRPZPP`),
both diagnosed by GCC 14 as *"flexible array member in a struct with no
named members"*, a different variant of the same unconditional
`error_at()` (covered by the same upstream commit `r15-209`/PR53548,
which explicitly says "in unions **and alone in structures**"). Confirmed
preexisting with `git stash` before touching `rsb.h`: the error was
already there, it was just hidden behind the `disp.h` block because
`ninja` never reaches compiling `cmdwnd.c` (the first TU that pulls in
`rsb.h`) until another TU running in parallel fails first. Both sites got
the same guard, `#if defined(__GNUC__) && !defined(_MSC_VER) &&
(__GNUC__ < 15)`, with `rgbms[1]` / `rgzpp[1]`, explicitly authorized by
the maintainer after the finding was reported (not part of the originally
approved scope, permission was requested separately before touching the
second file).

**This is a compiler-version compatibility shim, not a design change.**
The layout of `struct PLDR`, `struct RSBI`, and `union GRPZPP` is the
same in intent (a variable-size tail after a fixed header/alias over
named fields); the only thing that changes is which C expression GCC 14
accepts to declare it. Under GCC >=15 (the `#else` branch of each guard)
the code remains the original flexible array member `[]`, with no
behavior change, the guard is symmetric and touches neither the MSVC path
(`_MSC_VER` excluded) nor GCC >=15.

**Neutrality of `cwPLDR` confirmed, not just argued:** `grep -rn cwPLDR
src/Opus` before and after the change returns only the macro's
definition (`disp.h:251`), zero usage sites. `izppMax` (`rsb.h:89`,
`sizeof(union GRPZPP)/sizeof(struct ZPP)`) did not move either: the
union's named branch (5 `struct ZPP`) remains strictly larger than the
branch with `rgzpp[1]` (1 `struct ZPP`), so `sizeof(union GRPZPP)` did
not change, same reasoning for `struct RSBI` versus
`ibmsMax`/`ibmsMax2`/`ibmsMax3` (literal constants, not derived from
`sizeof`, and in any case the named branch of 5-9 `BMS` dominates over
`rgbms[1]`).

**Real verification performed (Debian VPS, GCC 14.2.0, this build):**

1. `cmake --build --preset linux-winelib-debug --target opus_original_engine`:
   **compiles and links clean (exit 0)**, no `error: flexible array
   member ...` anywhere in the output. Confirmed before/after with `git
   stash`: without the fix, the same build stops at `disp.h:248`; with
   the fix, that class of error does not reappear anywhere in the
   `Opus/` tree.
2. `cmake --build --preset linux-winelib-debug --target WORD1`: **still
   does not complete, but for a wholly unrelated reason**: `wrc: Error:
   codepage 1252 not supported` while compiling `port/word1.rc` (missing
   codepage data in this VPS's `wrc`). Unrelated to FAM/union, not
   touched, was already like this.
3. `ctest --test-dir out/linux-winelib-debug` (full gating suite): the
   tests that depend on `opus_original_engine`/pure Winelib linking
   against `user32`/`gdi32`/`comdlg32`
   (`opus_x64_runtime_test`, `opus_original_sttb_test`,
   `opus_original_plc_test`, `opus_sdm_cab_test`,
   `opus_original_command_test`) **fail to link on this VPS for lack of
   `wine32:i386`/multiarch** (`it looks like wine32 is missing... apt-get
   install wine32:i386`), confirmed preexisting with `git stash`, same
   symptom with or without the FAM fix. Environment blocker, not a code
   one, out of scope for this task.
   The three that do depend on the core/shell boundary crossing
   winegcc/wineg++ **compile, link, and pass**:
   `opus_shell_memory_foreign_test`, `opus_shell_config_test`,
   `opus_shell_font_substitution_test`: 3/3 OK (no regression).
4. `src/core`'s own suite (`OPUS_CORE_BUILD_TESTS`, native compiler,
   `ctest --test-dir
   out/linux-winelib-debug/opus_core_build-prefix/src/opus_core_build-build`):
   **5/5 OK** (`opus_shell_font_substitution_test`,
   `opus_shell_font_metrics_test`, `opus_shell_font_metrics_fidelity_test`,
   `opus_shell_spine_test`, `opus_shell_config_test`). Expected:
   `src/core` never includes `Opus/wordtech/disp.h` or `Opus/rsb.h`, the
   change could not have affected it; run anyway for rigor, no
   regression.

**Pending, out of scope for this task:** the `wrc`/codepage 1252 blocker
(blocks all of `WORD1`) and the missing `wine32:i386` on this VPS (blocks
5 gating tests by link failure) are separate environment blockers,
unrelated to FAM/union, not investigated or touched here.

### Cross-check on Fedora 44 / GCC 16.1.1 (second machine): 2026-08-12

**Actual status: the `__GNUC__ < 15` guard behaves as expected, GCC 16
takes the original `#else` branch, zero FAM/union diagnostics across the
whole tree.** But the full build (`ninja -k 0`, reconfigured clean) and
the `src/core` suite **do not end up green** on this machine, for reasons
entirely unrelated to the guard, a real link gap just exposed
(`WORD1` -> `opus_shell_spine`) and two environment differences (Fedora's
font-path packaging convention, a Qt segfault). Both compiler outcomes
are documented side by side because they are the two real machines this
fix was tested on, not a projection:

| | Debian VPS (previous section) | Fedora 44 (this section) |
|---|---|---|
| Compiler | GCC 14.2.0 | **GCC 16.1.1** (`gcc (GCC) 16.1.1 20260515 (Red Hat 16.1.1-2)`) |
| Guard branch that compiles | `#if` (`rgdr[1]`/`rgbms[1]`/`rgzpp[1]`) | **`#else`** (`rgdr[]`/`rgbms[]`/`rgzpp[]`, original form, no workaround) |
| `opus_original_engine` (207 TUs) | compiles and links clean | **compiles and links clean** |
| Full `WORD1.exe` | blocked by `wrc`/codepage 1252 (unrelated) | **blocked by linking: missing `opus_shell_spine`** (unrelated, see below) |
| `src/core` suite (5 tests) | 5/5 OK | **1/5 OK** (4 failures unrelated to the guard, see below) |

#### Command and literal output: compiler version

```
$ gcc --version
gcc (GCC) 16.1.1 20260515 (Red Hat 16.1.1-2)
Copyright (C) 2026 Free Software Foundation, Inc.
Esto es software libre; vea el código para las condiciones de copia.  NO hay
garantía; ni siquiera para MERCANTIBILIDAD o IDONEIDAD PARA UN PROPÓSITO EN
PARTICULAR
```

#### Guard confirmed, exact context (`git grep -n -A4 -B6 '__GNUC__ < 15'`)

```
src/Opus/rsb.h-35-struct RSBI {
src/Opus/rsb.h-36-	union {
src/Opus/rsb.h-37-	struct	{
src/Opus/rsb.h:38:#if defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)
src/Opus/rsb.h-39-		struct BMS rgbms [1];  /* GCC <15 rejects FAM alone in struct (PR53548, r15-209) */
src/Opus/rsb.h-40-#else
src/Opus/rsb.h-41-		struct BMS rgbms [];
src/Opus/rsb.h-42-#endif
...
src/Opus/rsb.h:77:#if defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)
src/Opus/rsb.h-78-		struct ZPP rgzpp[1];  /* GCC <15 rejects FAM alone in struct (PR53548, r15-209) */
src/Opus/rsb.h-79-#else
src/Opus/rsb.h-80-		struct ZPP rgzpp[];
src/Opus/rsb.h-81-#endif
...
src/Opus/wordtech/disp.h-246-	union   {
src/Opus/wordtech/disp.h-247-		HQ	hqpldre;    /* when fExternal true */
src/Opus/wordtech/disp.h:248:#if defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)
src/Opus/wordtech/disp.h-249-		struct DR rgdr[1];  /* when fExternal false -- GCC <15 rejects FAM in union (PR53548, r15-209) */
src/Opus/wordtech/disp.h-250-#else
src/Opus/wordtech/disp.h-251-		struct DR rgdr[];   /* when fExternal false */
src/Opus/wordtech/disp.h-252-#endif
```

With `__GNUC__` = 16 on this machine, `(__GNUC__ < 15)` evaluates false
at all three sites: the `#else` branch compiles, that is, **the original
Microsoft form with no workaround at all**, exactly as intended.
Confirmed not just by reading the guard but by the compilation result
(next section): zero occurrences of `flexible array member` in any log,
across both complete build passes that were run.

#### Clean build, `ninja -k 0`: reconfiguration plus result

```
$ rm -rf out/linux-winelib-debug out/linux-winelib-release
$ cmake --preset linux-winelib-debug
-- The C compiler identification is GNU 16.1.1
-- The CXX compiler identification is GNU 16.1.1
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/winegcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/wineg++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Performing Test HAVE_STDATOMIC
-- Performing Test HAVE_STDATOMIC - Success
-- Found WrapAtomic: TRUE
-- Found OpenGL: /usr/lib64/libOpenGL.so
-- Found WrapOpenGL: TRUE
-- Found WrapVulkanHeaders: /usr/include
-- Configuring done (3.4s)
-- Generating done (0.1s)
-- Build files have been written to: /home/exia/word1/msword/out/linux-winelib-debug
$ ninja -k 0 -j4
```

(`-j4` instead of the default parallelism of 12: this machine has 7.7 GiB
of RAM, not 32+; with -j12 the 370-target build risked OOM. `-k 0` was
kept as requested, it does not limit retries, only limits parallelism.)

The build advanced through **370/370 attempted targets** (no early block
from `disp.h`/`rsb.h`, the error class that blocked the VPS does not even
appear) and ended with exactly **3 `FAILED:`**, none related to
FAM/union. Full log (120118 lines, 7.5 MB) saved at
`/tmp/claude-1000/-home-exia-word1-msword/ef583425-7fc7-457c-b877-9abeeaa77950/scratchpad/ninja-full-build.log`
from this session, not included in full here due to size; each full
`FAILED:` block, where all the signal is, is pasted below:

```
$ grep -c 'FAILED:' ninja-full-build.log
3
$ grep -c ' error:' ninja-full-build.log
3
$ grep -in 'flexible array' ninja-full-build.log ninja-targeted-retry.log
(no matches)
```

**Failure 1/3: `opus_original_plc_test.exe`, linking, unrelated to the guard:**

```
FAILED: [code=2] /home/exia/word1/msword/build/tests/Debug/opus_original_plc_test.exe 
: && /usr/bin/wineg++ -g -Wl,--dependency-file=CMakeFiles/opus_original_plc_test.dir/link.d CMakeFiles/opus_original_plc_test.dir/Opus/wordtech/clsplc.c.o CMakeFiles/opus_original_plc_test.dir/port/original/opus_asm_plc_adapters.cpp.o CMakeFiles/opus_original_plc_test.dir/port/original/opus_original_plc_test.c.o -o /home/exia/word1/msword/build/tests/Debug/opus_original_plc_test.exe  /home/exia/word1/msword/build/lib/Debug/libopus_x64_runtime.a  -luser32 && :
/usr/bin/ld.bfd: /home/exia/word1/msword/build/lib/Debug/libopus_x64_runtime.a(opus_win16_platform.cpp.o): en la función `GetPhysicalFontHandle':
/home/exia/word1/msword/src/port/original/opus_win16_platform.cpp:69:(.text+0x1aa): referencia a `GetCurrentObject' sin definir
/usr/bin/ld.bfd: [...] referencia a `GetBitmapDimensionEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetBitmapDimensionEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `GetViewportExtEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `GetViewportOrgEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetViewportExtEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetViewportOrgEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetWindowExtEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetWindowOrgEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `GetTextExtentPoint32A' sin definir
/usr/bin/ld.bfd: [...] referencia a `MoveToEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `ShellExecuteA' sin definir
/usr/bin/ld.bfd: [...].exe.so: el símbolo oculto «SetWindowExtEx» no está definido
/usr/bin/ld.bfd: falló el enlace final: valor incorrecto
collect2: error: ld devolvió el estado de salida 1
winegcc: /usr/bin/g++ failed
```

Cause: `src/CMakeLists.txt:1220`,
`target_link_libraries(opus_original_plc_test PRIVATE
opus_original_c_dialect opus_x64_runtime user32)`, does not list `gdi32`
or `shell32`, and `opus_win16_platform.cpp` (inside `opus_x64_runtime`)
calls the `*Ex` GDI variants (`gdi32`) and `ShellExecuteA` (`shell32`).
Not a new gap from this session: there is no recent change to that
target, it is preexisting, only visible now because on the VPS linking
was never even attempted (blocked earlier by `disp.h`, and then by the
missing `wine32:i386`).

**Failure 2/3: `opus_x64_runtime_test.exe`, linking, unrelated to the guard:**

```
FAILED: [code=2] /home/exia/word1/msword/build/tests/Debug/opus_x64_runtime_test.exe 
: && /usr/bin/wineg++ -g -Wl,--dependency-file=CMakeFiles/opus_x64_runtime_test.dir/link.d CMakeFiles/opus_x64_runtime_test.dir/port/original/opus_x64_runtime_test.cpp.o CMakeFiles/opus_x64_runtime_test.dir/port/original/opus_x64_layout_test_fixture.c.o -o /home/exia/word1/msword/build/tests/Debug/opus_x64_runtime_test.exe  /home/exia/word1/msword/build/lib/Debug/libopus_x64_runtime.a  -luser32  -lgdi32 && :
/usr/bin/ld.bfd: [...]opus_sdm_runtime.cpp.o: en la función `(anonymous namespace)::commit_ribbon_list_selection(...)':
/home/exia/word1/msword/src/port/original/opus_sdm_runtime.cpp:1779:(.text+0x8a99): referencia a `OpusX64TraceRibbon' sin definir
/usr/bin/ld.bfd: [...] (más 4 sitios, mismo símbolo)
/usr/bin/ld.bfd: [...] en la función `run_word95_common_file_dialog(...)':
/home/exia/word1/msword/src/port/original/opus_sdm_runtime.cpp:2184:(.text+0xabc7): referencia a `GetOpenFileNameA' sin definir
/usr/bin/ld.bfd: [...]:2184:(.text+0xabe3): referencia a `GetSaveFileNameA' sin definir
/usr/bin/ld.bfd: [...]:2186:(.text+0xabfd): referencia a `CommDlgExtendedError' sin definir
/usr/bin/ld.bfd: [...].exe.so: el símbolo oculto «GetSaveFileNameA» no está definido
/usr/bin/ld.bfd: falló el enlace final: valor incorrecto
collect2: error: ld devolvió el estado de salida 1
winegcc: /usr/bin/g++ failed
```

Two distinct causes in the same failure: (a) `GetOpenFileNameA`/
`GetSaveFileNameA`/`CommDlgExtendedError` belong to `comdlg32`, absent
from `target_link_libraries` (`src/CMakeLists.txt:1447`, lists only
`user32 gdi32`); (b) `OpusX64TraceRibbon`, `extern "C" void
OpusX64TraceRibbon(...)` is **declared** in `opus_sdm_runtime.cpp:24`
and **called** from there, but is only **defined** in
`port/original/opus_original_startup_probe.cpp:387`, which is source
exclusive to the `WORD1` executable, not part of
`libopus_x64_runtime.a`. Any binary that links `opus_x64_runtime`
without also including the probe (like this test) ends up with an
unresolved reference by the current tree's design, not by a transient
error.

**Failure 3/3: `WORD1.exe`, linking, unrelated to the guard, blocks all of Phase 4 on this machine:**

```
FAILED: [code=2] /home/exia/word1/msword/bin/WORD1.exe 
: && /usr/bin/wineg++ -g -mwindows -municode -Wl,--dependency-file=CMakeFiles/WORD1.dir/link.d generated/original/word1.spec generated/original/word1.res CMakeFiles/WORD1.dir/port/original/opus_original_startup_probe.cpp.o -o /home/exia/word1/msword/bin/WORD1.exe  /home/exia/word1/msword/build/lib/Debug/libopus_original_engine.a  /home/exia/word1/msword/build/lib/Debug/libopus_x64_runtime.a  -luser32  -ldbghelp  core/lib/libopus_shell_config.a  core/lib/libopus_shell_memory.a  core/lib/libopus_shell_font_metrics.a  /usr/lib64/libQt6Gui.so.6.11.1  /usr/lib64/libGLX.so  /usr/lib64/libOpenGL.so  /usr/lib64/libQt6Core.so.6.11.1  core/lib/libopus_shell_font_substitution.a && :
/usr/bin/ld.bfd: /home/exia/word1/msword/build/lib/Debug/libopus_original_engine.a(error.c.o): en la función `ErrorEidStartup':
/home/exia/word1/msword/src/Opus/wordtech/error.c:1630:(.text+0xc4f): referencia a `OpusShellReportError' sin definir
collect2: error: ld devolvió el estado de salida 1
winegcc: /usr/bin/g++ failed
ninja: build stopped: cannot make progress due to previous errors.
```

Cause, identified with `git show`: commit `ea5f908` (B4.4, "connects
`error.c:1618` to the `OpusShellReportError` contract") added the real
call in `error.c` but **did not touch `src/CMakeLists.txt`**, the
`WORD1` target (lines 1298-1309) never received `opus_shell_spine` in
its `target_link_libraries`, unlike `opus_shell_config`,
`opus_shell_memory`, and `opus_shell_font_metrics`, which are present
(lines 1306-1308). §B4.4 itself already warned about the missing
end-to-end verification ("not triggered or observed running this
session"), attributing it at the time to the `disp.h` block; on Fedora
`disp.h` no longer blocks, and this is the next real blocker in the
chain, not a regression from this task. Explicitly retried with `ninja
-k 0 opus_original_plc_test opus_x64_runtime_test WORD1` after the full
build: same three failures, identical byte for byte in the link message
(log at `.../scratchpad/ninja-targeted.log`).

**Neither `src/CMakeLists.txt` nor any code file was touched in this
task**, the three failures are left diagnosed, not fixed, pending a
decision (it directly affects Task 2 of this same session, which needs a
linkable `WORD1.exe.so`).

#### `ctest` for `src/core`: 1/5 OK, 4 failures unrelated to the guard

```
$ cd out/linux-winelib-debug/opus_core_build-prefix/src/opus_core_build-build
$ ctest --output-on-failure
Test project /home/exia/word1/msword/out/linux-winelib-debug/opus_core_build-prefix/src/opus_core_build-build
    Start 1: opus_shell_font_substitution_test
1/5 Test #1: opus_shell_font_substitution_test .......***Failed    0.00 sec
FALLÓ: Tms Rmn: archivo de fuente '/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf' debe poder abrirse -- ¿faltan las fuentes Liberation en esta máquina?
FALLÓ: Symbol: archivo de fuente '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf' debe poder abrirse -- ¿faltan las fuentes Liberation en esta máquina?
FALLÓ: Helv: archivo de fuente '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf' debe poder abrirse -- ¿faltan las fuentes Liberation en esta máquina?
FALLÓ: Courier: archivo de fuente '/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf' debe poder abrirse -- ¿faltan las fuentes Liberation en esta máquina?
OpusShellFontSubstitution_test: 4 fallo(s) de 16 verificaciones.

    Start 2: opus_shell_font_metrics_test
2/5 Test #2: opus_shell_font_metrics_test ............***Failed    0.10 sec
[21 fallo(s), misma causa raíz: no puede abrir los .ttf en la ruta hardcodeada]

    Start 3: opus_shell_font_metrics_fidelity_test
3/5 Test #3: opus_shell_font_metrics_fidelity_test ...***Failed    0.11 sec
[56 fallo(s), misma causa raíz]

    Start 4: opus_shell_spine_test
4/5 Test #4: opus_shell_spine_test ...................***Exception: SegFault  0.58 sec

    Start 5: opus_shell_config_test
5/5 Test #5: opus_shell_config_test ..................   Passed    0.01 sec

20% tests passed, 4 tests failed out of 5
```

**The 3 font failures are a single finding, not three:** the hardcoded
paths in `src/core/src/OpusShellFontSubstitution.cpp:31-34` and their
mirror in `OpusShellFontSubstitution_test.cpp:35-38` assume the
Debian/Ubuntu convention (`/usr/share/fonts/truetype/liberation/...`).
**The fonts are indeed installed on this machine**, confirmed, not
assumed:

```
$ rpm -q liberation-serif-fonts liberation-sans-fonts liberation-mono-fonts
liberation-serif-fonts-2.1.5-15.fc44.noarch
liberation-sans-fonts-2.1.5-15.fc44.noarch
liberation-mono-fonts-2.1.5-15.fc44.noarch
$ fc-match "Liberation Serif"
LiberationSerif-Regular.ttf: "Liberation Serif" "Regular"
$ find / -iname 'LiberationSerif-Regular.ttf' 2>/dev/null
/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Regular.ttf
[...two more paths inside Flatpak runtimes, irrelevant...]
```

Fedora packages each family in its own directory
(`liberation-serif-fonts/`, not `truetype/liberation/`). It is a
packaging-convention difference between distributions, not a missing
font, the same portability bug §B2.6/B2.7 of this document already
solves for the *era name -> family substitution* (via `fc-match`), but
that **was not applied to the physical file path**: that second step is
still hardcoded to an absolute path instead of being resolved via
`fc-match -f '%{file}'` the way the §B2.6 probe does. It is a real
cross-distro portability bug, discovered by this cross-check, it was not
characterized before because the VPS is Debian and the path does exist
there. Out of scope for this task (it only asks to verify the FAM/union
guard); noted for a separate porting task, not fixed here.

**`opus_shell_spine_test` segfaults**, cause not diagnosed in this task
(this machine has neither `gdb` nor `valgrind` installed, see Task 2 of
this same session for the same tooling gap). Captured by
`systemd-coredump`:

```
$ coredumpctl list | tail -1
Tue 2026-08-11 22:49:36 -04 27390 1000 1000 SIGSEGV present  .../core/bin/opus_shell_spine_test  1.7M
```

Without `gdb`, `coredumpctl info` produces no usable symbolic backtrace
(only a list of loaded modules). Not investigated further: it is a
second finding, not asked for by this task, and shares the same tooling
blocker as Task 2. Reproduced twice (`QT_QPA_PLATFORM=offscreen`
included) with the same result, so it is not an intermittent failure
related to this machine's Wayland session.

#### Conclusion of this section

**The `__GNUC__ < 15` guard fix is verified on the two real machines it
was tested on, with the two compiler versions explicitly documented: GCC
14.2.0 (Debian VPS, takes the `#if` branch, with the workaround) and GCC
16.1.1 (Fedora 44, this section, takes the `#else` branch, without the
workaround), both compile `opus_original_engine` (207 TUs) clean, zero
FAM/union diagnostics in either case.** That is the specific claim this
task asked to verify, and it holds.

**What does NOT hold is "the build/tests are green on Fedora"**, they
are not, for reasons entirely unrelated to the guard: a real link gap
(`WORD1`/`opus_shell_spine`, now exposed since `disp.h` no longer blocks
before reaching it) and two environment differences (font path
convention, an undiagnosed Qt segfault for lack of `gdb`). Documented
with the same rigor as the VPS finding: every claim above has its
command and its literal output, nothing is taken on faith without
evidence.
</content>
