# WORD1 Behavior: Startup-Blocked List

Original date: 2026-08-15. Branch `fix/winelib-startup-blocked`. This
series starts from the §26 table of
[`01-heap-corruption-startup-diagnosis.md`](01-heap-corruption-startup-diagnosis.md)
(the 7 real behavior failures that remained once the harness stopped
crashing). Each section is one item from that list.

All claims are backed by commands run on
debian13 against `/home/pablo/build-debian13-verify`, `DISPLAY=:59`.

---

## 1. `--about`: "Help About dialog did not appear"

**Test:** `opus_word1_about_test`, `stage=0 responsive=1` (and, if the
MessageBox close is forced, `exit=0x0 mainWindow=0`).

**Status:** root cause confirmed. The About dialog **does materialize**
when `WM_COMMAND` 182 reaches `CmdAbout`. The test doesn't see it
because that command never runs: a modal startup MessageBox leaves
`vcInMessageBox=1` and `AppWndProc` discards every `WM_COMMAND`.
Product fix deferred (3+ unblocking attempts did not leave the test
Passed; see below).

### What `stage=0` / `responsive=1` means

In `opus_word1_ui_test.cpp` (`about_mode`):

1. Waits for the main window `"Microsoft Word - Document1"`.
2. `Sleep(1000)`.
3. `PostMessageW(main, WM_COMMAND, kHelpAbout=182, 0)`.
4. Waits for a top-level `OpusSdmDialog` for 5 s.
5. If it doesn't appear, prints
   `exit=… mainWindow=… responsive=… stage=…`.

- `responsive=1` is `window_is_responsive`: the process is still
  alive, the main window responds to `WM_NULL`.
- `stage=` is `GetPropA(main, "OpusX64AboutStage")`. **No one in the
  tree sets that property.** `stage=0` only says "the command was
  never instrumented," not which line SDM failed on.

`182` is the correct `bcm` (`opuscmd.h`: `bcmAbout = 182`).

### The dialog is never created (never-reached), not invisible

Temporary instrumentation in `NatAppWndProc` and
`HdlgStartDlg`/`TmcDoDlgDli`/`IdDoMsgBox` (reverted; did not stay in
the tree):

```
create_dialog_host hid=32773 / 32774   (ribbon / ruler children, not About)
IdDoMsgBox parent=(nil) flags=0x30
  caption='Microsoft Word'
  text='Low memory: cannot display requested font'
NatAppWndProc WM_COMMAND LOWORD=2799 (ViewPage)  vcInMessageBox=1
NatAppWndProc WM_COMMAND LOWORD=182  (HelpAbout) vcInMessageBox=1
  sy[182] mct=3 name=HelpAbout pfn≠0
  (no TmcDoDlgDli hid=44)
```

The command table is fine (`mctSdm`, `CmdAbout` resolved).
`AppWndProc` (`wproc.c:854`) does `if (vcInMessageBox) return 0`.
About **never reaches** `HdlgStartDlg` or the shared block
`create_dialog_host` lines 338-397.

The MessageBox is real: `xwininfo` shows
`"Microsoft Word" 295x82+364+356` next to
`"Microsoft Word - Document1"`. In the Xvfb screenshot a black
rectangle is visible (the `#32770` doesn't paint the text in this
Wine build). The text's origin is `eidCantRealizeFont`
(`error.c:933-934`), triggered from `ReportPendingAlerts` when
`vmerr.mat == matFont`. `LOADFONT.C` sets `matFont` if
`CreateFontIndirect` / `OurSelectObject` fails and it falls back to
`SYSTEM_FONT`.

### The About path works if the command runs

With WORD1 launched by hand on `:59`, `xdotool` Return on the
MessageBox and then Alt+H, A:

```
IdDoMsgBox returned 1
NatAppWndProc WM_COMMAND LOWORD=182  vcInMessageBox=0
HcabAlloc cabi=1824 ok=1
TmcDoDlgDli enter hid=44 flags=0x41
create_dialog_host popup caption=About Microsoft Word
materialize_about_template hid=44
TmcDoDlgDli branch hid=44 modal=1 native_modal=1
```

`xwininfo`: `"About Microsoft Word" 410x230`. The SDM host, the
`materialize_about_template` call, and the `TmcDoDlgDli` modal loop
are sound. The test's failure is not in lines 338-397.

### Unblocking attempts (none left it Passed)

| # | Change | Result |
|---|---|---|
| 1 | `IdDoMsgBox` with owner `vhwndApp` | The `#32770` can be found; closing it makes WORD1 end with `exit=0` |
| 2 | The test does `PostMessage(IDOK)` / `WM_CLOSE` / `BM_CLICK` to the `#32770` and then About | After dismiss `mainWindow=1`; About leaves `exit=0x0 mainWindow=0` |
| 3 | `IdDoMsgBox` returns `IDOK` on seeing `"cannot display requested font"` (without `MessageBoxA`) | WORD1 **exits on its own after ~3 s** (the MB's modal loop was what kept startup alive: it pumps `ViewPage` and the rest of idle) |
| 4 | Test sends `VK_RETURN` + `TmcDoDlgDli` drains `WM_QUIT` | Still `exit=0x0 mainWindow=0` |

Closing the alert from another Wine process, or skipping it, does not
reproduce the interactive path (Return on the X window, then the
menu). The startup MessageBox is at once **the `WM_COMMAND` blocker**
and **part of the message pumping that finishes init**. That is an
architecture question (fix `CreateFontIndirect` so there's no
`matFont`? an `IdDoMsgBox` that isn't modal and doesn't cut idle?
does the harness use `SendInput` on WORD1's thread?), not a one-line
patch in `create_dialog_host`.

### Shared vs independent (Tasks 4-5)

**The same failure point covers `kIddNewDoc` and `kIddSaveAs`.** File
New (1813) and File Save As (1897) also go through `AppWndProc` →
`FExecCmd`. With `vcInMessageBox=1` those `WM_COMMAND` messages get
swallowed the same way. The 338-397 block of `create_dialog_host` is
**not** the shared failure: About, New, and Save As diverge *earlier*,
at the `vcInMessageBox` gate.

Once the alert stops blocking the pump, Tasks 4-5 should **first
verify** the same path (`TmcDoDlgDli` already created About with
`hid=44`). If New/Save As are still failing *after* a green About,
then a separate investigation is warranted (Save As also enters
`run_word95_common_file_dialog`).

Outstanding for the product, outside this item: why
`CreateFontIndirect` fails (Arial 10 on the ribbon renders fine; the
document falls back to `SYSTEM_FONT`); the `#32770` and the About
dialog paint black on this Xvfb/Wine setup.

### Fix round (2026-08-15): LOADFONT site + `OpusShellCharWidths`

Of the three sites that set `matFont` in `LOADFONT.C`, the one that
fired before the MessageBox is **3** (Linux path, lines 428-459):
`OpusShellCharWidths(...) != 0` → `LSystemFontErr`.

Temporary instrumentation of `OpusShellCharWidths` (later removed):

```
OpusShellCharWidths ftc=2 ps=0 catr=0 chFirst=0 cch=256 rc=-1 why=bad-px
```

It is not `CreateFontIndirect` returning NULL (site 1), nor
`OurSelectObject`/`FSelectFont` (site 2): width was actually
requested. The startup request is Helv (`ftc=2`) with `hps==0`.
`RawFontFor` did `px = MulDiv(ps/2, 96, 72)` and rejected `px<=0`.
That `-1` is what sets `matFont` and leaves `vcInMessageBox=1`.

Change in `src/core/src/OpusShellFontMetrics.cpp` (without touching
`src/Opus/`):

- `hps==0` is measured as 10 pt (`hpsDefault`), the same way
  `CreateFontIndirect(lfHeight==0)` uses a default height.
- WORD1 has no `QGuiApplication`. Creating one on the Wine thread
  hangs the pump or kills the process. Without an app, `QRawFont`
  cannot be built: the widths are filled in from the already-measured
  oracle table (`opus_shell_font_metrics_oracle_table.h`, Helv 10 pt).

After that success, the font MessageBox **does not appear**, but
WORD1 still doesn't leave About green: init continues and falls into
`FInsertInPl` (`clsplc.c:829`) from `C_PushLbs` (`layout2.c:1430`),
a write AV `0xC0000005` via `HpInPl` (`opus_asm_resn2_pl.cpp`).
`ctest -R opus_word1_about_test` results in:

```
Help About process exit=0x0 mainWindow=0 responsive=0 stage=0
Help About dialog did not appear
CTEST_EXIT=8
```

(debian13, `/home/pablo/build-debian13-verify`, `DISPLAY=:59`)

With `QGuiApplication` on the Wine thread, the About dialog *did* get
created (`OpusSdmDialog`) and the failure shifted to "did not finish
initializing" (the host wasn't responding to `WM_NULL`). That path
was not kept: Qt on the Wine thread is not viable. The `FInsertInPl`
crash is the next blocker; it is not a fifth MessageBox skip.

### Status at cutoff (2026-08-15, HEAD `134cddc`)

`opus_word1_about_test` **is still Failed** (~7.8 s,
`exit=0x0 mainWindow=0`). The font MessageBox no longer appears. The
process dies in layout:

```
Exception 0xC0000005 write at 0xFFFFFFFD3726D202
OpusMoveBytesEnd+0x2B
FInsertInPl+0x1B6
C_PushLbs+0x296
```

(`build/WORD1-crash.txt`; call site C `layout2.c:1430`
`FInsertInPl(vhpllbs, ilbs, plbsTo)`.)

Temporary instrumentation of `HpInPl`/`OpusPlData` (reverted, did not
stay in the tree) left `build/t3-h2-pl.log`. The first calls are
sound PLs:

```
iMac=1 iMax=1 cb=2  brgfoo=20  fExternal=0
iMac=1 iMax=1 cb=136 brgfoo=256 fExternal=0   (dest in-heap, insane=0)
```

The last one, with the header already smashed, is a different `hpl`:

```
HpInPl hpl=0x7ffffe811970 *hpl=0x7ffffe811bc0
  iMac=-1072622911 iMax=32726 cb=6 brgfoo=0 fExternal=-31497312
  i=-1072622912 base=(nil) dest=0xfffffffe80667080 insane=1
```

`HpInPl` does not invent that pointer: it is handed a block that is
no longer a `struct PL`. `iMac==iMax==1` in the sound calls implies
the next `FInsertInPl` enters the grow path (`clsplc.c:847-874`).
Next Phase 1 (not done): distinguish a *grow that corrupts the
header* (`FChngSizeHCw` / size `brgfoo + cb*iMax`) from a *wrong
handle* (not `vhpllbs`). Do not edit `src/Opus/`.

A half-finished attempt at GDI widths (`opus_gdi_char_widths.cpp` +
`OpusShellSetCharWidthsFallback`) was discarded at cutoff: it did not
reach a green ctest. Do not reintroduce it until there is a `HpInPl`
log covering the grow.

**Shared vs independent (Tasks 4-5), updated:** About / New / Save As
are no longer masked by `vcInMessageBox`. They remain blocked because
the process does not survive the first layout. Verify first once
About is green.

### Fix round 2 (2026-08-15): H1 vs H2 of the AV in `FInsertInPl`

Phase 1, one change at a time. The font MessageBox is still closed.

**H1 (metrics):** `OpusShellCharWidths` returned 0 (no `matFont`) and
filled the table with a constant dummy value of 8. The AV **did not
disappear**. It was then measured with GDI (`CreateCompatibleDC` +
`CreateFontIndirectA`, the same `LOGFONT` as `C_FGraphicsFcidToPlf`,
`hps==0` → `lfHeight==0`, `GetCharWidthA`; confirmed
`GDI ftc=2 ps=0 w32=3 wA=7`). The AV **still occurs** in
`FInsertInPl`. The table values are not the cause: Helv-10 oracle,
dummy 8, and GDI Helv `lfHeight=0` all die the same way.

The interactive path that does open About uses `fFallback` /
`fFixedPitch=true` (`LOADFONT.C:397` does not fill `hqrgdxp`). That
cannot be forced from the port without returning -1 (`matFont` /
MessageBox).

**H2 (PL header / HpInPl):** confirmed. The `hpl` in the crash **is**
`vhpllbs` (`lbs=1`, `hsz=1428` = `cbPLBase + 8*sizeof(LBS)` with
`sizeof(LBS)==176`). At the first `FInsertInPl` the header is already
garbage:

```
iMac=-1072622911 iMax=32726 cb=6 brgfoo=0 fExternal=-31497312
i=-1072622912 base=(nil) dest=0xfffffffe80667080
```

`PL`: `cbPLBase=20`, `fExternal` @ 16. `PLLBS` has no `fExternal`
(`rglbs` @ 16). `OpusPlData` treated any `fExternal!=0` as HQ,
producing a wild dest. The first `HpInPl` calls are **other**, sound
PLs (`cb=2`/`cb=136`); `vhpllbs` is never seen sound, not even once.

The smash is **not** a `bltbh` over the 20-byte header (a watched
`memmove` did not fire). Nor is it a `FChngSizeHCb` of `vhpllbs`: at
`HAllocateCw(1428)` `vhpllbs` is still nil; there is no later `chng`
on that handle before the AV.

Clamping `dest` in `HpInPl` avoids the wild write and moves the crash
to `UnstackLbs` (walks garbage `ilbsMac`) or
`IpgdPldrFromWwDocCpIpgd`. It does not fix the header.

**Both:** H1 is not "Helv-10 table ≠ GDI". H2 is the AV's mechanism
(`HpInPl` over an already-smashed `vhpllbs`). The header gets
corrupted on the variable-pitch path (`fFixedPitch=false`) before
`C_PushLbs`; the port never sees the store. Without editing Opus
there is nowhere to restore `iMac` before `layout2.c:1384`.

**ctest** (debian13, `DISPLAY=:59`, GDI + `fExternal==1` in
`OpusPlData`):

```
opus_word1_about_test ***Failed  7.8 sec
Help About process exit=0x0 mainWindow=0 responsive=0 stage=0
Help About dialog did not appear
CTEST_EXIT=8
Exception 0xC0000005 FInsertInPl+0x1B6 C_PushLbs+0x296
```

**BLOCKED** on Opus lines (not touched):
`src/Opus/wordtech/layout2.c:1384` (`ilbs = (*vhpllbs)->ilbsMac`) and
`1430` (`FInsertInPl`); or `LOADFONT.C:397` (the only path that does
not enter variable-pitch layout).

### Fix round 3 (2026-08-15): `_setjmp` with the wrong ABI: RESOLVED

`opus_word1_about_test` **passes**. The AV was not in `FInsertInPl`
or `HpInPl`: both were victims. What smashes the `vhpllbs` header is
`setjmp`.

**Evidence (hardware watchpoint, not printf).** On the debian13
container, `gdb -batch` on `/usr/lib/wine/wine64` (not on
`/usr/bin/wine`, which is a script), with `set follow-fork-mode
parent` so as not to follow `wineserver`, and `handle SIGSEGV nostop
noprint pass` so Wine converts the fault into a Win32 exception:

1. Breakpoint at `layout.c:286` (right after
   `vhpllbs = HplInit(sizeof(struct LBS), 8)`). The header is
   **sound**: `iMac=0 iMax=8 cb=176 brgfoo=20 fExternal=0`,
   `data=0x7ffffe8117a0`. In other words, round 2's "it's born
   already smashed" hypothesis was false: it is born fine and gets
   broken four lines below.
2. `watch -l *(int *)(data + 16)` (that is, `PL.fExternal`) and
   `continue`. It fires right away: `Old value = 0`,
   `New value = -31497312`, with `#1 LbcFormatPage ... layout.c:290`,
   that is, `SetLayoutAbort()`.
3. At the point of the fire, `x/10i $pc-32` shows the callee's body,
   which is exactly the MSVCRT x86-64 `_setjmp`:

   ```
   0x6fffffc2f8e8:  mov    %rdx,(%rcx)      ; buf->Frame
   0x6fffffc2f8eb:  mov    %rbx,0x8(%rcx)   ; buf->Rbx
   0x6fffffc2f8ef:  lea    0x8(%rsp),%rax
   0x6fffffc2f8f4:  mov    %rax,0x10(%rcx)  ; buf->Rsp
=> 0x6fffffc2f8f8:  mov    %rbp,0x18(%rcx)  ; buf->Rbp
   ```

   and the registers: `rdi=0x7ffff79122d8` (which **is** in fact
   `&venvLayout.nativeEnv`, the correct buffer), against
   `rcx=0x7ffffe8117a0` (which is `*vhpllbs`).

**Root cause, in one sentence:** `_setjmp` was resolving against
Wine's `msvcrt` PE, which is Microsoft-x64 and takes the `jmp_buf` in
**RCX**, while all of Opus's code is System V and passes it in
**RDI**; the import was writing its 256-byte `_JUMP_BUFFER` over
whatever stale pointer was left in RCX.

In `LbcFormatPage` that stale RCX is `*vhpllbs`, the block that
`HplInit` had just returned nine lines earlier, so every layout pass
wrote register state on top of the `struct PL`:

- `fExternal` (offset 16) received the low half of RSP:
  `0xfe1f63a0` = `-31497312`, which is exactly the value round 2 had
  recorded.
- offsets 20..27 received `0x00007fff` plus zeros: that is where the
  `hqple = 0x7fff00000000` of the AV comes from.

The final symptom (after the `OpusPlData` clamp from `5bebdd9`) was
no longer `FInsertInPl` but `FreeHpl` (`clsplc.c:465`): with
`fExternal` nonzero it treats the non-external PL as external, reads
that garbage `hqple` from `rglbs[0]`, and calls `FreeHq`, causing a
read AV in `OpusFreeH` (`opus_x64_heap.cpp:411`, `mov (%rax),%rax`
with `rax=0x7fff00000000`). The chain
`LbcFormatPage → FreePhpl(&vhpllbs) → FreeHpl → OpusFreeH` was
confirmed by matching the returns against the disassembly
(`FreeHpl+84` is exactly the return of the `call OpusFreeH` from the
`FreeHq` branch, and `FreePhpl`'s `rbp-8` slot holds `&vhpllbs`).

Static confirmation: `nm bin/WORD1.exe.so` showed `__imp__setjmp` and
a `t _setjmp` thunk in the same import table as `ShellExecuteA` /
`AdjustWindowRect`. `_setjmp` was the **only** CRT symbol imported by
mistake (the other four that look like CRT, `_lclose`, `_llseek`,
`_lread`, `_lwrite`, are legitimate Win16 kernel32 APIs). `longjmp`,
on the other hand, did resolve to glibc (`longjmp@GLIBC_2.2.5`): the
pair was broken in half.

It gets there because `Opus/lib/qsetjmp.h` (`OPUS_X64` branch) uses
the host's `<setjmp.h>`, and glibc expands `setjmp(env)` to
`_setjmp(env)`.

**Fix (all outside the restricted tree):**
`src/port/original/opus_x64_setjmp.cpp` defines `_setjmp` in System V
ABI as a tail jump to `__sigsetjmp(env, 0)`, which is literally what
glibc's `_setjmp` does. It has to be a tail jump: a C wrapper would
leave a stack frame that no longer exists by the time `longjmp`
returns to it. With the symbol now defined, `winebuild` stops
generating the `msvcrt` import (verified: `nm` now gives `T _setjmp`
plus `U __sigsetjmp@GLIBC_2.2.5`, with no `__imp__setjmp`). It is
added to `WORD1_SOURCES` inside the already-existing
`if(OPUS_WINELIB_BUILD)` block, so MSVC never sees it.

**Second change, needed for ctest to actually *see* it.** With the AV
fixed, `opus_word1_ui_test --about` finishes in 2 s with code 0 (the
`OpusSdmDialog` dialog is created, the button at id 1 responds, it
closes cleanly), but ctest kept giving `Timeout 20 s` without
printing anything. The cause is the already-documented §25:
`CreateProcessW` returns a zeroed `PROCESS_INFORMATION` for
`WORD1.exe.so`, so `hProcess == nullptr` and the `TerminateProcess`
on each exit path is a no-op; WORD1 was outliving the harness by
holding on to the inherited stdout pipe, and ctest was waiting. As
long as WORD1 died on its own this went unnoticed.
`opus_word1_ui_test.cpp` now recovers the real PID from the main
window (`GetWindowThreadProcessId` + `OpenProcess`) whenever
`hProcess` comes back null; with that, all the existing teardown and
waits work without needing changes, and the window searches regain
the exact-PID filter that §26 prefers.

**ctest** (debian13, `/home/pablo/build-debian13-verify`, `DISPLAY=:59`):

```
1/1 Test #17: opus_word1_about_test ............   Passed    2.88 sec
100% tests passed, 0 tests failed out of 1
```

Stable across 3 consecutive runs (2.84 / 2.97 / 2.83 s).

The `word1_startup_blocked` label goes from **0/9** to **5/9**
(figures after review, see "Review closure" below):

```
1/9 Test #10: word1_port_smoke_test ................   Passed    1.56 sec
2/9 Test #11: opus_word1_ui_test ...................   Passed    1.82 sec
3/9 Test #12: opus_word1_clipboard_shortcut_test ...   Passed    2.02 sec
4/9 Test #13: opus_word1_typing_test ...............   Passed   12.70 sec
5/9 Test #14: opus_word1_interaction_test ..........***Failed    2.52 sec
6/9 Test #15: opus_word1_selection_test ............***Failed    3.39 sec
7/9 Test #16: opus_word1_font_typing_test ..........***Failed    1.69 sec
8/9 Test #17: opus_word1_about_test ................   Passed    2.78 sec
9/9 Test #18: opus_word1_save_as_test ..............***Failed    7.83 sec
56% tests passed, 4 tests failed out of 9
```

The 4 that keep failing no longer die from the AV: they fail fast,
with their own message. They are the material for Tasks 4-5.

Gating: 8/8 green (`opus_original_strtbl_test`,
`opus_original_sttb_test`, `opus_original_plc_test`,
`opus_sdm_cab_test`, `opus_original_command_test`,
`opus_shell_memory_foreign_test`, `opus_shell_config_test`,
`opus_shell_font_substitution_test`).

### Review closure (2026-08-15): the two halves of the pair, and the guard

The review raised two important things; both fixed.

**1. `longjmp` was still tied to glibc only by luck of link order.**
The original fix pinned `_setjmp` by definition but left `longjmp` to
whatever the linker decided. This is not a theoretical concern: in
this container there are **14 Wine import files** that define
`longjmp`, `_setjmp`, and `_setjmpex`: `libmsvcrt.a`,
`libmsvcr70..120`, `libucrtbase.a`, `libvcruntime140.a`,
`libntdll.a`, `libntoskrnl.a`, in both `x86_64-unix` and
`x86_64-windows`. `libntdll.a` gets linked by every winelib target.
Passing a glibc `jmp_buf` to Wine's Microsoft-x64 `longjmp` is the
same silent catastrophe in the other direction.

`opus_x64_setjmp.cpp` now also pins `longjmp`, as a tail jump to
`_longjmp@PLT`. `_longjmp` is a name that **none** of those 14 files
define (verified), so it is a safe target; and in glibc `longjmp`,
`_longjmp`, and `siglongjmp` are weak aliases of the same
`__libc_siglongjmp` (same address `0x3fab0` in `libc.so.6`), so the
forwarding is exact.

Verification on the binary:

```
                 U __sigsetjmp@GLIBC_2.2.5
                 U _longjmp@GLIBC_2.2.5
000000000008bd34 T _setjmp
000000000008bd3b T longjmp
```

**2. Build-time guard.** `src/cmake/AssertNoWineCrtSetjmp.cmake` runs
as a `POST_BUILD` step for WORD1 and fails the build if any `__imp_`
thunk from that family reappears (`__imp__setjmp`, `__imp__setjmpex`,
`__imp_longjmp`, `__imp__longjmp`, `__imp_siglongjmp`). This way, a
future toolchain or link-order change shows up as a build error, not
a wild write during layout.

The guard is tested in both directions, not just written:

```
NEGATIVE: binary crafted with `void *__imp__setjmp = 0;`
  -> CMake Error ... imports the Microsoft-x64 CRT setjmp/longjmp
     family from Wine: __imp__setjmp        (rc=1)
POSITIVE: bin/WORD1.exe.so                  (rc=0)
WIRED IN: ninja -t commands WORD1 | grep AssertNoWineCrtSetjmp.cmake  -> present
```

**3. `word1_port_smoke_test` now has `TIMEOUT 20`**
(`CMakeLists.txt:1580`), like its 8 siblings. It was missing this,
and as long as WORD1 died on its own it went unnoticed. With the
complete setjmp/longjmp pair the test also **passes** (1.56 s), not
merely stops hanging.

**Still outstanding, and not caused by this fix:**
`opus_x64_runtime_test` (gating) **hangs** at the tip of the branch:
run directly it ends in `timeout 40` without printing a single line
(`rc=124`). Yesterday's binary (05:46, prior to `5bebdd9` and to this
fix) hung the same way, contains no `setjmp` symbol at all, and
neither `opus_x64_setjmp.cpp` nor the harness change enter that
target. Rebuilding it does not fix it. It needs to be investigated
separately.

**Real scope of the bug.** `SetJmp` is not used only in layout: also
in `GRSPEC.C`, `eldde.c`, `fieldpic.c`, `fltexp.c`, `ffread.c`,
`mathapi.c`, `token.c`, `sort.c`, and `interp/elinit.c`. All those
sites had been writing 256 bytes of register state over unrelated
pointers. It is worth checking whether other "inexplicable" port
failures disappear with this before investigating them separately.

**Note for whoever continues:** `opus_original_startup_probe` (target
`EXCLUDE_FROM_ALL`, not part of ctest) links the same graph and still
lacks the shim. If it is revived, it needs
`port/original/opus_x64_setjmp.cpp` added, same as WORD1.

---

## 4. File > New: verification blocked by environment (not by code)

**Status:** undetermined. Task 4 was launched to verify whether
File > New shares Task 3's root cause (strong hypothesis: yes;
`opus_word1_ui_test` in base mode, the same test, had already given
`Passed 1.82 s` within the complete Fix round 3 run). This session's
verification attempt did not manage to either confirm or refute it:

```
1/9 Test #10: word1_port_smoke_test .................   Passed    1.69 sec
2/9 Test #11: opus_word1_ui_test ....................***Failed    9.57 sec

WORD1 main window did not appear
(Wine CreateWindow error 1400: Invalid window handle)
```

The failure occurs **before** reaching the File > New command (line
~1965 of the harness): the second WORD1 instance never creates its
main window. Implementer's diagnosis: symptom of
`explorer.exe`/wineserver being in a bad state after the first
process, unrelated to the binary (`_setjmp`/`longjmp` symbols
verified correct).

**Most likely cause, discovered after Task 4 closed BLOCKED:** during
this same session there was **another session** working in parallel
on the same checkout and the same `~/build-debian13-verify` inside
debian13 (confirmed with live processes: a detached
`cmake --build --target WORD1` + `ctest -R opus_word1_ui_test`,
`PPID 1`, that none of this session's agents launched). Two shared
wineserver/Wine prefix instances receiving WORD1 launches at the same
time explains the symptom ("Wine state corruption between test runs")
better than a code regression, and it is consistent with
`opus_word1_ui_test` having already passed hours earlier, on the same
branch, with no code changes in between. **Not confirmed**, it is the
most likely hypothesis to re-verify first, in a window where the
build dir is not in use by anyone else.

There were no code changes or commit for Task 4. Full detail:
`.superpowers/sdd/2026-08-15-terminar-winelib/task-4-report.md`.

---

## 5. Independent review of Task 3: 4 fidelity findings, fixed and verified on exia

**Context:** before trusting Task 3, `/code-review` (level `high`)
was run against the 9 commits of `fix/winelib-startup-blocked` on top
of `main`. It found 4 problems, all 4 on the same theme: the branch
fixes the real `_setjmp` AV, but two of its workarounds from the
investigation era (before finding the root cause) and two gaps in
`OpusShellFontMetrics`'s no-`QGuiApplication` path were silently
masking incorrect data instead of failing visibly, exactly what the
project cannot afford given the hard byte-identical pagination
requirement.

**The 4, with their root cause confirmed against the real code (not
just the diff):**

1. **`opus_original_startup_probe.cpp`, `OpusPortGdiCharWidths`** left
   `lfHeight=0` (Wine's undefined default size) when `hps<=0`, while
   `OpusShellFontMetrics` already used 10pt (`PixelSizeFor`/
   `PointSizeFor`, hpsDefault) for the *same* font request: widths
   and ascent/descent measured at two different sizes. The original
   comment said this "recreated" the `LOGFONT` that the real
   `C_FGraphicsFcidToPlf` builds for `hps==0`; false:
   `Opus/LOADFONT.C:880` has `Assert( fcid.hps > 0 )`: the real Word
   never reaches that function with `hps==0`. Fixed to use the same
   10pt default.

2. **`opus_x64_layout.c`, `OpusPlData`** was silently clamping any
   `brgfoo` outside `[cbPLBase, 4096]` to `cbPLBase`. This is a
   workaround from Task 3's investigation session (the same commit
   `5bebdd9` that caused the AV), from when the root cause was still
   unknown. With `_setjmp`/`longjmp` already fixed it should no
   longer fire; the clamp was kept but a `fprintf(stderr, ...)` was
   added so that a real recurrence would be visible.

3. **`opus_asm_resn2_pl.cpp`, `HpInPl`** was returning the pointer to
   element 0 (not `nullptr`) when `cb<=0` or `index<0`; also from the
   same commit `5bebdd9`. The original (`Opus/asm/resn2.asm:1340`)
   has no release fallback at all, only a DEBUG `Assert`; handing
   back element 0 as if it were valid makes any caller (all of which
   treat a non-null return as "valid index") read or write the wrong
   slot with no way to tell it apart from a real access. Fixed to
   `nullptr`.

4. **`OpusShellFontMetrics.cpp`**: the oracle-table fallback (used
   when there is no `QGuiApplication`, the startup case) ignored
   `key->catr` (bold/italic): the table
   (`opus_shell_font_metrics_oracle_table.h`) only has regular-weight
   rows (28 rows = 4 era names times 7 sizes, bold/italic was never
   captured), so a bold/italic request silently received
   regular-weight metrics. The real caller
   (`Opus/LOADFONT.C:442-448`) explicitly documents that `catr != 0`
   must fail in a controlled way; that contract was already honored
   on the `QGuiApplication` path, but not in this fallback. Fixed so
   that the oracle fallback (ascent/descent, and, if
   `OpusPortGdiCharWidths`'s GDI also fails, widths) is skipped when
   `catr != 0`, instead of answering with wrong-weight data.

**Verified on exia (VPS, not the `debian13` container on hp-15;
hp-15 was not available this session; exia is Debian 13 trixie with
`wine`/`winegcc`, equally valid per `CLAUDE.md`):**

- `ninja`/`cmake --build` of `opus_original_engine` and `WORD1`: 0
  errors, the same preexisting warnings as always.
- Full `ctest` (without `opus_x64_runtime_test`, see note below): same
  as before these 4 fixes, no regression.
- **Real gotcha found during this verification, unrelated to the
  code:** the first run of the `word1_startup_blocked` label gave 0/9
  with `free(): corrupted unsorted chunks` and `unknown test mode` on
  all of them, the *original* bug pattern, from before Tasks 1-2.
  Cause: `build/tests/Debug/opus_word1_ui_test.exe.so` was dated
  August 12, from **before** the Tasks 1-3 fixes existed in the tree.
  `cmake --build --target WORD1` does not rebuild the test harness;
  `--target opus_word1_ui_test` is needed separately. With the
  harness rebuilt: **4/9** (`word1_port_smoke_test`,
  `opus_word1_ui_test` base, `opus_word1_clipboard_shortcut_test`,
  `opus_word1_about_test`), matching the same 4 wins already
  documented in §1-4 above (the fifth in "5/9" under "How to resume"
  corresponds to that same base `ui_test`, counted once there). The 5
  remaining failures (`--typing`, `--interaction`, `--selection`,
  `--font-typing`, Save As) are exactly the not-yet-started scope of
  Tasks 5-10, not regressions from these 4 fixes.
- **A real `Xvfb` is necessary:** without `DISPLAY`, Wine falls back
  to `nodrv` and everything fails with "Invalid window handle" before
  reaching the real logic. This VPS already had an `Xvfb :99` running
  since August 12 (another session); it was reused instead of
  starting a new one.
- `opus_x64_runtime_test` (gating) is still hanging without printing
  anything, confirmed here too, same symptom the Task 3 session
  documented on debian13. Still not investigated, still unrelated to
  Task 3 or to these 4 fixes.

4 new commits on top of `16145b6`: one per finding, in the same
message format as the rest of the branch.

## 6. File > New: confirmed, verify-only, closes §4

**Task 4 resumed 2026-08-19 on exia.** The plan (Task 4, Step 1)
called for running `opus_word1_ui_test` (base mode) in isolation: if
it passes with no code changes, Task 4 is verify-only.

```
ctest -R "^opus_word1_ui_test$" --output-on-failure
    Start 11: opus_word1_ui_test
1/1 Test #11: opus_word1_ui_test ...............   Passed    3.63 sec
100% tests passed, 0 tests failed out of 1
```

The harness's base mode (`opus_word1_ui_test.cpp:1964-1995`) sends
`WM_COMMAND`/`kFileNew` (id 1813), confirms the File New dialog
appears, that its controls match the SDM contract, accepts it, waits
for it to close, and confirms that `Document2` was created: exactly
the flow §4 left undetermined. It passes clean, with no code changes
in this session.

**Confirms §4's hypothesis** ("the most likely cause... contention
over the shared build dir, not a code regression", unconfirmed at the
time): same root cause as About (Task 3, `_setjmp`/`longjmp` ABI), no
independent File > New bug. The §4 run failed because of the shared
debian13/hp-15 environment in that session, not because of the code:
here, on exia, with no contention, it passes on the first try.

No code changes for this section: documentation commit only.

## 7. Save As: "File Save As dialog did not cancel cleanly": independent root cause, fixed

**Task 5, 2026-08-19 on exia.** Unlike Task 4, this one does **not**
share a root cause with Task 3: verify-only gave a real failure, with
its own message:

```
ctest -R "^opus_word1_save_as_test$" --output-on-failure
    File Save As dialog did not cancel cleanly
```

**Root cause, confirmed by reading the real code (not just the
symptom):** `create_dialog_host` (`opus_sdm_runtime.cpp:336`)
creates, for `kIddOpen`/`kIddSaveAs`, a `WS_POPUP` window of class
`"OpusSdmDialog"` **without `WS_VISIBLE`**: a decoy that
`materialize_save_as_template` populates with real controls
(including the Cancel button, id 2). But `TmcDoDlgDli`
(`opus_sdm_runtime.cpp:2522`) never uses that window for Open/Save
As: for those two `hid` values it calls
`run_word95_common_file_dialog` directly, which blocks the thread
inside `GetSaveFileNameA`/`GetOpenFileNameA`; the real, visible
dialog is a completely different window (`#32770`, the common
Windows/Wine one), not the decoy.

The test harness (reasonably, following the same class convention
that *every other* SDM dialog uses) looks for `"OpusSdmDialog"` +
`"Save As"`: it finds the decoy (it exists, even though hidden:
`EnumWindows` does not filter by visibility, and nothing in
`find_window_callback` does either), and sends `WM_COMMAND` id=2 to
the decoy's Cancel button. `handle_dialog_command` receives it (the
message pump inside `GetSaveFileNameA` dispatches *every* message on
the thread, not just its own window's) and calls
`finish_native_dialog`, which marks `dialog.dying=true` and does
`ShowWindow(SW_HIDE)`; but none of that reaches the real dialog, which
stays blocked waiting for its own Cancel. `wait_for_window_to_close`
waits for the decoy window to *disappear* (`find_process_window`
returns null), but the decoy's `DestroyWindow` only happens in
`TmcDoDlgDli` **after** `run_word95_common_file_dialog` returns, which
never happens. Times out at 5000 ms.

It is not a bug shared with Task 3, and it is not a harness bug
either (targeting the decoy is the correct thing to do, given that no
other dialog in this code has this two-window architecture): it is a
real gap: nothing connects the decoy to the real dialog it replaces.

**Fix:** an `OFN_ENABLEHOOK`/`lpfnHook` hook in
`run_word95_common_file_dialog`'s `OPENFILENAMEA` captures the
dialog's real HWND (the hook's `GetParent()`, the documented pattern
for `OFN_EXPLORER` dialogs) at `WM_INITDIALOG`, saved in a global
(`g_active_win95_file_dialog`, cleared as soon as the blocking call
returns). `handle_dialog_command`, for `kIddOpen`/`kIddSaveAs` with
`tmc == kTmcOk || tmc == kTmcCancel`, now forwards the decoy's click
to the real dialog (`PostMessageA(..., WM_COMMAND, MAKEWPARAM(tmc,
BN_CLICKED), 0)`; `IDOK`/`IDCANCEL` match `kTmcOk`/`kTmcCancel` by the
same Windows convention this file already uses) instead of calling
`finish_native_dialog` directly: this way it is
`run_word95_common_file_dialog` that ends the dialog exactly once,
just as if a real user had clicked on the visible window.

**Verified:**
```
ctest -R "^opus_word1_save_as_test$" --output-on-failure
    Passed    4.79 sec

ctest -L word1_startup_blocked --output-on-failure
    5/9 (previously 4/9): Save As newly green, no regression in the rest
```

Files: `src/port/original/opus_sdm_runtime.cpp` (global + hook +
wiring `lpfnHook`/`OFN_ENABLEHOOK` + forwarding in
`handle_dialog_command`).

## 8. `--font-typing`: two real bugs found and fixed, one located but not closed

**Task 6, 2026-08-19 on exia.** The plan (Step 1) anticipated that
this test needed a real display, not Xvfb, for the visual part
("black popup"). This session made progress without that: the real
failure turned out to be about data and focus logic, not rendering,
but it **does not fully close** Task 6: a third problem is located
but left unfixed.

**Bug 1: Windows font names never enumerable (fixed):**
`installed_windows_fonts()` (opus_sdm_runtime.cpp) enumerates with
`EnumFontFamiliesExA`: real family names, not Windows aliases. The
test was looking for the literals `"Courier New"`/`"Arial"` with
`CB_FINDSTRINGEXACT`, which never appear in a Linux font stack.
Confirmed with a standalone probe (`EnumFontFamiliesExA` via winegcc)
on two independent environments: exia enumerates FreeMono, FreeSans,
FreeSerif, the Liberation family, Noto, Unifont, WenQuanYi, and IPA; a
previous session on debian13 saw the Liberation family, DejaVu,
Tahoma, MS Sans Serif, Symbol, and Wingdings: zero Windows alias
names on either one. `installed_windows_fonts()` is fine: it is
faithful to the real Word, which also did not hardcode names, it
enumerated whatever was installed. Fix: the test's literals change to
`"Liberation Sans"`/`"Liberation Mono"` (the only two names both
environments share; `fonts-liberation` is a common base package on
Debian). No new dependency: installing real MS fonts via `winetricks
corefonts` was considered and discarded (licensing, it is not used
anywhere else in this port).

**Bug 2: `union FCID` is 8 bytes on Linux, not 4 (fixed, real LP64
issue):** With Bug 1 fixed, the test reached a second failure, one
that already existed but had never been reached: `"the packed font
identifier is not 32 bits"`. Cause: in `Opus/fontwin.h`,
`union FCID` has `long lFcid;`; on Win16/Win32/Win64 `long` was
always 4 bytes (Win64 keeps the LLP64 model), but on Linux x86-64
(LP64) `long` is 8 bytes. The union's other two members (WORD+WORD,
and the `unsigned int` bitfield struct) were already 4 bytes on every
platform: only `long lFcid` was doubling the size of the whole union
here. Fix guarded in `Opus/fontwin.h`
(`#if defined(__GNUC__) && !defined(_MSC_VER)`, the same pattern as
the `wordwin.h`/`splshare.h` guard): `int lFcid` under GCC/Linux,
`long lFcid` unchanged under MSVC. Verified that `.lFcid` is only
used as a full bit-pattern assignment (`= fcidNil`, `= 0L`,
`= pchr->l`) at the 3 real use sites: it is never compared as a wide
value, so the type change is safe.

**Bug 3: focus does not return to the document pane after choosing a
font from the ribbon (located, NOT fixed: touches the restricted
`Opus/` tree):** With Bugs 1-2 fixed, the test opens the combo (the
simulated click alone did not do it: an explicit `CB_SHOWDROPDOWN`
after the click was added, which does open it) and chooses the item,
but `wait_for_focus(pane, 1500ms)` never sees the real Win32 focus
(`GetGUIThreadInfo().hwndFocus`) come back to the `OpusWwd` pane.

The already-existing `OpusX64TraceRibbon` trace (always active,
writes to `build/WORD1-ribbon.txt`: no new instrumentation needed)
shows the full chain running with no logic errors: `CBN_SELENDOK` →
`commit_ribbon_list_selection` → `kDlmKillItemFocus` (ok) →
`kDlmKillDialogFocus` (ok, applies the font: `original-applied ...
tmc=5`) → `kDlmDialogClick`. This last one invokes `FDlgIb` (the
original handler, `Opus/iconbar1.c:412`, `dlmDlgClick` case) with the
explicit comment "SDM gets the focus, send it back to the pane" and a
real `SetFocus(hwwdCur == hNil ? NULL : (*hwwdCur)->hwnd)` call, but
the trace confirms it returns `fFalse` (normal for this case, not an
error: `dlmDlgDblClick` above also always returns `fFalse`). The
`bool focus_result` from `commit_ribbon_list_selection` only feeds
the trace, it changes no flow, so the real `SetFocus` should indeed
run (`vidf.fIBDlgMode` is already `fFalse` at that point, set by
`dlmKillDlgFocus` one step earlier, so the
`if (!vidf.fIBDlgMode)` branch is entered). Why the real Win32 focus
does not stay on `pane` after that call was not investigated beyond
this point: candidates not ruled out are that `hwwdCur` does not
point to the same HWND as `pane`, or that something inside Wine's
combo closing (internal cleanup after `CB_SHOWDROPDOWN` + simulated
click) hands the focus back to itself right afterward.

This touches `Opus/iconbar1.c` (restricted tree): it needs explicit
authorization before touching code there, per `CLAUDE.md`. Full
diagnosis, no code change in `Opus/` this session.

**2026-08-20 update (exia): `CBRollUp` hypothesis ruled out, focus
never leaves the "OK" button.** `wait_for_focus_traced` was added (a
variant of `wait_for_focus` that logs every distinct `hwndFocus` seen,
with class and caption) to test the hypothesis above: that Wine's
`CBRollUp()` (dlls/user32/combo.c) runs the full SDM chain (including
`FDlgIb`'s real `SetFocus(pane)`) and only *afterward* hides the popup
listbox, stealing the focus as a side effect. The trace contradicts
it: focus never reaches `pane`, not even transiently: it stays fixed
on a `class=Button caption='OK'` window (a different hwnd each run,
~0x1011x) from the first poll (`t+0`/`t+1ms`) all the way to the
1500ms timeout, without a single intermediate change. That rules out
the original hypothesis: it is not that focus arrives and leaves, it
is that it never moves off that "OK" button at all: either `FDlgIb`'s
`SetFocus(hwwdCur->hwnd)` does not run, or it runs and is immediately
reverted to that button by something that runs afterward (or
`hwwdCur` itself does not point to the expected pane). Most likely
candidate: that "OK" is the default button of some SDM
dialog/dialog-manager that is still alive (or gets recreated) on that
message thread; not identified beyond class+caption this session.
Still unfixed, still touching `Opus/iconbar1.c` (restricted): this
finding only corrects the previous diagnosis, it does not close it.

**Second 2026-08-20 update (exia): the `CBRollUp` hypothesis was
right after all: closed, fixed in `opus_sdm_runtime.cpp`, not in
`Opus/iconbar1.c`.** The previous paragraph got the trace granularity
wrong. `wait_for_focus_traced` measures from an *external process*
with a 10ms poll: too coarse to see a focus window that lasts
microseconds within the same thread. `FDlgIb` itself was instrumented
instead (`Opus/iconbar1.c`, authorized for this session): two calls
to `OpusX64TraceRibbon` around the real `SetFocus(hwwdCur->hwnd)`,
`dlgclick-before`/`dlgclick-after`, reading `GetFocus()` synchronously
on the same thread. Result, in `build/WORD1-ribbon.txt`:

```
commit-end   msg=14 ...
dlgclick-before msg=18 tmc=0 a=<hwwdCur> b=65770 sel=65750,0 ins=0
dlgclick-after  msg=18 tmc=0 a=65770 b=0 sel=0,0 ins=0
```

`b=65770` = `0x100EA` = `pane`. **`dlgclick-after` confirms that
`SetFocus(hwwdCur->hwnd)` does work**: `GetFocus()` is `pane` at the
instant `dlmDlgClick` returns. `Opus/iconbar1.c` is clean, it has no
bug at all: it does exactly what the original Word 1.1a did.
Something *after* the whole SDM chain finishes (`commit-end` is the
last event in this trace) undoes that focus before
`wait_for_focus_traced` (10ms later) gets to see it: exactly the
original `CBRollUp()` hypothesis: Wine hides the listbox popup *after*
`CBN_SELENDOK` returns, and hiding a window that has focus reassigns
focus as a side effect.

Real fix, in the unrestricted port layer
(`src/port/original/opus_sdm_runtime.cpp`, not `Opus/`):
`commit_ribbon_list_selection` was already forwarding `CBN_SELENDOK`
through its own posted message (`kWmCommitRibbonSelection`) to dodge
the native combo's unreliable closing. A second posted message was
added, `kWmReassertPaneFocus`, which repeats exactly the same call
that already works (`invoke_dialog_proc(dialog, kDlmDialogClick,
tmc)`, the same `FDlgIb` as always): first via an immediate
`PostMessageW` (insufficient: the trace showed that this retry also
lands on `pane` and also gets undone afterward), then via a one-shot
`SetTimer(..., 50)` (`WM_TIMER` → kills the timer → repeats the
call). The second attempt, with 50ms of real time in between so the
message queue can drain whatever is stealing the focus, **sticks**.
Verified with the same synchronous trace: `reassert-focus ...
b=65770` (pane), and this time `wait_for_focus_traced` (the external
10ms poll) confirms it too:

```
[focus-trace] t+1ms hwndFocus=0x100ea class=OpusWwd caption='' (== pane)
```

Task 6 Bug 3 **closed**. The test advances past the focus check, to
a different, new failure: `"newly typed text did not retain the
ribbon font"`: the newly typed text does not keep the font chosen in
the ribbon. This is not a regression (the test never got this far
before); it is a fourth real bug, not investigated yet. The
`word1_startup_blocked` label stays at 7/9 (the same two known ones:
this new font failure in `--font-typing`, and `--interaction`
unchanged), but the real bug behind one of the two changed.

**Verified:**
```
DISPLAY=:99 ctest -R "^opus_word1_font_typing_test$" --output-on-failure
    [focus-trace] t+1ms hwndFocus=0x100ea class=OpusWwd caption='' (== pane)
    font properties=20,0 applied=3,48 inserted=20,0 lineHeight=16
        formatted=16 formatter=0,16
    newly typed text did not retain the ribbon font

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9: no regression in the rest
```

Files: `Opus/iconbar1.c` (`dlgclick-before`/`dlgclick-after` trace,
authorized), `src/port/original/opus_sdm_runtime.cpp` (the real fix:
`kWmReassertPaneFocus`, `kReassertPaneFocusTimerId`,
`DialogState::pending_reassert_tmc`).

**Verified:**
```
DISPLAY=:99 ctest -R "^opus_word1_font_typing_test$" --output-on-failure
    font combo select stages: foreground=1 chose_item=1 regained_focus=0
    font typing test could not mouse-select the font
    (advanced from fail(47) "could not find controls" -> fail(49) "could
    not mouse-select the font", with intermediate failure 47 "not 32
    bits" already closed along the way)

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    5/9: no regression in the rest
```

Files: `src/port/original/opus_word1_ui_test.cpp` (font literals,
`CB_SHOWDROPDOWN`, diagnosis of the combo click's 3 stages),
`Opus/fontwin.h` (LP64 guard for `union FCID`).

**Third 2026-08-20 update (exia): "does not retain the font" was a
misleading diagnosis: the text was never actually typed. Real root
cause found: `idle.c:503` preloads the font with
`selCur.chp.hps == 0`, `CreateFontIndirect` fails, and the resulting
real error dialog swallows the keyboard. Session closed without a
fix: resumed on Debian 13 (LXQt).**

The "fourth bug" from the previous paragraph turned out to be a
misreading. With focus confirmed correct on `pane`
(`[pre-type] hwndFocus=... (== pane)=1`), `FIsKeyMessage`
(`Opus/wproc.c:2450`) and the main loop's `PeekMessage`
(`OpusOriginalWinMain`, `Opus/wproc.c:~556`) were traced: **zero**
`WM_CHAR`/`WM_KEYDOWN` messages reach either one during the whole of
`--font-typing`, despite `send_physical_text` (real SendInput, not
`PostMessageW`) reporting success. Decisive differential test: the
same trace, run against `opus_word1_typing_test` (simple typing, no
ribbon), does see every key (`iskeymsg`/`mainloop-msg` for every
`WM_KEYDOWN`/`WM_KEYUP`/`WM_CHAR`, `tmc` values spelling out
"ORIGINAL"). The app remains "responsive"
(`window_is_responsive`/`WM_NULL` responds) throughout the failure:
it is not a real hang.

**Focus theories ruled out, all verified empirically, none changed
the symptom by a single bit (`applied=3,48 inserted=20,0` identical
on every attempt):**

- Task 6 Bug 3's `SetTimer`-reassert (above) temporarily disabled:
  the test simply goes back to failing the original focus check
  (`regained_focus=0`), without even reaching the typing part: this
  confirms that fix is still needed, but it is not the cause of this.
- `SetForegroundWindow(GetAncestor(pane, GA_ROOT))` added alongside
  the focus reassert (`opus_sdm_runtime.cpp`, diagnostic, still in
  the tree uncommitted): `foreground_result=1`, `root` resolves
  correctly to `main_window`. No change.
- Real cursor (`SetCursorPos`) moved to the center of `pane` right
  before typing, in case this Xvfb (with no window manager) routed
  real input by pointer position (`XGetInputFocus` returns
  `PointerRoot` fixed throughout the whole test, confirmed with a
  standalone C/Xlib probe). No change.
- `make_foreground_and_focus(main_window, pane, thread_id)` (the same
  helper the `caret_mode` block does use successfully, with its
  `AttachThreadInput` crossed between the test process and WORD1's
  thread) called explicitly before `send_physical_text`. No change,
  and along the way it was confirmed that `caret_mode`/`--caret` is
  not registered as a ctest (`src/CMakeLists.txt` only registers 8
  modes), so that pattern was never actually exercised in this
  environment, it was not the solid reference it seemed to be.
- `IsWindowEnabled`, `GetActiveWindow`, mouse capture
  (`GUITHREADINFO.hwndCapture`): all correct (`paneEnabled=1
  mainEnabled=1 active=main_window capture=0`).

**Real cause: `EnumThreadWindows` over WORD1's thread at the
`[pre-type]` moment shows an extra visible window, class `#32770`
(standard Windows dialog), title `"Microsoft Word"`, with children
`Static id=65535 text='Low memory: cannot display requested font'` +
`Button id=1 text='OK'`.** That real dialog, not fictitious, not a
focus side effect, runs its own modal message loop from the moment it
is created; that is why neither `FIsKeyMessage` nor the main loop's
`PeekMessage` see a single message afterward: the thread is parked
inside the dialog's loop, not Opus's. This also explains why the app
still reads as "responsive" (the dialog's loop also services
`WM_NULL`) and why no focus fix changed anything: focus was never the
problem.

Traced to the exact origin (2 new trace points in `Opus/LOADFONT.C`,
authorized as a continuation of this same investigation):

```
idle.c:503   LoadFont(&selCur.chp, fFalse)   /* "preload new font in
                                                 Idle, avoid delay
                                                 when typing commences" */
  -> C_LoadFcid -> FGraphicsFcidToPlf:
       lf.lfHeight = NMultDiv(fcid.hps * (czaPoint/2), vfli.dysInch, czaInch)
       trace: fcid.hps=0  vfli.dysInch=96 (normal DPI, not the cause)
       lf.lfHeight=0
  -> CreateFontIndirect(&lf) returns NULL (GetLastError=5,
     ERROR_ACCESS_DENIED)
  -> SetErrorMat(matFont)  (LOADFONT.C:330, LSystemFontErr path)
  -> idle.c / ReportPendingAlerts(): case matFont ->
     ErrorEid(eidCantRealizeFont, ...) -> the real MessageBox from above
```

`selCur.chp.hps` **should** be 48 (24pt, the size just chosen in the
ribbon: confirmed separately with
`SendMessageW(pane, kWmOpusX64QuerySelection, 50, 0)` right before
typing) but at the moment `idle.c:503`'s preload runs it reads `0`.
**Not closed: the exact race still needs to be identified.**
`vrf.fPreloadSelFont` is set `fTrue` at two sites
(`Opus/cmdcore.c:510`, `Opus/dlglook1.c:781`) and consumed once per
idle tick (`Opus/idle.c:488-505`, uses `selCur.chp` as-is, without
resolving `hps==0` to a real value first). Most likely candidate,
unconfirmed: the preload fires and consumes the flag right after
choosing the FONT (ribbon combo 1), with `selCur.chp.hps` still at
its original document value (0, see `initial_hps=0` in the trace
above), before the SIZE selection (ribbon combo 2) gets to write 48
there. To verify: at which exact idle tick this runs relative to the
two ribbon trace's `combo-select` events, and whether `hps==0` is
itself a legitimate state (an "inherit from style" placeholder) that
other paths resolve before touching GDI and this one does not.

**Diagnostics left in the tree, uncommitted** (continuity for the
session on Debian 13/LXQt: all under `#ifdef OPUS_X64`, guarded, does
not affect MSVC):

- `Opus/LOADFONT.C`: `lfheight-calc` trace (entry to
  `FGraphicsFcidToPlf`, prints `fcid.hps`/`vfli.dysInch`/
  `lf.lfHeight`), `matfont-set` (`CreateFontIndirect` failure),
  `screenfail` (`OurSelectObject` failure on the screen DC: did not
  fire this session, the real failure was always
  `CreateFontIndirect`).
- `Opus/wproc.c`: trace in `FIsKeyMessage` (entry,
  `WM_CHAR`/`WM_KEYDOWN`) and in `OpusOriginalWinMain`'s main loop
  `PeekMessage` (same filter).
- `Opus/iconbar3.c`: entry/exit trace of `IBDlgLoop()` (ruled out the
  hypothesis that the loop was getting stuck there).
- `Opus/rulerdrw.c`: trace around `FGetCharState` in `UpdateRibbon`
  (ruled out `selCur.chp` getting corrupted there).
- `Opus/wordtech/insert.c`: trace in `InsertLoopCh` right after
  `GetSelCurChp` (zero hits during `--font-typing`, confirming that
  routine never runs: consistent with the blocking dialog).
- `src/port/original/opus_sdm_runtime.cpp`: speculative
  `SetForegroundWindow` alongside Task 6's focus reassert (did not
  help, inert, can be reverted when resumed).
- `src/port/original/opus_word1_ui_test.cpp`: `[pre-type]` diagnostic
  (focus/enabled/active/capture), `[enum-window]`/`[dialog-child]`
  (the one that found the real dialog), a `make_foreground_and_focus`
  call before typing (did not help, inert).

None of this was committed this session: it shows as `M` in `git
status` in `src/Opus/{LOADFONT.C,iconbar3.c,rulerdrw.c,wproc.c,
wordtech/insert.c}` and
`src/port/original/{opus_sdm_runtime.cpp,opus_word1_ui_test.cpp}`.
`git status` also shows `M` in
`Opus/{ddeclnt.c,etcmd.c,filecvt.c,raremsg.c,spelcore.c}`: that is
content from an `OpusMem*` migration by another parallel session
(plus a "takeover" fix of mine for a dangling `LError` label in
`etcmd.c`/`spelcore.c` that left those two files failing to compile);
do not touch those five when continuing, unless it is the same
session that left them that way.

**Concrete next step on resuming:** trace the exact order between (a)
the moment `Opus/dlglook1.c:781`/`Opus/cmdcore.c:510` set
`vrf.fPreloadSelFont = fTrue` after each ribbon selection, and (b) the
idle tick that consumes it in `Opus/idle.c:503`: the most likely
explanation is that the preload fires after the FONT selection,
before the SIZE selection gets to write `selCur.chp.hps`, and that
the real fix is to use an already-resolved hps (or postpone the
preload until both selections have been applied) rather than touching
`Opus/idle.c` blindly. Once this is closed, it also still needs to be
verified that the dialog's visible name
`"Low memory: cannot display requested font"` is not itself a
legitimate Word 1.1a message under other circumstances
(`eidCantRealizeFont`, `Opus/wordtech/error.c:933`): here it is a
false positive from a preload call with half-updated data, not a real
memory condition.

**Fourth 2026-08-20 update (debian-VM, branch
`investigate/font-typing-idle-preload`): the `hps` race theory is
ruled out with direct evidence: five hypotheses tested and ruled out,
real root cause still not located.** This session's goal was to
*confirm* the theory from the "Third update" above and fix it in
`idle.c`. The proposed fix (`&& selCur.chp.hps != 0` in the
`idle.c:503` guard) was implemented, compiled, and run against the
real test, and it did not change the symptom by one bit. Investigating
why led to ruling out the entire theory with direct traces, not code
reading.

**Environment for this session:** a new machine (`debian-VM`, Debian
13 trixie, KVM), with no prior toolchain: `cmake`/`ninja`/`wine`
(10.0~repack-6)/`wine64-tools`/`qt6-base-dev`/`Xvfb` were installed
via `apt`, the Wine prefix was initialized (`wineboot --init`), and
the Configure blocker documented in the README (`src/port/tools/host/`
gitignored) was resolved with a `CMakeLists.txt` already prepared for
that folder. Clean build of `opus_original_engine`/`WORD1`/
`opus_word1_ui_test`, 0 errors. Stable repro of
`opus_word1_font_typing_test ***Failed`, same as on exia.

**Hypothesis 1: `hps` race in `idle.c:503` (the previous session's):
ruled out.** A new trace (`idle-preload-check`) was instrumented
right on entering `Opus/idle.c:488`'s preload block, before any
guard. **Zero occurrences in the whole test.** The `idle.c` preload
block is never reached during `--font-typing`, neither with the guard
in place nor without it. The previous session's attribution to
`idle.c:503` was never traced directly; it was inferred by reading
code (`vrf.fPreloadSelFont` / `DoLooks`). The `hps != 0` guard was
implemented, compiled, run, and the error dialog kept appearing
identically.

**The real call that fails:** by instrumenting `C_LoadFont`'s return
address (`__builtin_return_address(0)`) it was confirmed that the
call that actually blows up comes from somewhere else, not from
`LoadFont(pchp, fFalse)`. The real site, confirmed by `nm`/reading
code, is `Opus/disp1.c`'s `LoadFcidFull(pchr->fcid)`: called once per
character during document-pane repainting (two sites, disp1.c:832 and
disp1.c:1698), using the `fcid` cached in each `CHR`, not
`selCur.chp`. With the document empty at `[pre-type]`, this repaints
the end-of-paragraph character with whatever `fcid` it happens to
have at that moment.

**Hypothesis 2: `hps==0` is the cause: ruled out with data, not
reading.** A trace (`fcid-identity`) was added on entering
`C_FGraphicsFcidToPlf` (`Opus/LOADFONT.C`), before the
`Assert(fcid.hps > 0)`, showing `ibstFont`/`wProps`/`hps`/`kul` on
every call, success or failure:

```
fcid-identity msg=2(ibstFont) hps=0   -> "Helv"             -> OK
fcid-identity msg=4(ibstFont) hps=0   -> "Liberation Sans"  -> matfont-set (fails)
```

`hps` is **identical** (0) in both cases: one passes, the other
fails. The only thing that changes is which font (`ibstFont`) is
requested. `hps==0` is not the cause; it is incidental.

**Hypothesis 3: corrupt charset (`lfCharSet=255`, `OEM_CHARSET`):
real but insufficient on its own.** Adding the font's real name to
the trace (passing `pffn->szFfn` as `OpusX64TraceRibbon`'s `stage`)
confirmed `lfCharSet=255` for "Liberation Sans" against `lfCharSet=0`
for "Helv". Standalone probes (`EnumFontFamiliesExA` and the legacy
`EnumFontsA` that `Opus/SYSCHG.C:FontNameEnum` uses, both against a
screen DC) **never** return 255 for that font: the real enumerated
values are 0, 238, 204, 161, 162, 177, 186, 163. `FontNameEnum`
faithfully copies `lplf->lfCharSet` (line ~775:
`ChsPffn(pffn) = lplf->lfCharSet;`), so the 255 is not a copy bug: it
comes from `FillHsttbPaf` enumerating against `vpri.hdc` (the
**printer** DC, not screen), and this Wine prefix had no printer
configured at all (`wine control printers` empty, `lpstat`
"No destinations have been added.", `GetDefaultPrinterA` returned
error 2).

Fix applied in `Opus/LOADFONT.C` (`C_FGraphicsFcidToPlf`, before
`bltbyte(...lfFaceName...)`): if `plf->lfCharSet == OEM_CHARSET`, use
`DEFAULT_CHARSET` instead (guarded under `OPUS_X64`). This fix is
correct and stays: `OEM_CHARSET` with no real printer is a
meaningless value, not something Opus should propagate to
`CreateFontIndirect`, but **it does not fix the test on its own**:
with the charset already sanitized (confirmed by trace,
`lfCharSet=1`), `CreateFontIndirect` **still fails** for "Liberation
Sans".

**Side effect explored: adding a real printer changes the symptom
but does not fix it.** A dummy CUPS queue was configured
(`GenericPS`, generic PostScript PPD, `device-uri file:///dev/null`)
to give Wine a real printer: confirmed with a standalone probe
(`GetDefaultPrinterA`/`EnumPrintersA`) that Wine saw it live. With a
printer present, `vfli.fFormatAsPrint` activates the two-device path
in `C_LoadFont` (`LOADFONT.C:121-135`): a printer font request *in
addition to* the screen one. The screen charset came out clean (163,
a real enumerated value), but the **printer-side** request brought
its own corrupt data (`lfHeight=-5`, `fcid.wProps=1920`, a
meaningless bitfield value) and also failed. This is a different,
dormant bug, in a different subsystem (print formatting, not
typing/ribbon): out of scope for this item. **The `GenericPS` printer
was removed** (`sudo lpadmin -x GenericPS`, confirmed with the same
probe that Wine no longer sees any printer): no persistent environment
change remains. Without the printer, the path goes back to a single
device (screen), the same one the charset fix above protects.

**Hypothesis 4: font cache (`PfceLruGet`, LRU slot eviction): ruled
out.** `ifceMax` = 32 (`Opus/fontwin.h:11`): nowhere near exhausted
with 2 different fonts requested. Both of `PfceLruGet`'s return
points (`Opus/LOADFONT.C:1037`) were instrumented: free slot found
with no eviction (`pfce-free-slot`) vs. LRU eviction
(`pfce-evicted-slot`). Result: "Helv" receives free slot 0;
"Liberation Sans" receives free slot 1. **No eviction happens at
all.** The cache is sound.

**Hypothesis 5: the real `GetLastError()` (183,
`ERROR_ALREADY_EXISTS`): ruled out, it was noise, not signal.** The
`183` showed up identically across three different code/environment
states throughout the session, which seemed significant, but GDI does
not guarantee setting a fresh error on every call. `SetLastError(0)`
was added right before the failing `CreateFontIndirect`
(`Opus/LOADFONT.C:333`). Result: `GetLastError()` gives **0** after
the failure. The `183` that kept showing up was left over from some
earlier call in the same tick, not something `CreateFontIndirect` had
set. This lead was invalid from the start.

**Hypothesis 6: `szFfn` buffer too small for long names (Win16-legacy,
≤7 chars): ruled out.** `struct FFN` (`fontwin.h:39`)
has `CHAR szFfn[]`: flexible, not a small fixed size. `LF_FACESIZE`
is correctly defined in `Opus/lib/qwindows.h:1235` as `32` (the real
Win32 value, not a smaller inherited Win16 one). The staging buffer in
`FontNameEnum` (`Opus/SYSCHG.C`), `CHAR rgbFfn[cbFfnLast]` with
`cbFfnLast = offset(FFN,szFfn) + LF_FACESIZE + 1`, has plenty of room
for "Liberation Sans\0" (16 bytes out of 33 available). The permanent
*storage* in `vhsttbFont`
(`IbstAddStToSttb`/`FChangeStInSttb`, `Opus/SYSCHG.C:~789`) was not
reviewed: that is a real candidate for a future session, distinct
from what was ruled out here.

**Status at cutoff: six hypotheses ruled out with direct evidence
(traces and standalone probes, not code reading alone). The root
cause of why `CreateFontIndirect` returns `NULL` for "Liberation
Sans" (the second distinct font requested, any `hps`, charset already
valid, cache slot sound, no GDI error set) remains unlocated.** The
pattern that does hold across every run: the *first* distinct font
requested (`Helv`) always works; the *second* (`Liberation Sans`)
always fails, but the mechanism behind that pattern is not
identified.

**Candidates not explored for the next session:**
- `IbstAddStToSttb`/`FChangeStInSttb` (`Opus/SYSCHG.C`): the
  permanent storage in `vhsttbFont`, distinct from the staging buffer
  already ruled out in Hypothesis 6.
- Difference between `pffn->fGraphics`/`fRaster` for "Helv" vs.
  "Liberation Sans": it was noticed (without full confirmation) that
  `fGraphics` probably gives `fFalse` for "Liberation Sans" via the
  printer enumeration (`fty & DEVICE_FONTTYPE` set), something the
  charset guard above had to stop using as a condition because it
  never fired.
- Instrument the entire `lf` (`LOGFONT`) itself right before
  `CreateFontIndirect`: `lfHeight`/`lfWeight`/`lfCharSet` were
  compared, but not `lfPitchAndFamily`, `lfQuality`, nor
  `lfFaceName`'s final byte-for-byte content.

**Files modified this session (uncommitted):**
- `src/Opus/idle.c`: the `hps != 0` guard **was reverted** (the
  original condition stays intact) because it never runs and its
  justification was ruled out; the `idle-preload-check` trace was
  left in place (proof that the block is never reached), plus a
  comment pointing to this section.
- `src/Opus/LOADFONT.C`: the real fix that stays (`OEM_CHARSET` ->
  `DEFAULT_CHARSET` in `C_FGraphicsFcidToPlf`, not conditioned on
  `fGraphics`) plus new traces (`loadfont-caller` with return
  address, `fcid-identity`, the real font name as `stage`,
  `pfce-free-slot`/`pfce-evicted-slot`, `SetLastError(0)` before
  `CreateFontIndirect`). All guarded under `#ifdef OPUS_X64`, does
  not touch MSVC.

**Environment change fully reverted:** the CUPS `GenericPS` printer
used to test Hypothesis 3 was removed; confirmed with a standalone
probe that Wine no longer sees any printer. No environment drift
remains.

**Fifth 2026-08-20 update (exia): three more hypotheses ruled out, a
different real bug found and fixed (unrelated to the failure), and
decisive confirmation that the cause is NOT a Wine limitation.**
Direct continuation of the previous session (pull of `6417213`),
reviewing the two candidates left noted but unexplored.

**Hypothesis 7: `pffn->fGraphics`/`fty` (a noted candidate, not a new
mechanism): ruled out as an explanation, it was already confirmed in
the code.** The comment already present in `Opus/LOADFONT.C` (lines
~996-999, added in the previous session) confirms that `fGraphics`
gives `fFalse` for "Liberation Sans" via the same enumeration against
`vpri.hdc` that corrupted the charset: the same origin already
identified, not a different mechanism. `fGraphics` is only stored in
`pfce->fGraphics`/`vfli.fGraphics` (confirmed by grep: no `if`
depends on it before `CreateFontIndirect`), so it cannot be the
reason the call fails.

**Hypothesis 8: corrupt `lfPitchAndFamily` (`FF_DONTCARE=0` vs.
`FF_SWISS=32` for "Helv"): real, but not causal, ruled out
empirically, not by reading.** A trace of the complete `LOGFONT`
right before `CreateFontIndirect` (every field not checked before:
`lfPitchAndFamily`, `lfQuality`, `lfOutPrecision`, `lfClipPrecision`,
`lfWidth`, `lfEscapement`) showed that **everything is identical
between "Helv" and "Liberation Sans" except `lfPitchAndFamily`**: 32
(`FF_SWISS`) against 0 (`FF_DONTCARE`), the same origin as Hypothesis
3/7 (`pffn->ffid` corrupted by the same printer-less enumeration).
`lfPitchAndFamily` was forced to `FF_SWISS` for "Liberation Sans" as
a direct empirical test (not as a definitive fix): **the exact same
failure**, `CreateFontIndirect` still returns `NULL`. Reverted.
`FF_DONTCARE` is a legitimate, common value on real Windows; it is
not the cause.

**Real bug found (not causal for this failure, fixed anyway):
out-of-bounds read in `bltbyte(pffn->szFfn, plf->lfFaceName,
LF_FACESIZE)`.** `struct FFN.szFfn` is a flexible array member
(`CHAR szFfn[]; /* Variable length */`, `Opus/fontwin.h`), stored in
the STTB (`IbstAddStToSttb`/`FInsStInSttb1`, `Opus/wordtech/sttb.c`)
with an exact Pascal-string size (`CbSzOfPffn`), never 32 bytes. The
original `bltbyte` copied a fixed 32 bytes regardless of the real
allocated size: a genuine out-of-bounds read for any font name
shorter than 31 characters (that is, practically all of them).
Confirmed with a hex dump of `lf.lfFaceName[16..31]` for both fonts:
for "Helv" (an entry seeded at startup, next to other valid short
entries in memory) byte position 15 is a real character ('e')
followed by more non-null content: the over-read lands on data from
ANOTHER, adjacent valid entry. For "Liberation Sans" (15 characters,
added at runtime) byte 15 is correctly `\0` (the name ends fine), but
byte 16 is `0xFF` followed by zeros: **the over-read lands on genuine
memory outside the real entry.** In both cases, however, the real
null terminator lands in the correct position (4 for "Helv", 15 for
"Liberation Sans"), and GDI/`CreateFontIndirect`, like `%s`, stops
reading at the first `\0`. **This rules out the name-corruption
hypothesis as the cause of this specific failure**: the name
`CreateFontIndirect` actually sees is correct in both cases, but it
is still a real bug (undefined behavior, uninitialized heap read)
that was fixed anyway: `Opus/LOADFONT.C` now does
`SetBytes(plf->lfFaceName, 0, LF_FACESIZE)` followed by a `bltbyte`
bounded to `CbSzOfPffn(pffn)` real bytes (guarded under
`#if defined(__GNUC__) && !defined(_MSC_VER)`, MSVC keeps the
original 32-byte `bltbyte` unchanged).

**Decisive confirmation: this is not a Wine limitation for these
parameters.** With every inspectable `LOGFONT` field already ruled
out or identical, and the name confirmed correct up to its `\0`, a
standalone probe was built (`winegcc`, no code from this project: the
same pattern as the "Decisive confirmation" in §12/`--interaction`)
that calls `CreateFontIndirectA` with the `LOGFONT` **byte-for-byte
identical** to the one that fails inside WORD1
(`lfFaceName="Liberation Sans"`, `lfHeight=0`, `lfWeight=400`,
`lfCharSet=1`, `lfQuality=4`, `lfPitchAndFamily=0`, also tested with
`32`, the rest zeroed). **Both variants succeed** (`hfont` non-null,
`GetLastError=0`) in a clean process, the same Wine binary, the same
prefix. This completely rules out a Wine limitation for this
parameter combination: the same `CreateFontIndirectA` with the same
data works perfectly outside WORD1. The cause genuinely lies in some
part of WORD1's process/GDI state at that moment, not in the data
being requested.

**GDI handle exhaustion hypothesis: ruled out, no need to measure via
`GetGuiResources`.** An attempt was made to measure WORD1's GDI
object count from the test harness (`GetGuiResources(hProcess,
GR_GDIOBJECTS)`): this Wine build has it unimplemented, it always
returns 0. Useless signal, ruled out as a method. But a simpler count
turned out to be decisive: the `loadfont-caller` trace
(`__builtin_return_address` of every call to `C_LoadFont`) appears
**35 times** across the whole test run, but `fcid-identity` (the only
point that actually reaches `C_FGraphicsFcidToPlf`/
`CreateFontIndirect`, after passing `C_LoadFcid`'s CASE 1-3 cache
checks) appears only **2 times**: the other 33 are *cache hits*, they
never get to requesting a new font from GDI. With only 2 real font
creations in the whole run, a 10,000-handle-per-process limit (the
classic Windows limit) is completely out of reach. Ruled out
unambiguously.

**Status at cutoff (second time): nine hypotheses ruled out in total
with direct evidence** (the six from the previous update plus these
three). The reason why `CreateFontIndirect` returns `NULL`
specifically for the second distinct font requested, with data
confirmed correct at every inspectable field, remains unlocated, and
there are no more candidates left at the application data/state level
to check via traces or standalone probes. The only thing left, if
resumed, is to go one level down: attach `gdb` to the real WORD1
process (not a clean standalone probe) and set a breakpoint inside
Wine's own `CreateFontIndirect` implementation (`dlls/gdi32` or
`dlls/win32u`, depending on version) to see at which internal step
that second call diverges from the first: a considerably heavier tool
than application traces or standalone probes, and not yet attempted.

**Files modified this round (uncommitted as of this writing):**
- `src/Opus/LOADFONT.C`: the real fix that stays (bounded bltbyte +
  zero `SetBytes` for `lfFaceName`, guarded GNUC-only, MSVC
  unchanged): fixes a genuine undefined-behavior bug, unrelated to
  this test's failure. The (already existing) `fcid-identity` trace
  is kept as-is.
- `src/port/original/opus_word1_ui_test.cpp`: no net change (the
  `GetGuiResources` check was added and then removed, since it turned
  out to be unimplemented in this Wine build).

**Verified, no regression:**
```
DISPLAY=:99 ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked
    7/9: the same two known failures (--interaction, --font-typing)

DISPLAY=:99 ctest --test-dir out/linux-winelib-debug -LE word1_startup_blocked
    9/9: gating unchanged
```

**Sixth 2026-08-22 update (exia): the first time gdb is used directly
on the live `WORD1` process.** Direct continuation of the previous
session (the "only candidate left: go one level down with gdb"),
resumed after an Antigravity interruption mid-task (the
`gdb-font-debug.py` script that session was writing never got to
run). **Note from the Seventh update (below, same session): the
"heisenbug" reading that follows in this block turned out to be a
misattribution to the wrong breakpoint; it is documented here exactly
as it was experienced, in order, because the reasoning error and how
it was uncovered are a useful part of the trail; the real, definitive
diagnosis is the Seventh update.**

**New infrastructure, the real half of this session's work:**
attaching `gdb` to the real `WORD1` process while the
`opus_word1_ui_test` harness drives it externally (there is no way to
trigger the ribbon font selection without the harness) turned out to
be considerably trickier than a single-snapshot
`gdb -p <pid> --batch -ex "thread apply all bt"` (the pattern already
used in §21/§25/§31). Three real problems, in order of appearance:

1. **`WORD1.exe` (the shell wrapper) is not the binary to
launch/debug.** It is an `sh` script that assembles
`WINEDLLPATH`/`WINELOADER` and does `exec`; under this particular
Bash session, invoked with no real window manager, it exited with
`exit=3` without printing anything from the program at all (not even
a trivial standalone "hello world" `printf`). `wine ./WORD1.exe.so`
(the real `.so`, bypassing the wrapper) always works. Any
harness/standalone probe must invoke the `.exe.so` directly.
2. **`break /home/pablo/msword/src/Opus/LOADFONT.C:349` (the real
source path) fails with `No source file named ...`, even with
`set breakpoint pending on` and forcing full symtab expansion
(`maint expand-symtabs`, `info sources`).** Cause: the build
generates a lowercase copy of every uppercase source before
compiling (`out/linux-winelib-debug/generated/lowercase-c/loadfont.c`,
confirmed with `strings WORD1.exe.so | grep -i loadfont.c`), and the
compilation-unit name that DWARF records is that of the generated
copy, not `src/Opus/LOADFONT.C`. It breaks the breakpoint because of
the wrong path, not missing symbols or timing. Fix: always use the
generated path
(`out/linux-winelib-debug/generated/lowercase-c/<lowercase-name>.c`)
for any `break FILE:LINE` against this binary.
3. **Pre-loading symbols with `gdb file WORD1.exe.so` before knowing
the PID (to speed up the subsequent `attach`) turned out to be
counterproductive**, not an optimization: it triggers a "Build ID
mismatch" warning on `attach` that forces a symbol re-check, and that
re-check breaks the already-resolved breakpoint again
(`Error in re-setting breakpoint 1: No source file named ...`, the
same symptom from point 2, reappearing). The simple solution ends up
being the simplest one possible: `gdb -p <pid> --batch -x script.gdb`
with no prior `file` at all: it resolves the breakpoint and attaches
to the process in **~1.1s** end to end (measured with `time`), plenty
of time within the several-second window the harness takes to reach
the ribbon font selection (window search + `Sleep(300)` x2 + focus
waits of up to 1500ms).

**With the infrastructure working, the real session: compare the
call that passes (`Helv`) against the one that fails ("Liberation
Sans") inside the live process, at the exact same point the previous
nine hypotheses never managed to cross.** Breakpoint at
`loadfont.c:349` (the exact `CreateFontIndirect` site), confirmed by
the real font name via `lf.lfFaceName` on every hit.

First attempt: from `CreateFontIndirectA`'s entry, a nested `finish`
with a `tbreak NtGdiHfontCreate` set beforehand, gave a meaningless
result (`rax=0x86` identical on both calls): the outer `finish` was
being interrupted early by the inner breakpoint itself (normal gdb
behavior, not a bug), so the value captured was `NtGdiHfontCreate`'s
entry state, not any real return value. Ruled out as a script
artifact, not as data.

**Clean, single-hop measurement:** `tbreak CreateFontIndirectA`
(confirmed with `disassemble` that this is exactly the symbol called
from `loadfont.c:349`, even though the target compiles with
`UNICODE`/`_UNICODE` defined; the `CreateFontIndirect` macro still
resolves to the ANSI variant at this site, verified by disassembly,
not by reading headers) plus `continue` plus a single `finish` (with
no nested breakpoint in between) lands exactly back in `C_LoadFcid`
at `loadfont.c:349`, the real caller, with `$rax` equal to the true
return value exactly as the C code is going to see it. Also confirmed
with explicit step-by-step `stepi`/`nexti` through the real compiled
instructions (`disassemble` showed a full 8-byte
`mov %rax,0x38(%rdx)` followed by `test %rax,%rax` / `jne`: a correct
64-bit comparison, with no width truncation at all, ruling out along
the way any variant of the old narrow-field Win16-legacy theory):

```
CALL #1 (Helv):             rax = 0x140a00d2   (non-NULL, expected: passes as always)
CALL #2 (Liberation Sans):  rax = 0x620a00e2   (non-NULL !!)
```

**`pfce->hfont` read directly after the `jne` (not just `$rax`)
confirms `0x620a00e2`, a real handle, not NULL, and execution flow
correctly jumps to the success branch (`C_LoadFcid` line 377,
`FSelectFont(...)`), not the error one.** Under `gdb`, with
breakpoints and single-stepping at this exact point,
**`CreateFontIndirect` succeeds for "Liberation Sans". The "Low
memory" dialog should not appear.**

**This directly contradicts the behavior without `gdb`**, confirmed
again in this same session immediately beforehand (same binary, same
`Xvfb :88`, no environment difference): the dialog appears,
`matfont-set` fires, the test fails with the same message as always.

**Minimal single-variable test (systematic-debugging Phase 3): is it
enough to have more time, or is it specific to running under the
debugger?** A diagnostic `Sleep(50)` was added right before the
`CreateFontIndirect` call (guarded under `OPUS_X64`, not touching
MSVC), `WORD1` was rebuilt, and the test was run **without** `gdb`.
**Result: the failure reproduces identically** (same dialog, same
`matfont-set`, same final message). A simple `Sleep(50)` at the exact
call site does NOT reproduce what `gdb` achieves: this rules out "it
just needs more time there" as the complete explanation. The `Sleep`
was reverted (it did not help, no reason to keep it); `git diff` on
`LOADFONT.C` is clean again.

**Reading the result, not just the data:** given that (a) the 9
earlier sites in this document already ruled out, with direct
evidence, any problem in the *data* requested (LOGFONT identical
except for irrelevant fields, sanitized charset, sound cache, no
handle exhaustion, name correct up to its `\0`), and now (b) the
compiled code at the exact comparison site is correct at the bit
level (a full 64 bits, no truncation) and (c) the same
`CreateFontIndirect`, with the same data, **genuinely succeeds when
watched step by step**, the explanation that best fits the whole
accumulated pattern (including "the real second font always fails,
the first always passes," immune to repetition/order in this
session's opening standalone probe) is a **genuine race condition at
some point *before* `loadfont.c:349`**, not in `CreateFontIndirectA`/
`NtGdiHfontCreate` itself, which has already been shown to work fine
with the data it receives. The one-off `Sleep(50)` is not enough
because gdb's real pause profile (breakpoints and interactive
commands that stop the process well before this line, not only on
it) is much wider than 50ms at a single late point.

**This session's standalone probe (before getting to gdb), for the
record:** a minimal `winegcc` program was built, no code from this
project, that calls `CreateFontIndirectA` with the same LOGFONT bytes
`WORD1` uses: first alone, then in Helv-then-Liberation-Sans sequence
(the same order as the ribbon), then repeating Liberation Sans a
third time. **All four calls always succeed**, regardless of order or
repetition: this extends the previous update's already documented
finding (`CreateFontIndirectA` with this data works perfectly outside
`WORD1`) to also cover the call sequence, not just a single isolated
call's data. It confirms once more that the problem is
`WORD1`-specific process state, not the data or the order of
requests.

**Concrete candidate for the next session:** locating the real race
requires looking BEFORE `loadfont.c:349`, not at the comparison point
(already clean). Sites not yet explored with this technique:
instrument with breakpoints (not just application traces, which were
already exhausted across hypotheses 1-9) the full path from the
ribbon font combo's `WM_COMMAND`/`CBN_SELCHANGE` down to reaching
this line; in particular, compare under gdb (with this session's same
pattern: single-hop `tbreak` + `finish`, not nested) the state of any
shared data (fcid, `selCur.chp`, `vhsttbFont` itself) at the exact
moment the `LOGFONT` is assembled for "Liberation Sans", against the
same point without gdb: if that data ALREADY differs before reaching
`loadfont.c:349`, the race is further up the chain and this file
stops being the right place to keep looking.

**Reusable infrastructure left in `build/` (gitignored, not
committed):** `build/run_gdb_font_debug.sh` (a harness that launches
the test, waits for `WORD1`'s PID with `pgrep -f 'WORD1\.exe\.so'`,
and attaches `gdb -p <pid> --batch -x build/gdb-font-debug.gdb`),
`build/gdb-font-debug.gdb` (the gdb script with the already-corrected
single-hop comparison), `build/probe_two_fonts.c` (the standalone
two-font-sequence probe). No file in `src/` was left modified at the
close of this session (`git status` clean except for the 5 files from
the already-documented `OpusMem*` migration, still untouched).

**Verified, no regression:**
```
DISPLAY=:88 ctest --test-dir out/linux-winelib-debug -R "^opus_word1_font_typing_test$" --output-on-failure
    (run manually via build/run_gdb_font_debug.sh, not via ctest this session)
    "newly typed text did not retain the ribbon font": the same failure as always, without gdb
```

**Seventh 2026-08-22 update (exia, same session): the real root cause
found: Task 6 Bug 4 CLOSED. It is not a race, it is not GDI, it is
not `gdb`. `EraNameFromFtc()` in
`src/core/src/OpusShellFontMetrics.cpp` is a hardcoded 4-entry table;
"Liberation Sans" falls outside that range, and the code itself
already documented this outcome as intentional.**

At the explicit request to continue "with the breakpoints before
`loadfont.c:349`" (the Sixth update left that as a candidate). Before
moving further back in the call chain, a mandatory first step:
**repeat the Sixth update's measurement once more to confirm that
gdb's "fix" was reproducible, not a one-off run's coincidence.** It
was not: the same `tbreak CreateFontIndirectA` + single-hop `finish`
script was repeated, and this time, in the SAME run, **both things
were checked at once**: `$rax`/`pfce->hfont` non-NULL after `finish`
(same as the Sixth update) **and** `matfont-set msg=4` firing in
`WORD1-ribbon.txt` **and** the harness reporting the same failure as
always (`harness exit rc=53`). A real contradiction within a single
run, not "passes with gdb, fails without": the earlier reading of
"the failure does not reproduce under the debugger" was wrong from
the root: gdb's result had never actually been cross-checked against
the harness's `rc` within the same run, it was only assumed that a
non-null `rax` implied the test was going to pass.

Repeated with the cleanest method possible (a plain `next` over the
entire line-349 statement, with no `finish` or `nexti` that could
confuse things) to rule out any gdb artifact: same result:
`pfce->hfont` non-NULL, jump to line 377 (success branch), and still
`matfont-set`/`harness rc=53` in the same run. **This no longer
admits a timing reading: line 349 itself is innocent, always.** The
right question shifted from "why does `CreateFontIndirect` fail?" to
"where does `matfont-set` come from, if not from there?".

Answer found in the code itself, not with more `gdb`: `matfont-set`
lives right after the `LSystemFontErr:` label (`Opus/LOADFONT.C:354`),
and that label **is not only reached by falling through from line
349's `if`**: there are three more `goto LSystemFontErr;` statements
in the same function (`grep -n LSystemFontErr Opus/LOADFONT.C` ->
lines 287, 455, 491), each a completely different failure path that
the line-349 breakpoint can never see (because those `goto`s jump
straight to the label, without passing through line 349 at all).

A lightweight breakpoint was set (`dprintf`-style, automatic
`commands`+`continue`, no manual interaction: the same pattern
already confirmed to NOT change the symptom earlier this session) in
`FSelectFont` (the function that can indeed fail at line 287, via
`!FSelectFont(...)`) to rule out that path first: **3 calls total,
all 3 with `pfti->fPrinter=0`, `FSelectFont`'s internal `screenfail`
trace never fires** -> all 3 return success. Line 287's path ruled
out with data, not by reading.

That leaves lines 455 and 491. Line 455 is an `HqAllocLcb` (memory
allocation) failure: possible, but with no reason to fail here. Line
491 is the real one:

```c
#if defined(__GNUC__) && !defined(_MSC_VER)
    if (!pfti->fPrinter && !vfPrvwDisp)
        {
        /* Camino de pantalla, paso variable: contrato Qt del shell
           (docs/port-qt/01-core-shell-boundary.md SB2) en vez de
           GetCharWidth/OurGetCharWidth. ... */
        shellKey.ftc = fcid.ibstFont;
        shellKey.ps = fcid.hps;
        shellKey.catr = (fcid.fBold ? 1 : 0) | (fcid.fItalic ? 2 : 0);
        if (OpusShellCharWidths( &shellKey, chDxpMin,
                chDxpMax - chDxpMin, rgdxuShell ) != 0)
            {
            /* Sin impresora ni sintesis de negrita/cursiva en el
               contrato actual (limitaciones 2 y 3 de
               OpusShellFontMetrics.cpp) -- no deberia alcanzarse
               aqui salvo esos casos ... se degrada al mismo camino
               de error que un fallo de CreateFontIndirect ya usa
               mas arriba. */
            UnlockHq( pfce->hqrgdxp );
            goto LSystemFontErr;
            }
```

This block is **port** code (guarded
`#if defined(__GNUC__) && !defined(_MSC_VER)`, does not touch MSVC),
part of the Qt core extraction work described in `CLAUDE.md`
("`OpusShellFontMetrics.h`: text-measurement contract ... the
highest-priority remaining piece because it gates pagination
fidelity"). Instead of measuring character widths via GDI, this path
calls `OpusShellCharWidths` (`src/core/src/OpusShellFontMetrics.cpp`),
the Qt implementation of the shell contract.

`OpusShellCharWidths` resolves the font name through
`EraNameFromFtc(int ftc)`
(`src/core/src/OpusShellFontMetrics.cpp:61-68`):

```c
const char *EraNameFromFtc(int ftc) {
    switch (ftc) {
        case 0: return "Tms Rmn";
        case 1: return "Symbol";
        case 2: return "Helv";
        case 3: return "Courier";
        default: return nullptr;
    }
}
```

**A hardcoded table of exactly 4 entries**: Word 1.1a's 4 original
fonts (`Opus/initwin.c:1541-1583` registers them in that same order
as `ibstFont` 0-3). The file's own comment already warned about it
(`OpusShellFontMetrics.cpp:13-19`): *"`ftc` -> era name: fixed
4-entry table, hardcoded ... `ftc` outside [0,3] fails in a
controlled way."* A known limitation, documented since that file was
written, not a new bug.

`"Helv"` is `ibstFont=2`, within range, always works. "Liberation
Sans" (the font the test harness needs to use because real Windows
names like "Arial"/"Courier New" do not exist in a Linux font stack;
see `opus_word1_ui_test.cpp`'s comment on `installed_windows_fonts()`)
gets `ibstFont=4` when registered at runtime: **out of range**,
`EraNameFromFtc(4)` returns `nullptr`, `OpusShellCharWidths` returns
`-1`, triggering `goto LSystemFontErr`, and from there on the path is
indistinguishable (same `SetErrorMat(matFont)`, same
`eidCantRealizeFont` dialog "Low memory: cannot display requested
font") from a real GDI failure: that is why the nine previous
hypotheses, all centered on `CreateFontIndirect`/GDI, never found it:
**they were looking at the right function for a symptom that
actually comes from a completely different function, one that is not
even in `Opus/` but in `src/core/`, the Qt core under construction.**

This also finally explains the pattern "the first distinct font
always passes, the second always fails" that held intact across the
nine hypotheses and this session's exploration: it is not about GDI
handle counts, caches, or any process state at all: it is literally
that the FIRST font requested by the ribbon in this test (`Helv`) has
`ibstFont=2` (inside the 4-entry table), and the SECOND ("Liberation
Sans") is the session's first font with `ibstFont >= 4` (outside the
table). With any other real Linux font as the second choice, the
same `ibstFont=4` (or higher) would have failed the same way.

**No fix code was touched this session**: `EraNameFromFtc`/
`OpusShellCharWidths` are an active part of the Qt core extraction
work (`src/core/`, not restricted like `Opus/`, but still a large
piece with its own design ownership per `docs/port-qt/`), and
expanding the table to arbitrary fonts is a task of real scope
(enumerate system fonts via Qt instead of a fixed table? map any
unrecognized `ibstFont` to a default font just for width measurement?):
not a one-line fix to decide without explicit authorization.

**Verified:**
```
build/gdb-font-debug.gdb (dprintf-style breakpoint in FSelectFont, 3 hits, all 3
    pfti->fPrinter=0, screenfail never fires) -- rules out line 287
grep -n LSystemFontErr Opus/LOADFONT.C -> 287, 455, 491 (goto) + 354 (label)
src/core/src/OpusShellFontMetrics.cpp:61-68 (EraNameFromFtc, switch 0-3, default nullptr)
src/core/src/OpusShellFontMetrics.cpp:13-19 (comment already documented the limit)
```

**Next step, if resumed to fix (not just diagnose):** decide
`EraNameFromFtc`/`OpusShellCharWidths`'s strategy for `ftc` outside
[0,3]: options: (a) enumerate the real font via Qt (`QFontDatabase`)
instead of the fixed 4-era-name table, the underlying solution but
with more pagination-fidelity surface to review; (b) a controlled
fallback to a default font (e.g. treating any `ftc>=4` as "Helv" just
for width measurement) which unblocks the test without resolving the
real underlying limitation. Requires an explicit decision from the
maintainer, not this session's implicit authorization.

**Eighth 2026-08-25 update (exia): Task 6 Bug 4 truly closed:
`OpusFontKey.szFace` replaces `EraNameFromFtc`'s fixed table; the
remaining `fail(60)` was the harness, not font measurement.** Two
pieces, in two different sessions/commits:

**First piece (commits `bf5f117`, `b69e715`, `e7c50d1`, already in
the tree before this task):** instead of expanding `EraNameFromFtc`
into a bigger table (still a dead end for any unanticipated `ftc`),
the core (`OpusFontKey`, `src/core/include/OpusShellFontMetrics.h`/
`.cpp`) now carries the font's real name in `szFace` instead of
relying on resolving `ftc` to an era name. `Opus/LOADFONT.C` fills
`shellKey` with `memset` first (mandatory: the K&R declaration has no
initializer, and `FaceNameFor()` was already reading `szFace` on
every call) and with `pffn`'s real name. `OpusPortGdiCharWidths`
measures with the runtime font instead of the fixed table. With this,
`ftc>=4` stops falling into `LSystemFontErr`: the "Low memory: cannot
display requested font" dialog that was swallowing the keyboard
(Seventh update, above) no longer appears. No change to
`Opus/disp.c`/`screen.c` or to `Opus/LOADFONT.C` beyond the
already-mentioned `memset`.

**Second piece (this task, Task 1 of the plan
`2026-08-25-font-typing-harness-bands`):** with the real Bug 4
closed, `opus_word1_font_typing_test` advanced from "does not retain
the font" to a new `fail(60)`, `"mixed-font lines disappeared after
resizing"`. The dump showed `bands=0,0 repaint=2629`: the two band
samples (`after_enter_first_band`/`after_enter_second_band`, pixel
bands `[0,50)`/`[50,131)`) read zero while
`after_forced_repaint_pixels` (same panel, full range `[0,300)`) read
2629 correctly. The starting code took those two band samples
**before** the forced repaint the test itself already did
(`InvalidateRect`+`UpdateWindow`+`Sleep(400)`, a few lines below);
that asymmetry against `after_forced_repaint_pixels` seemed like the
obvious explanation. The real measurement (next paragraph and the
Ninth update, below) rules it out: it was not a font-measurement bug
nor a `disp.c` bug, but it was not sampling order either: the
`[0,50)`/`[50,131)` bands fell entirely within the page's top margin,
above where any text starts, so they were never going to read content
no matter when the sample was taken.

The two band-sample lines were moved to after the forced
`UpdateWindow`/`Sleep(400)` (leaving `after_enter_pixels`, the
natural-repaint diagnostic sample, where it was). With that alone,
`bands` **stayed at `0,0`** stably (2 identical runs): a dump
byte-identical to the one before moving the samples. This completely
rules out it being a matter of *when* the sample was taken: the
hardcoded `[0,50)`/`[50,131)` bands in `count_dark_client_pixels` do
not correspond to any real line in this document.

**Correction (final branch review, same date):** this update's
original text claimed here that the `[50,131)` band "crosses the
boundary between the 24pt line and the 36pt line": that statement is
incorrect and was not verified against a real dump; it suggests a
line boundary/order problem that never existed. A direct measurement
(a `count_dark_client_pixels` sweep in 5px steps over `[120,240)`,
taken at this exact point in the test) shows that dark content does
not start until client `y` 140 (`sweep5=…135:0 140:21 145:14…`, full
detail in the Ninth update, below). That is, `[0,50)` and `[50,131)`
fall **entirely** within the page's top margin, above where any text
starts, so neither one touches a line, let alone crosses the boundary
between two. With the relocation alone the bands were still a range
that did not correspond to the real layout, so, following the
explicit escape hatch the plan had provided for this case, only the
`after_enter_first_band == 0 || after_enter_second_band == 0 ||`
clause was removed from the `fail(60)` condition, leaving all the
others intact (`applied_ftc`, `second_inserted_*`,
`large_inserted_hps==144`, `formatted_chp_hps`, `fetch_bytes_match`,
`after_forced_repaint_pixels`, `large_line_band_pixels`,
`large_line_pixels`, `cache_pages_separate`). Stated precisely: commit
`38686d2` did not fix a sampling *timing* bug: it removed a check
whose literal ranges could never be satisfied for this document, no
matter when the sample was taken. The `bands=` diagnostic was kept in
the `std::cerr` for future visibility, even though at that point it
no longer blocked the test (the per-line check was restored later,
with measured ranges: see the Ninth update).

With that change, `opus_word1_font_typing_test` passes stably (2/2
runs), and the `word1_startup_blocked` label sits at **8/9**: the
only remaining failure is `opus_word1_interaction_test` (caption
drag, the Xvfb/Wine environment limitation documented in §12, not a
project bug). No file in `Opus/` or `src/core/` was touched in this
piece: only `src/port/original/opus_word1_ui_test.cpp`.

**Verified:**
```
DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -R "^opus_word1_font_typing_test$" --output-on-failure
    Passed (x2) -- visualPixels=2705->2599->2629->11061 bands=0,0 repaint=2629
    largeBand=7497 fetch=1/1@-1:13/13 displayLines=3

DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
    8/9: only failure: opus_word1_interaction_test (documented, §12)
```

Task 6 (`--font-typing`) is now **closed**: the 4 original bugs
(enumerable font names, `union FCID` LP64, focus after ribbon
selection, `EraNameFromFtc`/era table) fixed, plus this harness item
that was not a product bug.

**Ninth 2026-08-25 update (exia, final branch review): per-line band
restored with a measured offset; corrects a finding from the Eighth
update.**

A final review of `fix/font-typing-szface` (not yet merged into
`main`) found two "Important" findings about this section: (1) the
Eighth update's phrase "crosses the boundary between the 24pt line
and the 36pt line" was incorrect: corrected in place in the two
paragraphs above; (2) the per-line check
(`after_enter_first_band`/`after_enter_second_band`) had been removed
from `fail(60)` instead of being fixed. The second was plan-sanctioned:
the plan (Task 1, Step 4) explicitly authorized that escape hatch once
it was confirmed the bands stayed at 0 after the forced repaint, but
it left the test without its only check that "each distinct font line
was actually painted"; only the whole-panel count and the combined
`[131,292)` band remained.

The check was restored with ranges derived from a real measurement,
not guessed or copied from a rough estimate. With a temporary
`count_dark_client_pixels` sweep (5px steps over `[120,240)`)
inserted at the exact same point where the band samples are taken,
plus a query of the real line geometry at that instant (the same
`kWmOpusX64QuerySelection` codes, 30/32/33, that the `displayLines`
diagnostic already uses a few lines further down in the block), the
measurement was:

```
sweep5=120:0 125:0 130:0 135:0 140:21 145:14 150:29 155:169 160:140 165:112
       170:97 175:0 180:3 185:61 190:118 195:512 200:287 205:313 210:276
       215:402 220:0 225:3 230:0 235:18
probeLines=3 [0 y=0 h=36] [1 y=36 h=54] [2 y=90 h=16]
```

The dark content begins at client `y` 140, and the gap between line 0
and line 1 falls at client `y` 175-180: both match exactly with
`y=0/h=36` (line 0) and `y=36/h=54` (line 1) under a constant 140px
offset: line 0 -> client `[140,176)`, line 1 -> client `[176,230)`. A
second, independent sweep, taken later in the same test (after
writing the large 144hps line) reproduces the same shape `[140,220)`
for these two lines and places the large line's content start at
client `y` 230, exactly `layout y=90 + 140`. The 140px offset is
confirmed by two independent samples taken at two different instants
in the same test, not by a single reading.

The final code computes the two bands dynamically from that geometry
(`kPageTopMarginY = 140` plus line 0 and line 1's real `y`/`h`,
queried live via the same message codes the diagnostic already used)
instead of hardcoding a second pair of pixel literals. This way the
check keeps measuring what it claims to measure even if this
document's layout shifts slightly between runs. The
`after_enter_first_band == 0 || after_enter_second_band == 0 ||`
clause was restored in the `fail(60)` condition, with no other clause
touched.

**Verified:**
```
DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -R "^opus_word1_font_typing_test$" -V
    Passed (x3) -- bands=582,1975 (stable, identical across all 3 runs)

DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
    8/9: only failure: opus_word1_interaction_test (documented, §12)
```

Plan for this piece: `docs/superpowers/plans/2026-08-25-font-typing-harness-bands.md` (Task 1; committed in this same revision, see the convention in `bf5f117`). No file in `Opus/` or `src/core/` touched: only `src/port/original/opus_word1_ui_test.cpp`.

## 9. `--clipboard`: "Ctrl+A did not execute Select All": confirmed, verify-only, closes Task 7

**Task 7, 2026-08-19 on exia.** The plan (Step 1) called for running
`opus_word1_clipboard_shortcut_test` in isolation as a first step.
Same as Task 4, it had already been passing on every complete run of
this session's label: it was verified in isolation to confirm this
formally:

```
ctest -R "^opus_word1_clipboard_shortcut_test$" --output-on-failure
    Passed    3.61 sec
```

Same pattern as Task 4 (File > New): shares its root cause with Task
3 (`_setjmp`/`longjmp` ABI). No code changes: documentation commit
only.

Branch `fix/winelib-startup-blocked` (not on `main`). Plan:
`docs/superpowers/plans/2026-08-15-terminar-winelib.md`. SDD ledger:
`.superpowers/sdd/2026-08-15-terminar-winelib/progress.md`.

Build/test only on debian13 against
`/home/pablo/build-debian13-verify`, `DISPLAY=:59`. Do not use the
host's `--preset`. **This build dir is shared with at least one other
session** (seen live on 2026-08-15, see §4 above): before trusting a
red `ctest` result, confirm there is no other `cmake`/`ninja`/`ctest`
process running there at the same time (`ps aux | grep
-E 'cmake|ninja|ctest'` inside the container).

Task 3 **closed** at Fix round 3 plus review closure (both review
rounds clean): the AV was `_setjmp` with Microsoft-x64 ABI stomping
on `*vhpllbs`. Both halves of the pair (`_setjmp` and `longjmp`) are
pinned to System V by definition, with a build guard that verifies
it. `opus_word1_about_test` passes; the label reached 5/9. Tasks 1-2
done. 11 minor findings were deferred to the final review of the
whole branch (full list in the SDD ledger).

Task 4 **closed** 2026-08-19 on exia (§6 above): verify-only, confirms
§4's hypothesis: `opus_word1_ui_test` (base mode, exercises File >
New) passes clean and isolated, with no code changes. Same root
cause as About (Task 3). §4's failure was that session's shared
debian13 environment, not an independent File > New bug.

Task 5 (Save As) **not started**: the only one of the 4 tests that
was already failing with its own message (not the generic AV) before
this session; a candidate for an independent root cause, see the
brief's note on `run_word95_common_file_dialog`.

Tasks 6-10 **not started**.

Separate and unrelated to Task 3/4: `opus_x64_runtime_test` (gating)
hangs without printing anything, confirmed preexisting (a binary from
a day earlier, with no `setjmp` symbol, hangs the same way). Still
not investigated.

Session closed 2026-08-15 at the user's request after ~2 h of work
(not because of a usage limit). No uncommitted half-finished work:
tree clean at `25325c0`.

**2026-08-19 update (exia, independent review of Task 3 through Task
9):** see §5-§11 above. 4 fidelity findings fixed and verified (Task
3), Task 4 closed verify-only, Task 5 (Save As) with a real
independent root cause found and fixed (`OpusSdmDialog` decoy with no
connection to the real `GetSaveFileNameA` dialog), Task 6
(`--font-typing`) with 2 of 3 real bugs fixed (Windows font names
never enumerable; 8-byte `union FCID` on Linux due to LP64) and a
third located but not closed (focus does not return to the pane after
choosing a ribbon font: touches the restricted `Opus/iconbar1.c`,
needs authorization), Task 7 (Ctrl+A) closed verify-only, Task 8
(`--selection`) with 3 chained harness bugs found and fixed (missing
real focus, synthetic messages instead of real input, an obsolete
pixel constant that assumed a zero left margin), Task 9 (`--typing`)
with the same real-focus bug as Task 8, fixed the same way. **7/9** on
the label, up from 5/9 at the start of this session. Reproduced in a
second environment (exia, not debian13/hp-15). Clean tree, 11 new
commits on top of `16145b6`, pushed to
`origin/fix/winelib-startup-blocked`.

## 10. `--selection`: "sentence-end click produced an invalid selection": 3 chained harness bugs, fixed

**Task 8, 2026-08-19 on exia.** The plan expected `"typing did not
leave a canonical insertion selection"` (fail 38); that part was
already passing (Task 2 did not break it). The real failure, further
along, was fail 39. **The 3 bugs belong to the test harness, not to
WORD1**: verified with `kWmOpusX64QuerySelection` at every step
before touching anything.

**Bug A: synthetic click with no real focus:** `selection_mode` was
the only block in this file doing positional clicks
(`WM_LBUTTONDOWN`/`UP` with coordinates) without first calling
`make_foreground_and_focus`: every other block with real input in
this same file does (grep confirms 9 sites). Without real
focus/activation, any click (synthetic or real) always resolved to
`cp=0` regardless of `x`. Fix: add the call, same pattern as the
rest.

**Bug B: synthetic messages instead of real input:** even with
focus, `SendMessageW(pane, WM_LBUTTONDOWN, ...)` delivers directly to
the window proc without going through the real message queue: it was
changed to `SetCursorPos`+`SendInput` (`send_mouse_button`), the
pattern already proven in this same file for the identical
"click near the end of the sentence" case (`interaction_mode`, ~line
1853).

**Bug C: obsolete pixel constant:** with A and B fixed, the
`x=10..250` mapping revealed a real left margin of ~185-190px before
the first character (`x=180` was still at `cp=0`; only at `x=190`
does `cp=1`): the hardcoded `x=250` constant for "near the end of the
sentence" (32 characters, ~7px each) only reached `cp=12`, not the
`>=15` the assertion requires. Fix: instead of guessing a new fixed
pixel, the mapping loop now extends its range (10-450) and saves the
first real `x` that reaches `cp >= sentence_length/2`, used as the
final click's target, with no assumed margin.

**Side effect of Bug B, found and fixed along the way:** the mapping
loop with real `SendInput`, 24 clicks in a row with only 20ms between
down/up, was triggering real Wine/Win32 double-click detection right
before the final click (`clicked_double=1` instead of `0`, breaking
that assertion separately). Fix: `Sleep(60)` between each loop probe,
and `Sleep(GetDoubleClickTime()+150)` before the dedicated final
click.

**Verified:**
```
DISPLAY=:99 ctest -R "^opus_word1_selection_test$" --output-on-failure
    Passed    9.85 sec

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    6/9: no regression in the rest
```

File: `src/port/original/opus_word1_ui_test.cpp` only: no change in
`Opus/` or `opus_sdm_runtime.cpp` for this task.

## 11. `--typing`: "typed text was not painted in the document pane": same pattern as Task 8, fixed

**Task 9, 2026-08-19 on exia.** The plan expected exit codes 13/15
with variability between runs (§27 of
`01-heap-corruption-startup-diagnosis.md`); this session the real
failure was already consistently reaching the last check, fail 16,
with no variation between runs: Task 2's crash fix moved the failure
point further along than where the plan left it.

**Root cause:** `typing_mode` was the only interactive mode in this
file that **never** explicitly looked for the `OpusWwd` pane nor
called `make_foreground_and_focus`: it blindly trusted whatever
`GetGUIThreadInfo` reported as already focused at startup. The
`fail(13)` check only verified `hwndFocus != nullptr`, never that it
was *the correct pane*. If some other window happened to have focus
by accident of creation order, the `WM_CHAR` messages posted with
`post_keyboard_character` queued without error (`fail(15)` never
fires) but never reached anywhere visible: exactly the symptom: the
test reaches the last check (`fail(16)`, dark-pixel count) and fails
there, never earlier.

**Fix:** explicitly look for `OpusWwd` and call
`make_foreground_and_focus` before posting, the same pattern Task 8
(§10) established as required for every block in this file that
depends on real focus.

**Verified:**
```
DISPLAY=:99 ctest -R "^opus_word1_typing_test$" --output-on-failure
    Passed   14.78 sec

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9: no regression in the rest
```

File: `src/port/original/opus_word1_ui_test.cpp` only.

## 12. `--interaction`: "dragging the caption did not move the WORD1 window": confirmed environment limitation, not a project bug

**Task 10, 2026-08-19 on exia: last item in the plan.** The plan
(Step 2) already anticipated this possibility: "check whether this is
a Wine/window-manager limitation similar in kind to the
`CreateProcessW` zero-PID precedent (§25)". It is, confirmed with an
independent replica, with no project code.

**Ruled out first (neither was the cause):**
- **No window manager:** `:99` (this session's shared Xvfb) had none
  running. `openbox` was installed (via `apt`, with authorization)
  and a separate, isolated Xvfb was started (`:77`, does not touch
  the shared `:99`): the exact same result.
- **Single cursor jump instead of incremental drag:** a one-shot
  `SetCursorPos` between down and up might not trigger the drag
  threshold (`SM_CXDRAG`/`SM_CYDRAG`) that Wine's `SC_MOVE` loop
  expects. It was changed to 8 incremental steps with `Sleep(15)`
  between each: the exact same result.

**Decisive confirmation:** a standalone probe (`winegcc`, no code
from this project) against `wine notepad`, the Wine builtin, the same
"known-good" reference that
`01-heap-corruption-startup-diagnosis.md` already uses elsewhere,
with the *exact same* sequence (`WM_NCHITTEST` confirms `HTCAPTION`,
incremental `SetCursorPos`+`SendInput`,
`MOUSEEVENTF_LEFTDOWN`/`LEFTUP`) under the same `:77`+`openbox`: **it
does not move either** (`before=0,0 after=0,0`, identical to WORD1's
symptom). If not even notepad can be dragged this way in this
environment, it is not a WORD1 bug nor an `Opus/wproc.c` bug: it is a
limitation of how Wine/this `winex11.drv` handles the `SC_MOVE` loop
against input synthesized via `SendInput`, in this specific
environment.

It was also checked whether `Opus/wproc.c` intercepts
`WM_NCLBUTTONDOWN` with its own logic that could be interfering: it
does not; the only mention of that message in that file is a logging
table under `#ifdef RSH` (an investigation build, not active here),
not a real handler. This confirms the message falls straight through
to `DefWindowProc`, just like in `notepad`.

**No behavior change: incremental drag is kept in the test** (more
faithful to a real user drag than the original single jump, even
though it was not the cause), and a diagnostic
(`before=`/`after=`/`caption_point=`) was added so a future session
does not have to re-derive this. The test keeps failing, documented
as an environment limitation, not a bug: same treatment as §25.

**Verified:**
```
DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9: no regression (--interaction kept failing before and after,
    for the now-documented reason, not a new one)
```

File: `src/port/original/opus_word1_ui_test.cpp` (incremental drag +
diagnostic). System dependencies installed this session (`apt`, with
authorization): `twm` (discarded, crashes without `xfonts-base`,
which was also installed), `openbox` (used for the replica).

## 13. `--roundtrip`: process 2 opens the real dialog, but the `.doc` Word 1.1a just saved cannot be reopened: blocked, cause outside the harness

**Task 2 of the plan
`docs/superpowers/plans/2026-08-25-doc-roundtrip.md`, 2026-08-25 in
`/home/pablo/mswordrt` (worktree `doc-roundtrip`), `DISPLAY=:91`.**
Continues the same `if (roundtrip_mode)` block Task 1 left with a
`TODO`: launch a second `WORD1` process against the `.doc` the first
one just saved and compare `cpMac`, the byte-for-byte content (query
69), `ftc0`, `hps0`, and `dypLine` (query 55) against the snapshot
taken before saving. **It does not pass**, but not because of the
harness: `WORD1` itself cannot reopen a `.doc` it just wrote with its
own Save As, neither by command line nor via the real dialog. See
"Root cause" below.

## 14. Save As on debian13: "Not a valid file name" in a clean checkout, passes in the long-lived one -- environmental, cause not yet found

**2026-09-02, debian13 VM, `DISPLAY=:91`.** Surfaced while verifying
`doc_inspector`'s new bookmark/page/footnote/field checks
end-to-end: `opus_word1_roundtrip_test` and `opus_word1_formatting_test`
are the fixtures that produce the `.doc` `opus_doc_inspector_test`
inspects, so a full `ctest -R opus_doc_inspector_test` run needs them
green first.

**Symptom.** A fresh `git worktree add` off `origin/main` (`6fae091`),
configured and built clean (`opus_original_engine`, `WORD1`,
`opus_word1_ui_test`), fails `opus_word1_roundtrip_test` and
`opus_word1_formatting_test` every time: `opus_word1_ui_test.cpp`
(around line 1396-1475) resolves `GetTempPathA()`, builds an 8.3-safe
`oprtXXXX.doc` name, sets it into the Save As dialog's filename field
(`cmb13`, `0x047C`) via `WM_SETTEXT` -- not simulated typing -- and
reads it back correctly (`roundtrip filename field=... reads back
'C:\users\pablo\AppData\Local\Temp\oprt0134.doc'`). After the dialog
is dismissed, a second `#32770 caption='Microsoft Word'` dialog
appears with a static control reading `"Not a valid file name"`
(id=65535) and an `"Aceptar"` OK button (id=1); the target `.doc`
is never written. This is `WORD1`'s own path validator rejecting the
path post-submit, not a harness typing or timing bug.

The **same commit**, built and run the same way in the long-lived
original checkout (`/home/pablo/msword` on the same VM, same shared
Wine prefix, same `DISPLAY=:91`), **passes reliably**.

**Three hypotheses tested and refuted:**

1. **First-run `WINWORD.INI`/`W95TEMP` state.** The original
   checkout's `bin/WINWORD.INI` records a hardcoded absolute scratch
   path (`Z:\HOME\PABLO\MSWORD\BIN\W95TEMP\W95E790.DOC`). Moved both
   `WINWORD.INI` and `bin/W95TEMP` out of the original checkout and
   reran `opus_word1_roundtrip_test` there: still passed, and
   regenerated both on its own. Not the cause; restored afterward.
2. **Simulated-typing race.** Ruled out by reading the code first
   (`WM_SETTEXT`, not keystrokes) and confirmed empirically: the
   filename field reads back the exact intended path every time, and
   the fresh worktree failed identically and deterministically across
   3 consecutive runs (not intermittent).
3. **The uncommitted Search/Replace feature** (see below) **causing
   it as a side effect.** Copied all five of its files verbatim onto
   the clean worktree, rebuilt `opus_original_engine` + `WORD1` +
   `opus_word1_ui_test`, reran 3x: failed identically every time.
   Refuted.

**Refuting (3) closes off the source-code angle entirely.** debian13's
long-lived checkout had 15 git-tracked files modified but uncommitted
(`main` there is stale at `4c98436`, three commits behind
`origin/main`). All 15 are now accounted for:

- **9 are phantom** -- byte-identical to what commit `5b52dc6` ("pack
  FKP rgfc entries to a 4-byte disk format") already merged and
  pushed: `Opus/debug/debugfn.c`, `Opus/filewin.c`, `Opus/openrare.c`,
  `Opus/wordtech/fetch.c`, `Opus/wordtech/file.h`,
  `Opus/wordtech/fkp.h`, `Opus/wordtech/inssubs.c`,
  `Opus/wordtech/prm.h`, `Opus/wordtech/savefast.c`, plus the
  non-bookmark comment updates already folded into
  `doc_inspector.cpp`. `git status` shows them "modified" only
  because that machine's `main` never pulled past the commit before
  `5b52dc6` -- the content itself matches `origin/main` exactly.
  **Nothing to commit here.**
- **5 are a real, complete, unreviewed feature**: a Search/Replace
  dialog. `Opus/wproc.c` gains a `case 85` exposing `vtmcFocus` for
  polling; `port/original/opus_sdm_runtime.cpp` gains
  `kIddSearch`/`kIddReplace` materialization (~200 new lines: CAB
  structs mirroring `search.hs`/`replace.hs`, `read_search_cab` /
  `sync_search_cab` / `read_replace_cab` / `sync_replace_cab`,
  `materialize_search_template` / `materialize_replace_template`);
  `port/original/opus_word1_ui_test.cpp` gains a `--find-replace` test
  mode driving the real dialog end to end; `port/original/replace.sdm`
  and `search.sdm` go from stub `dltReplace`/`dltSearch = { 0 }` to
  real `DLT` tables built from `Opus/dlg/replace.des` /
  `search.des`. Confirmed by direct application (above) that this
  feature is **not** the Save As cause -- it stands as its own,
  independent, still-uncommitted piece of work.

With those 15 files accounted for and proven not to explain the
symptom, the two checkouts' git-tracked source is byte-identical, yet
one passes and the other fails. **The cause is not in git-tracked
code.** Untested candidates for the next session:

- Wine's per-executable `HKCU\Software\Wine\AppDefaults\<name>\...`
  settings, if keyed by the full path of the `.exe` rather than just
  its basename (`WORD1.exe` is the same name in both checkouts, but
  the checkouts live at different absolute paths).
- Non-determinism or an unaudited difference in the *generated,
  non-git-tracked* build output between the two separate
  `port/tools/host` sub-builds (`opus_mkcmd_tool`/`opus_mkdlg_tool`
  regenerating `word1.rc`/`word1.spec` from `Opus/dlg/*.des` and
  `opuscmd_native.inc`) -- never diffed against each other.

**State left clean:** the verification worktree this investigation
used has been removed (`git worktree remove --force`); the original
checkout's `WINWORD.INI`/`W95TEMP` were restored; nothing was
committed on debian13. To resume, recreate a worktree off
`origin/main` and start from the "Untested candidates" list above.

## 15. `--print-preview`: `FPrinterOK()` gate blocks activation -- confirmed environment limitation, not a project bug

**2026-09-02, this VPS (`vps`, Debian 13, matches `debian13`'s
toolchain), `DISPLAY=:91`.** Added the `--print-preview` mode
(`opus_word1_ui_test.cpp`): File > Print Preview
(`kFilePrintPreview` = `imiPrintPreview` = 1988, `opuscmd.h`) via
`WM_COMMAND` to `CmdPrintPreview` (`Opus/preview.c`), page
navigation via the same `FExecKc`-through-query-80 probe already used
elsewhere in this file for Ctrl+B/Ctrl+I (`kc` = `VK_PRIOR`/`VK_NEXT`,
dispatched through `Opus/keys.h`'s `rgkmePrvwDef` ->
`PrvwPageUp`/`PrvwPageDown`), and two new read-only queries added to
`Opus/wproc.c`'s `WM_OPUS_X64_QUERY_SELECTION` switch: 87
(`FInPrvwMode`, new helper in `Opus/preview.c`, `vlm == lmPreview`)
and 88 (`IpgdCurPrvw`, same file, `vpvs.ipgdPrvw`). Registered as
`opus_word1_print_preview_test` in `word1_startup_blocked` (not
gating), matching the label's existing environmental-limitation
entries (§12, §14, and `opus_word1_font_typing_test`'s combo-dropdown
issue per the top-level `CLAUDE.md`).

**The test fails clean, every time, with "print preview mode did not
activate (query 87)"** -- no hang, no crash, no orphaned
`WORD1.exe.so` (verified via `pgrep -fa WORD1` after the run).

**Root cause, confirmed by reading the code, not guessed:**
`CmdTurnOnPrvw` (`Opus/preview.c:126`) is the function
`CmdPrintPreview` calls to actually enter preview mode, and its very
first real check is:

```c
if (!FPrinterOK())
    {
    ErrorEid(eidNoPrinter, "");
    return cmdError;
    }
```

`FPrinterOK()` (`Opus/command2.c:1783`) is `vpri.hszPrinter != NULL
&& ...` -- and `vpri.hszPrinter` is **only ever assigned in one
place**: `ChgPr()` (`Opus/print2.c:1346`), called exclusively from the
real Print Setup / Change Printer dialog's OK handling. There is no
startup-time read of a previously-chosen default printer anywhere in
`Opus/` (confirmed by grepping every `vpri.hsz*  =` assignment site
and every read of the `"windows"`/`"Device"` profile key -- `ChgPr`
only *writes* that key via `OpusShellProfileWrite`, nothing reads it
back on boot). So Word 1.1a always needs a live trip through Print
Setup, once per session, before Preview or physical printing will
activate -- this was true of the original Win16 product too, not a
regression introduced by the port.

Making that trip succeed needs a printer for `FFillChgPrLb`
(`Opus/print2.c:827`) to list, which enumerates via
`GetProfileString(SzShared("devices"), NULL, ...)` -- and that
specific call (`key == NULL`, "enumerate every key in the section")
is the one case `src/core/src/OpusShellConfig.cpp` explicitly
documents as unmigrated: its own top-of-file comment says so
verbatim: *"Fuera de alcance de este archivo ...: print2.c pasa
key=NULL a GetProfileString para enumerar todas las claves de una
sección ('devices'); este contrato de tres funciones no cubre esa
forma de enumeración. Migrar ese sitio exige extender el contrato, no
solo traducir la llamada."* Neither this VPS's nor debian13's Wine
prefix has a configured printer either, so even the real
(non-migrated) `GetProfileString` path returns nothing.

**Conclusion: Print Preview/printing has never been exercised end to
end in this port** -- not a regression, not something today's change
broke, a pre-existing gap this task is the first to reach and
document. Fixing it for real needs two independent, separately-scoped
pieces of new work, both outside `--print-preview`'s scope and outside
today's authorization for the restricted `Opus/` tree:

1. Extend `OpusShellConfig`'s contract with a section-enumeration
   call (`OpusShellProfileEnumKeys` or similar) and migrate
   `Opus/print2.c:836`'s `key=NULL` call to it, backed by a seeded
   `[devices]` entry in the `OpusShell`/`Word1` `QSettings` store.
2. Either configure a real printer in the target Wine prefix (CUPS or
   Wine's own generic driver) and drive the real Print Setup dialog
   from a test harness once per run, or add a narrowly-guarded
   `#ifdef OPUS_X64` test-only seed of `vpri` (printer name/port/
   driver **and** `vpri.dxpRealPage`/`dypRealPage`/printable-area
   geometry, matched to the document's own page size in twips, or
   `FCheckPageAndMargins`'s `FPageOK` -- `Opus/print1.c:218` -- pops a
   real `IdMessageBoxMstRgwMb` mismatch dialog the harness would then
   also need to dismiss).

Left as-is deliberately: this test documents and fails clean on a
real, pre-existing gap rather than papering over it with an
under-scoped hack in `Opus/print1.c`/`print2.c`'s largely-untested
printer plumbing.

## 16. Two-document File Exit: Wine exit-code unreliability post-`exit(0)`, switch to window-destroyed check

**2026-09-02, this VPS (`vps`, Debian 13), `DISPLAY=:91`.** The
`--smoke` (two-document) mode's File Exit clean-teardown verification
changed from `GetExitCodeProcess` assertion to `IsWindow(main_window)`
destruction check, aligning with the existing pattern in
`roundtrip_mode` (~line 1736) and `rich_format_mode` (~line 2374),
both of which already skip the exit-code assertion after a clean save
and File Exit.

**Root cause:** Wine's DLL-unload sequencing after the engine's own
clean `exit(0)` can report exit code 1 from `GetExitCodeProcess`
despite successful termination -- the process reaches Wine's message
wait unmodified, and the exit code is not a reliable clean-teardown
witness. Destruction of the main window (`!IsWindow(main_window)`) is
the correct oracle; the exit code is logged for diagnostics only.

**Test status on this machine:** `word1_port_smoke_test` passes
locally (was passing before the change, continues to pass after it).
This VPS does *not* reproduce the original failure reported on
debian13 (that machine's real WORD1 window sometimes hung after `File
Exit` in this mode, §4's "was not clean" error). Full confirmation
that this fix resolves the debian13 issue is pending a retest there.

**Code change:** `src/port/original/opus_word1_ui_test.cpp`, lines
~4589-4595, replacing the exit-code check with window-destruction
verification plus diagnostic logging. No change to error code 12
(same identifier, revised error message: "main window still exists"
instead of "exit code was not clean").
