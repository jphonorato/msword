# Diagnosis: heap corruption on WORD1 startup (Fedora 44)

**Date:** 2026-08-12 · Fedora 44, GCC 16.1.1, wine-staging 11.0, `gdb` 17.2 and
`valgrind` 3.27.1 (installed in this session, with explicit authorization,
for this task).

**Actual status: reproduced consistently, with two distinct corruption
signatures depending on the run; crash point symbolized with
`addr2line` against the binary itself; exact origin of the corruption NOT
isolated, blocked by two toolchain problems independent of the bug
itself (see below). Pure diagnosis: no code file under `src/Opus/`,
`src/OpusEtAl/`, or `src/port/` was touched in this task.**

Prerequisite resolved before starting: this session's `HEAD` did not
link `WORD1.exe.so` (`opus_shell_spine` was missing from `WORD1`'s link,
a gap left by commit `ea5f908`, see
`01-core-shell-boundary.md`, section "Cross-verification on Fedora 44").
Fixed with one line in `src/CMakeLists.txt` (adding
`target_link_libraries(WORD1 PRIVATE opus_shell_spine)` plus the matching
`IMPORTED` block, same pattern as the other 3 core libs) and
rebuilt. The binary diagnosed here (`bin/WORD1.exe.so`,
mtime 2026-08-11 23:57 CLT) corresponds to this session's real `HEAD`, not
an old copy.

---

## 1. Reproduction, two corruption signatures, not one

**Run under `gdb` (repeated 5 times, same result all 5):**

```
$ gdb -q --batch -ex "run" -ex "bt full" --args wine WORD1.exe.so
[...]
0024:fixme:dwmapi:DwmSetWindowAttribute (0000000000010086, 22, 000000000010789C, 4) stub
malloc(): invalid size (unsorted)

Program received signal SIGABRT, Aborted.
0x00007ffff7e0bccc in ?? ()
```

**Direct run (`wine WORD1.exe.so`, no debugger, repeated 2 times):**

```
$ wine WORD1.exe.so
[...]
0024:fixme:dwmapi:DwmSetWindowAttribute (0000000000010086, 22, 000000000010789C, 4) stub
free(): invalid next size (normal)
0024:fixme:dbghelp:sparse_array_add re-adding an existing key
0024:err:msvcrt:_wassert (L"NULL != abbrev_entry",L"dlls/dbghelp/dwarf.c",465)
Assertion failed: NULL != abbrev_entry, file dlls/dbghelp/dwarf.c, line 465
```

**Reading:** two distinct glibc messages (`malloc(): invalid size
(unsorted)` vs. `free(): invalid next size (normal)`) for the same
startup sequence, without changing a single line between runs. This is the
characteristic signature of real heap corruption (an out-of-range
`write` damages a `malloc` chunk's metadata), not of a
deterministic logic bug: the exact point where the allocator *detects*
the damage depends on the heap's internal state at that moment (which chunk
coalesce/split happens first), which in turn depends on timing and on
which tracing mechanism is active (gdb slows down and serializes execution,
valgrind more so). Both point to the same fact regardless: **heap
memory is being written outside the bounds of its block before `malloc`/
`free` notices it.**

In every case the startup path up to the failure is identical:
wine-staging's `loader_init`, a `DwmSetWindowAttribute` stub, and
then the corruption. Consistent and reproducible, not intermittent in
the sense of "sometimes it doesn't happen"; it always happens, only *how* it
is announced changes.

---

**QUALIFIED, see §12 (point 2).** The central symptom of this section
(`info sharedlibrary` empty) has a sufficient alternative explanation
(reading it after the inferior has already exited) confirmed in an environment without
`wine-preloader`. The attribution of cause to `wine-preloader` made here (on
Fedora 44/wine-staging 11.0, where that binary does exist) was not tested
again or refuted in this session: it remains the most likely explanation
*in that specific environment*, but it is no longer the only possible
explanation for the observed symptom.

## 2. `gdb`: the trace is not usable, an isolated cause, not the bug

`bt full` after the `SIGABRT` does not give a real stack:

```
#0  0x00007ffff7e0bccc in ?? ()
#1  0x2074616820646572 in ?? ()
#2  0x6d65732074786574 in ?? ()
#3  0x0000000000000000 in ?? ()
```

Frames `#1`/`#2` are garbage (they read as arbitrary ASCII text if the
bytes are decoded, they are not real return addresses). Investigated
in depth before dismissing it as noise:

- `info symbol $pc` gives `No symbol matches $pc`, **despite**
  `info proc mappings` confirming that `$pc` (`0x7ffff7e0bccc`) falls within
  the loaded range of `/usr/lib64/libc.so.6` (`0x7ffff7d97000`-`0x7ffff7f88000`).
- `info sharedlibrary` returns **empty**, no shared library
  registered at all, not even `libc.so.6` or `ntdll.so`, despite being mapped.
- Real debug symbols were downloaded via `debuginfod` (`set
  debuginfod enabled on`): 6.75 MB of `libc.so.6`, 2.76 MB of `ntdll.so`,
  plus `libunwind`. Nothing changed; the problem is not missing symbols,
  it is that gdb never associated any library object with those addresses.
- A rescan was forced with the `sharedlibrary` command after the signal:
  same result, empty.

**Cause identified, not merely suspected:** `wine-preloader` (the actual
binary that `wine` runs first, visible in the trace as `process NNNNN
is executing new program: /usr/lib64/wine-wow64/wine/x86_64-unix/
wine-preloader`) is Wine's own ELF loader that manually maps
`ntdll.so`, the system libraries, and itself **without going through
glibc's standard `_dl_debug_state`/`r_debug` protocol**; it reserves the
low address space by hand for Win32 layout compatibility before any
normal dynamic loader runs. `gdb` (through
`solib-svr4.c`) depends exactly on that protocol to populate `info
sharedlibrary`. Since it never activates the way `gdb` expects, symbol
resolution for *the whole process* (not just `WORD1.exe.so`) is
broken, regardless of whether debug symbols are
available. **This is a known limitation of debugging Winelib binaries with
vanilla `gdb`, not a defect of this build or of this task**; the path
recommended by the Wine project itself is `winedbg --gdb`, tried in
the next section.

---

**QUALIFIED, see §12.** Not re-verified or reproduced again in this
session: the environment used here (Debian 13/wine-10.0/GCC 14.2.0) is not
the one from this section (Fedora 44/wine-staging 11.0/GCC 16.1.1), and
`winedbg` was not tried. Left as is, neither refuted nor confirmed.

## 3. `winedbg --gdb`: blocked by a Wine bug, not a build issue

`winedbg` is the bridge designed for exactly this case (it understands
Wine's PE + ELF layout, and exposes a `gdb`-compatible proxy). It fails
before it gets to run anything:

```
$ printf 'c\nbt 20\n...' | winedbg --gdb "$(pwd)/WORD1.exe.so"
WineDbg starting on pid 0130
0130:0134: create process 'Z:\home\exia\word1\msword\bin\WORD1.exe'/0000000000000000 @00007F4E81713670 (0<0>)
012c:fixme:dbghelp:sparse_array_add re-adding an existing key
012c:err:msvcrt:_wassert (L"NULL != abbrev_entry",L"dlls/dbghelp/dwarf.c",465)
Assertion failed: NULL != abbrev_entry, file dlls/dbghelp/dwarf.c, line 465
```

**Wine's own `dbghelp` crashes** trying to read the DWARF of
`WORD1.exe.so`, before even reaching `run`/`continue`. It is the same
`assert` already seen in the direct run without a debugger (§1): it is not
a side effect of `winedbg`, it is `dbghelp.dll` (the same library used by
**the project's own startup probe**,
`port/original/opus_original_startup_probe.cpp`, via `SymInitialize`/
`SymFromAddr`/`StackWalk64`) failing to parse the DWARF that GCC 16
generates for this binary.

**This explains, with evidence and not just suspicion, an important
side finding:** why `WORD1-crash.txt` (the log the project's own probe
writes on every startup, see `build/WORD1-crash.txt`) **never
has function names or line numbers, only raw `WORD1+0xoffset`.** It is not
a limitation of the probe or of `dbghelp` in general: it is that Wine's
`dbghelp.dll` cannot parse the DWARF format GCC 16 emits for this
binary, so any attempt to symbolize at runtime (the probe, `winedbg`)
is blind. `addr2line` (offline, outside Wine, §5) can read it, it is
a problem with Wine's parser, not with the DWARF content itself.

Not investigated further (out of scope for this task: it is a Wine
bug, not a port bug). Worth reporting upstream if the probe ever
needs to symbolize live.

---

## 4. `valgrind`: two attempts, neither gives a usable signal

**Without `--trace-children`:** valgrind only instruments the `wine`
supervisor process, which does an `exec` into the real binary without
valgrind following it; zero errors reported because it never actually saw
the process that really corrupts memory:

```
$ valgrind --error-exitcode=99 --track-origins=yes wine WORD1.exe.so
==47068== Memcheck, a memory error detector
==47068== Command: wine WORD1.exe.so
==47068== Parent PID: 47067
[process terminates, exit=0, no errors -- because it never tracked the child process]
```

**With `--trace-children=yes`:** it breaks the address-space reservation
that `wine-preloader` needs to make for the Win32 layout, before even
getting to load `WORD1.exe.so`:

```
$ valgrind --trace-children=yes --track-origins=yes wine WORD1.exe.so
preloader: Warning: failed to reserve range 0000000000110000-0000000068000000
wine: dlls/ntdll/unix/virtual.c:3704: virtual_init: Assertion `view_block_start != MAP_FAILED' failed.
```

This is a known incompatibility between Valgrind's own memory
manager and `wine-preloader`'s low-address-space reservation for 64-bit
processes; it is not specific to this build. No further Valgrind flags
were tried (`--soname-synonyms`, manual reservations) since that would be
out of scope for this task's "diagnosis only, fix nothing": they are
Wine/Valgrind environment tweaks, not the bug.

---

## 5. What did work: `addr2line` directly against the binary's DWARF

Without going through Wine's loader or through `dbghelp`, reading the
`WORD1.exe.so` ELF (not stripped, with `debug_info`) directly:

```
$ addr2line -e WORD1.exe.so -f -C -i 0x1FD57C
N_FormatLineDxa
/home/exia/word1/msword/src/port/original/opus_asm_resn2_adapters.cpp:185
```

The project's own probe log (`build/WORD1-crash.txt`, written in this
session, from a real startup under `wine` without a debugger) gives:

```
Exception 0xC0000005 at 0x00006FFFFFC1B75F
Access violation: write at 0x0000000000000000
unwind #0 0x00006FFFFFC1B75F rsp=0x105DE0
unwind #1 WORD1+0x1FD57C rsp=0x105DE8
unwind #2 0x00006FFFFFC0EE53 rsp=0x105DF0
stack+0x0: WORD1+0x1FD57C
[...]
```

The three `unwind #N` entries are the only frames `StackWalk64` (used by
the probe) could actually verify; the rest (`stack+0xNN`) is a
heuristic sweep of the stack for values that fall inside `WORD1`'s
range, useful as a lead, not as proof of a real call sequence.

**Verified frame #1, symbolized, the concrete finding of this
task:**

```
WORD1+0x1FD57C → N_FormatLineDxa
  src/port/original/opus_asm_resn2_adapters.cpp:185-188:
      int N_FormatLineDxa(int ww, int doc, long cp, int dxa) {
          C_FormatLineDxa(ww, doc, cp, dxa);
          return 0;
      }
```

It is a one-line native adapter that calls straight into
`C_FormatLineDxa`, the real line-formatting engine (pagination, the
same subsystem documented in B1.3/B2 of `01-core-shell-boundary.md`).

**Frame #0** (the exact point of failure, a `write` to `0x0`) is the raw
address `0x00006FFFFFC1B75F`, **which does not fall inside `WORD1`'s
range** (no `WORD1+` prefix), so it is code from some other loaded
module (most likely a Wine DLL, `user32`/`gdi32`/`ntdll`, given the
address range). It could not be symbolized in this task: `addr2line`
only has the DWARF for `WORD1.exe.so`, not for Wine's `.so` files, and
`winedbg`/`dbghelp` (which would know how to resolve that) is broken (§3).

**Overall reading, with explicit caution about what is hard evidence and
what is a reasonable interpretation:** `N_FormatLineDxa` calls
`C_FormatLineDxa`, and the failure occurs *inside* that call, in code
outside `WORD1`; consistent with (not proven as) a call through a
damaged function pointer, or a write through an already-freed/moved
pointer that points to memory outside the process or to an unmapped page
at `0x0`. This fits the architecture pattern documented in
`docs/port-linux/00-reconnaissance.md` §1.7: the engine resolves commands
dynamically via `GetProcAddress` over tables generated by MKCMD, so a
corrupt function pointer in that area (dispatch table, not the stack)
would produce exactly this shape of failure. **This is a reasoned
interpretation, not proof**; the `write` that corrupts the heap itself was
not isolated, only its consequence a few steps later.

**Heuristic trail** (same method, `stack+0xNN` offsets, not
verified by `StackWalk64`, treat as a lead about the code area,
not a confirmed call sequence):

| Offset | Symbol | Location |
|---|---|---|
| `0x4C727B`, `0x457EFD`, `0x3AC0D8` | *(no symbol, code external to `WORD1`, not resolved)* | -- |
| `0x200FC2`, `0x200CB3` | `configure_word95_menus` | `opus_win95_chrome.cpp:425`, `:381` |
| `0x1FF0DF` | `NatRulerMarkWndProc` | `opus_asm_wproc.cpp:187` |
| `0x1FDD0C` | `N_FillIfldFlcd` | `opus_asm_native_adapters.cpp:286` |
| `0x1F368C` | `MyResetRepeatWord` | `generated/lowercase-c/spell.c:1747` |
| `0x1F31CE` | `WDDLBoxSpellMDict` | `generated/lowercase-c/spell.c:1596` |
| `0x1F3A51` | `TmcGosubSpellMM` | `src/Opus/spelcore.c:150` |
| `0x1FAF81` | `PlaybackHook` | `opus_asm_misc.cpp:260` |
| `0x1EF77B` | `CmdUndo` | `src/Opus/wordtech/undo.c:206` |
| `0x1FAA16` | `invoke_macro_ints_n` | `opus_asm_misc.cpp:81` |
| `0xE89A1` | `ExecEndProc` | `src/Opus/interp/elcore.c:3643` |
| `0x2DE5DA`, `0x2DE881` | `InvokeValue<...>` (template) | `opus_elx_dispatch.cpp:34`, `:27` |

Read from bottom to top (higher stack offsets meaning outer calls, if
the heuristic sweep reflected the real stack, which is not
guaranteed): the visible pattern (macro/ELX dispatch via `InvokeValue`
templates, passing through `ExecEndProc`, `CmdUndo`, spell-dictionary
routines, Win95 menu configuration, and ending in line formatting) is
compatible with a real startup sequence (menu initialization → dictionary
→ undo → native macro → formatting), but **again: this is a sweep of
text matches on the stack, not a confirmed stack.** Reported as a lead
to narrow down where to look, not as a proven execution path.

---

## 6. Origin of the corruption: not isolated in this task

**The exact instruction that writes out of bounds was not identified.**
What is established with real evidence:

1. There is real, reproducible heap corruption (glibc detects it, with two
   distinct messages depending on the run, §1).
2. The point where the damage manifests as an access violation (not the
   origin) is in code external to `WORD1`, reached from
   `N_FormatLineDxa`/`C_FormatLineDxa` (§5), a `write` to `0x0`.
3. The two standard avenues for going further (`gdb`, `valgrind`) are
   blocked by environment/toolchain incompatibilities **independent of
   the bug**: Wine's own loader breaks `gdb`'s symbol tracking (§2), and
   `dbghelp`/`winedbg` cannot parse the DWARF from GCC 16
   in this binary (§3), and `valgrind --trace-children` collides with
   `wine-preloader`'s memory reservation (§4).

## 7. Suggested next steps, not implemented, pending a decision

- **ASan.** It is the most direct candidate for isolating the real
  `write`: a build with `-fsanitize=address` would catch the
  out-of-range write at the exact moment, not several steps later as
  now. Requires reconfiguring the build (`CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS`
  for the `WORD1`/`opus_original_engine` target, or a new preset),
  **not applied in this task**, as agreed; proposed and awaiting
  confirmation. Known risk beforehand: ASan and Wine's own SEH/exception
  handling do not always coexist well (ASan's signal handler can clash
  with Wine's), it would need to be tried and one should be prepared for
  a different signal than expected, not assume it "just works".
- **Symbolize frame #0.** Finding which Wine module covers the range of
  `0x00006FFFFFC1B75F` (via `addr2line`/`nm` against the installed
  Wine `.so` files, since `winedbg` is not usable) would narrow down
  whether the jump lands in `user32`/`gdi32`/`ntdll`, which would help
  decide whether the corrupt pointer is a command table (§1.7 of
  `00-reconnaissance.md`), a window callback, or something else.
- **Review `C_FormatLineDxa` and its neighborhood in `Opus/wordtech/`**
  by hand (no dynamic tools) looking for unbounded index/size writes
  around structures that depend on `long`/`DWORD` being 8 bytes under
  LP64, the same bug pattern that already caused real, confirmed damage
  in BITAPP (`00-reconnaissance.md`, Phase 1, `bitapp.h:29`).
  Not done in this task since it is already root-cause analysis, not
  symptom diagnosis.
- **Report the `dbghelp.dll` assert upstream to Wine**, if the project
  ever comes to depend on the probe symbolizing live; today it is a
  tooling blocker, not a port blocker.

---

**QUALIFIED, see §12 (point 4).** The ASan result itself (steps 1-2,
Fedora, with `wine-preloader` genuinely present) was not called into
question. What is qualified is the closing generalization ("third of
three dynamic tools blocked by the same friction"): a fourth dynamic
instrument (glibc's heap checker) does map and does run under Wine in
an environment without `wine-preloader`, with the caveat that its
diagnostic value for this specific bug is limited (see §12).

## 8. ASan, ruled out, not just proposed (2026-08-12)

The §7 candidate was tried in isolation (a trivial Winelib binary in
`/tmp`, without touching `out/linux-winelib-*` or any code file) before
investing in reconfiguring the real build.

**Step 1, `winegcc` does not propagate `-fsanitize=address` to the final
link.** Its driver builds the link command itself and drops the flag: the
`.o` compiles instrumented, but the final link (`gcc -m64 -shared ... -ldl -lm
-lc`, without `-fsanitize=address` or `-lasan`) leaves `__asan_init`/
`__asan_version_mismatch_check_v8` unresolved (`ld: undefined
reference`, `winegcc: /usr/bin/gcc failed`, exit code 2).
Confirmed with `winegcc -v` showing the actual link command emitted.
Resolved by passing `-lasan` explicitly (`-Wl,--no-as-needed -lasan
-lpthread -ldl -lrt -lm`): the binary links and produces `t.exe`/`t.exe.so`.

**Step 2, it runs under `wine` with `LD_PRELOAD=libasan.so`, and fails
during ASan's own initialization, before reaching `main`:**

```
==PID==ERROR: AddressSanitizer failed to deallocate 0xc800 (51200) bytes
at address 0x00007ffb3800 (error code: 22)
```

Four combinations of `ASAN_OPTIONS` were tried
(`allocator_may_return_null=1`, `protect_shadow_gap=0`,
`verify_asan_link_order=0`, and all four together) plus
`WINEPRELOADRESERVE=0x0-0x0` (an attempt to disable `wine-preloader`'s
low-address-space reservation), same error in all five cases, no change
in the message or the return code.

**Reading:** it is the same class of incompatibility that already
blocked `valgrind --trace-children` in §4, an external memory manager
that reserves/unmaps its own address space clashes with the reservation
`wine-preloader` makes ahead of time for the Win32 layout, before any
library constructor runs. This is not a bug in ASan or in
this build: it is the third dynamic tool out of three tried
(`gdb`/`winedbg` in §2-3, `valgrind` in §4, now ASan) blocked by the
same kind of Winelib/x86-64 toolchain friction, not by the bug itself.
**Ruled out as an avenue, the real build was not reconfigured, it is not
left as "still to try".**

## 9. Manual review of `C_FormatLineDxa`, started, no root cause isolated yet

First stretch of the last item of §7 ("review `C_FormatLineDxa` and its
neighborhood by hand"). Scope covered in this session, with evidence:

- **The `CwFromCch`/`FChngSizeHCw`/`HAllocateCw` macros that govern the
  `vhgrpchr` buffer (the `CHR`/`CHRV`/`CHRT`/... run table that
  `C_FormatLineDxa` fills) were verified to be consistent; they are not the
  bug.** `CwFromCch(cch)` (`wordtech/word.h:196`) uses `sizeof(int)`,
  4 bytes both in the original Win16 (`int` of 2 bytes, so the real "cw"
  unit there is 2 bytes) and in this GCC x64 build (`int` of 4 bytes,
  so a "cw" unit of 4 bytes). The original macro `heap.h:84`
  (`FChngSizeHCw(h,cw,f) = FChngSizeHCb(h,(cw)<<1,f)`, assumes a 2-byte
  unit) **is not used in this build**: `heap.h:3-5` takes the
  `#ifdef OPUS_X64` branch and only includes `opus_x64_heap.h`, whose
  version (`FChngSizeHCw(h,cw,shrink) = OpusFChngSizeHCb(h, cw*sizeof(int),
  shrink)`) multiplies by 4, not by 2, consistent with the new unit of
  `CwFromCch` under a 4-byte `int`. No mixing of the two definitions
  confirmed by direct reading of `heap.h`. The growth pattern in
  `FExpandGrpchr` (`fetch1.c:838-853`, +25% with `cbCHRE` headroom) and
  its single relevant call site were also read in full, no sign of
  a size mismatch.
- `cbCHR`/`cbCHRT`/`cbCHRV`/`cbCHRDF`/`cbCHRFG` (`wordtech/format.h`) are
  all `sizeof(struct ...)` computed by the compiler itself, unlike
  the already-confirmed bug in `bitapp.h:29` (a fixed `DWORD` serialized
  against an external file format), this table is internal and
  self-consistent: there is no fixed format constant in between that
  could get misaligned under LP64. Ruled out as a bug class for this
  specific buffer.
- The ~15 points where `vbchrMac`/`bchrPrev`/`ffs.bchr` are compared
  against `vbchrMax` before indexing `(**vhgrpchr)[...]` (a full grep
  of `format.c`) are all guarded (`if ((vbchrMac += cbCHR...) <=
  vbchrMax ...)` or equivalent), no missing guard was found in
  this pass, but **each branch was not verified by hand line by line**,
  only the general guard pattern.

**Not yet covered, still pending:** the full body of
`C_FormatLineDxa` (`wordtech/format.c:454-1206+`, the function is more
than 600 lines) was not read in full; the stretch reviewed was the
initialization (454-750) and the run-buffer allocation subsystem.
The main body of the line-formatting loop is missing (character-width
measurement, tabs, fields/formulas, breaks) where the real call to
GDI/text measurement that the §5 crash reaches outside `WORD1` lives.

**New lead, not yet pursued, that changes the search angle:** this
branch of the project has been progressively wiring `WORD1` to the
`OpusShell*` contracts (recent commits: `00de60f2` connects
`C_LoadFcid`, on-screen character-width measurement, to
`OpusShellCharWidths`; `b803cc0` wires `opus_shell_font_metrics` into
`WORD1`). If the `C_FormatLineDxa` loop still calls real GDI
(`vpri.hdc`/`vfti`, screen HDC) for something `C_LoadFcid` already
resolves through the Qt contract at another point in the same
formatting flow, a null HDC or an inconsistent font state between the
two paths would fit "write to 0x0 in code outside `WORD1`" (§5) without
requiring prior heap corruption as the only explanation. **Not
confirmed, this is the next hypothesis to test, not a finding.**
`vpri.hdc` itself turned out to be only the printer HDC (`disp1.c`,
`print.c`, `initwin.c`) based on reading its assignments; it is not the
screen HDC that the formatting loop would use, so the hypothesis needs
to first identify which variable actually is the screen HDC before it
can be tested.

## 10. Hypothesis about the HDC in the formatting loop, ruled out for the character-width path; scope correction for the function (2026-08-12)

Continuation of §9. Direct reading (no gdb, no execution, only source
reading) of `wordtech/format.c` and the full `LOADFONT.C`.

**Important scope correction for the next session:** §9 estimated
`C_FormatLineDxa` at "600+ lines" (454-1206+). It is much bigger: the
real function runs from `format.c:454` through the `return; }` at
`format.c:2604` (closing the `#ifdef DEBUGORNOTWIN` at 2606), **~2150
lines, not ~750.** The `LEndJ:` (line 2378) and `LEnd:` (line 2481)
targets are much further down than the range cited in §9 suggested.

**Finding 1, the character-width measurement path of the main loop does
NOT call GDI or touch any HDC:**

A `grep` for `GetTextExtent*|GetCharWidth|SelectObject|hdc|HDC|GetDC`
over the whole of `wordtech/format.c` has no matches. The width
calculation in the hot loop (`format.c:900-907`, and repeated at
~15 more points mapped by a grep for `DxpFromCh` in the file) is:

```c
dxt = dxp = DxpFromCh(ch, &vfti);
```

`C_DxpFromCh` (`format.c:2912-2917`) is a pure function:

```c
HANDNATIVE int C_DxpFromCh( ch, pfti )
    int ch;
    struct FTI *pfti;
    {
    return pfti->rgdxp [ch] + pfti->dxpExpanded;
    }
```

It only indexes the `rgdxp[256]` array of `struct FTI`
(`Opus/fontwin.h:157`), already populated before entering the loop.
**There is no live HDC anywhere in the 900-1259 stretch of
`format.c`** (the range effectively reviewed line by line in this
session), the §9 hypothesis ("the loop still calls real GDI with an
inconsistent HDC") is **ruled out for this specific path**: there is
no GDI call to make inconsistent, it is a table lookup.

**Finding 2, the real screen HDC lives in `LOADFONT.C`, not in
`format.c`, and is still needed even on the path already wired to
Qt:**

`vfti`/`vftiDxt` (the `struct FTI` values `format.c` reads) are filled
in `C_LoadFcid` (`Opus/LOADFONT.C:198-608`), not in `format.c`. The real
screen HDC is `vsci.hdcScratch` (or each window's `hdc`,
`(*hwwd)->hdc`), assigned inside `FSelectFont`
(`LOADFONT.C:663-813`, screen branch at 709-808) and consumed by
`C_LoadFcid` in `GetTextMetrics(hdc, ...)` (`LOADFONT.C:349`, right after
the `LNewMetrics:` label).

Key point: the `OPUS_X64`/`__GNUC__` guard that diverts the
variable-pitch width table to `OpusShellCharWidths`
(`LOADFONT.C:428-467`, noted as wired by `00de60f2`) comes **after**
`FSelectFont` (line 345) and `GetTextMetrics` (line 349), meaning that,
**even on the path already connected to the Qt contract, `C_LoadFcid`
still calls real GDI (`FSelectFont` + `GetTextMetrics`) on
`vsci.hdcScratch` for `dypAscent`/`dypDescent`/`dxpOverhang` before
deciding whether the width table comes from GDI or from
`OpusShellCharWidths`.** This was already noted as open question #3 in
`docs/port-qt/01-core-shell-boundary.md` (referenced in this task's
prompt); this session does not resolve it, it only confirms by direct
reading that the mechanism is exactly that (unconditional
`GetTextMetrics` in `LNewMetrics`, line 349) and that there is no
parallel path in `format.c` that avoids it.

**HDC hypothesis, revised status:** ruled out for the
`C_FormatLineDxa` loop itself (there is no HDC there); **still open,
unconfirmed**, for `vsci.hdcScratch` inside `C_LoadFcid`/`FSelectFont`,
if that HDC ends up null or stale relative to the Qt startup, the crash
would occur inside `LOADFONT.C`/`disp*.c` (where `vsci.hdcScratch` is
initialized), not inside `format.c`. The initialization code for
`vsci.hdcScratch` was not read this session (candidate: `disp1.c`/`initwin.c`,
unconfirmed), it is the natural next step, not `format.c`.

**Stretch of `C_FormatLineDxa` covered this session (full, line-by-line
reading):** `format.c:454-1259` (initialization + main loop up through
`chSpace`/`LBreakOppR` handling) and `format.c:2560-2605` (the tail of
the function: `dypAfter` calculation, `fPageView` adjustment, closing
`grpchr`). `Opus/LOADFONT.C` (1064 lines) was also read in full.

**Not covered, pending for the next session:**
`format.c:1259-2560` (~1300 lines unread): tab handling
(`case chTab` and its neighborhood, not yet located), fields/formulas
(`chrmFormula`/`chrmDisplayField`/`chrmFormatGroup`, mentioned in
`format.h` but their handling in the loop not reviewed), `LEndJ:` (2378,
justification logic) and `LEnd:` (2481, line closing before the tail
already reviewed at 2560-2605). This is the stretch with the highest
remaining probability of containing the cause, given that §9 already
ruled out the run buffer and this session ruled out the simple
character-width path.

**No gdb was run this session**, everything above is source-code
reading, not live verification. Any confirmation of "null HDC" in
`vsci.hdcScratch` requires a breakpoint at `FSelectFont`
(`LOADFONT.C:709`) or `GetTextMetrics` (`LOADFONT.C:349`) during a
real `WORD1` startup, not done yet.

**REFUTED, see §12 (points 1-2).** The premise of §11.3 ("vanilla `gdb`
cannot resolve file:line breakpoints in `WORD1.exe.so`") is false. Real
cause: the 18 case-shimmed files (`LOADFONT.C` among them) register
their compilation unit's DWARF with the symlink's path in
lowercase; `break LOADFONT.C:349` could never resolve because of that,
not because of any limitation of `gdb`/Wine. `break loadfont.c:349`
resolves and fires on the first try, with a complete symbolic backtrace
up to `C_FormatLineDxa`. The "net result" of §11.4 and the 3-step
"recommended path" become obsolete, see the Promoted Plan in this
session's revision and §12.

## 11. Live verification attempted, blocked for two distinct reasons, neither confirms nor rules out the hypothesis (2026-08-12)

Direct continuation of §10. Environment **different** from §1-§9: a new
sandbox (Debian 13 "trixie", GCC 14.2.0, `wine-10.0` Debian repack,
`gdb` 16.3), not Fedora 44/GCC 16.1.1/wine-staging 11.0. No pre-existing
`WORD1.exe.so` binary, it was rebuilt from scratch. This environment
difference ends up being the session's main finding (see below), not an
incidental detail.

### 11.1 Build blockers found and resolved (environment, not `src/Opus/`)

Three new toolchain problems, independent of the bug under
investigation, blocked even producing a binary:

1. **`wrc: Error: codepage 1252 not supported`** when compiling
   `port/word1.rc`. Cause: this system's `wrc` (Wine Resource
   Compiler 10.0) does not find its NLS tables (`/usr/share/wine/nls/`)
   without explicit `--nls-dir`, even though `c_1252.nls` does exist on
   disk. **Fixed** by adding `--nls-dir=/usr/share/wine/nls` to the
   `wrc` `add_custom_command` in `src/CMakeLists.txt` (line ~1370).
2. **`GetCurrentThreadStackLimits was not declared in this scope`** when
   compiling `port/original/opus_original_startup_probe.cpp:249`. Cause:
   this Wine 10.0 package's `processthreadsapi.h` does not declare that
   function (exported by `kernel32` since Vista+; it was present in the
   Wine 11.0/Fedora used in §1-§9). **Fixed** with a local
   `extern "C" WINBASEAPI` declaration guarded by
   `#if !defined(_MSC_VER)`, commented in place explaining the cause;
   it does not touch `src/Opus/` or `src/OpusEtAl/`, it is in `src/port/`.
3. **`opus_word1_ui_test.cpp:379`: no matching function for call to
   `min(LONG, long int)`** when trying to compile `opus_word1_ui_test`
   (needed for the original plan of triggering `C_FormatLineDxa` via
   `--typing`/`--font-typing` instead of just launching `WORD1` empty).
   Cause: a `LONG` type mismatch (`int` in this Wine 10.0 `windows.h`)
   against the `20L` literal (`long`). **Not fixed**, out of the path
   ultimately used (see 11.3); if a future session needs this harness in
   this same kind of environment, the fix is an explicit cast at
   `opus_word1_ui_test.cpp:379-380`, not touched here.

None of the three is the bug under investigation, they are
portability gaps between Wine/GCC versions within the project's own
support tree (`src/port/`, `src/CMakeLists.txt`), not in `src/Opus/`.
**These changes were not committed**, they remain in the working tree,
pending the maintainer's decision on whether to keep them (needed to
reproduce on any Debian/wine-10.0 environment) or revert them.

### 11.2 The crash from §1 does not reproduce in this environment

With the binary already compiled (`bin/WORD1.exe.so`, not stripped, with
`debug_info`, confirmed with `file`), two direct runs under Xvfb
(`DISPLAY=:99 timeout {30,60} /usr/lib/wine/wine64 WORD1.exe.so`, a real
display, not the `nodrv` path seen when `DISPLAY` is not exported):

```
$ DISPLAY=:99 timeout 60 /usr/lib/wine/wine64 WORD1.exe.so
04b0:fixme:dwmapi:DwmSetWindowAttribute (0000000000060088, 22, 00007FFFFE1F788C, 4) stub
[end of output -- timeout kills the process at 60s, exit 124, no crash]
```

This is **exactly the same line** (`fixme:dwmapi:DwmSetWindowAttribute`)
that in §1 appears immediately before `malloc(): invalid size
(unsorted)` / `free(): invalid next size (normal)`. Here, instead, the
process stays alive and idle (confirmed with `ps aux`, `WORD1.exe.so`
listed, consuming minimal CPU) indefinitely, tested up to 60s with no
sign of corruption, no further output.

**Reading, not confirmed beyond this:** the §1 crash is not
deterministic across the Wine 11.0/Fedora44/GCC16.1.1 and
Wine 10.0/Debian13/GCC14.2.0 environments. It was not investigated which
of the three variables (Wine version, GCC version, or some difference in
`wine.conf`/DPI/theme between the two installs) is the cause; that is out
of scope for this specific task. **Practical consequence for the next
session:** reproducing the real crash to keep diagnosing it with `gdb`
requires an environment matching Fedora 44/wine-staging 11.0/GCC 16.1.1,
not this Debian sandbox.

### 11.3 File:line breakpoints never resolve, same mechanism as §2, now also confirmed for breakpoints

Tried anyway (the goal was to see `vsci.hdcScratch` in transit, not
just wait for the crash). With `set breakpoint pending on` and
`break LOADFONT.C:349` / `break LOADFONT.C:709` before `run`:

```
$ gdb -q --batch -ex "set breakpoint pending on" -ex "break LOADFONT.C:349" \
    -ex "run" -ex "info sharedlibrary" -ex "info breakpoints" \
    --args /usr/lib/wine/wine64 WORD1.exe.so
No symbol table is loaded.  Use the "file" command.
Breakpoint 1 (LOADFONT.C:349) pending.
[...]
Application could not be started, or no application associated with the specified file.
ShellExecuteEx failed: File not found.

[Inferior 1 (process 134178) exited with code 01]
From                To                  Syms Read   Shared Object Library
0x00007ffff7fc8000  0x00007ffff7fef3d1  Yes         /lib64/ld-linux-x86-64.so.2
Num     Type           Disp Enb Address    What
1       breakpoint     keep y   <PENDING>  LOADFONT.C:349
```

(That particular run failed on a relative-path error on Wine's side,
`ShellExecuteEx`, unrelated to the bug; it was repeated with the correct
cwd and the process ran normally up to the same idle point of §11.2.)
**What matters in this output:** after the process ran (even though it
terminated on an unrelated error), `info sharedlibrary` lists only
`ld-linux-x86-64.so.2`, not `WORD1.exe.so`, not `ntdll.so`, not
`libwine`, nothing. Breakpoint 1 stays `<PENDING>` forever. Repeated
with `DISPLAY=:99` and the process running normally for 25s without
crashing: same result, `run` never returns control to gdb because the
breakpoint never fires (it cannot fire: `WORD1.exe.so` is never
registered as a shared object).

**This confirms and extends the §2 finding:** it is not just that
`gdb` cannot *symbolize* an already-broken stack (§2), it also cannot
*resolve file:line breakpoints* inside `WORD1.exe.so` at all, because it
never sees it as a loaded shared object (Wine's manual
`wine-preloader` mechanism still does not go through
`_dl_debug_state`/`r_debug`). A breakpoint by absolute address
(`break *0xADDRESS`, with the address taken from `addr2line` as in §5)
would probably work, not tried in this session for lack of time, and in
any case the crash did not reproduce here (§11.2) to have an address of
interest to chase.

### 11.4 Net result

**Neither of the two breakpoints (`LOADFONT.C:349`, `LOADFONT.C:709`)
ever fired**, not for lack of an execution path (`WORD1`'s startup does
go through font loading, it is a normal startup route), but because
vanilla `gdb` cannot set file:line breakpoints in this binary, a
point already documented in §2 and now confirmed to also apply to
breakpoints, not just stack resolution. **The `vsci.hdcScratch`
null/stale hypothesis stands exactly where it stood in §10: open,
neither confirmed nor ruled out.** No real `print vsci.hdcScratch` or
`print hdc` was obtained in this session.

**Recommended path for the next session** (not attempted here):
1. Reproduce in an environment matching Fedora 44/wine-staging
   11.0/GCC 16.1.1 (§11.2), this Debian/wine-10.0 sandbox is not usable
   for this.
2. Use `addr2line`/static reading (as in §5) to get the absolute
   address of `LOADFONT.C:349` and `:709` in the binary rebuilt in that
   environment, and set the breakpoint by address
   (`break *0x...`), not by file:line, avoiding the §11.3 problem.
3. Alternative without `gdb`: instrument `LOADFONT.C` with a temporary
   `fprintf(stderr, ...)` printing `vsci.hdcScratch` and `hdc`
   at those two points, requires explicit authorization to touch
   `src/Opus/LOADFONT.C` (a GitHub issue first, per the restriction on
   that tree), but sidesteps both the §11.3 blocker and the §11.2
   non-reproduction (the print would show in any run, crash or not).

## 12. Live refutations of the §11.4 "recommended path" (2026-08-12, same session, Debian 13/wine-10.0/gdb 16.3 environment)

Direct continuation of §11, same binary (`bin/WORD1.exe.so`, not
rebuilt). The three §11.4 steps turned out to be unnecessary: step 2
(`addr2line` + `break *0x`) solved a problem that did not exist, and
step 3 (patching `src/Opus/LOADFONT.C`) gives data `gdb` already gives
for free. What follows is the evidence, command by command, without
summarizing.

### 12.1 File:line breakpoints do resolve, the blocker was the symlink's case

`src/CMakeLists.txt` (lines ~944-964) symlinks 18 sources, including
`LOADFONT.C`, to `generated/lowercase-c/*.c` and compiles them from
there. The DWARF records the path in lowercase. Reproduced twice with
the same binary, changing only the case of the breakpoint:

**Uppercase (as in §11.3), never resolves:**
```
$ gdb -q --batch -ex "set breakpoint pending on" -ex "break LOADFONT.C:349" -ex "run" ...
No symbol table is loaded.  Use the "file" command.
Breakpoint 1 (LOADFONT.C:349) pending.
[...]
0518:fixme:dwmapi:DwmSetWindowAttribute (0000000000080094, 22, 00007FFFFE1F788C, 4) stub
[the process stays idle -- run never returns, breakpoint stays <PENDING>]
```

**Lowercase, fires on the first try, full backtrace:**
```
$ gdb -q --batch -ex "set breakpoint pending on" \
    -ex "break loadfont.c:349" -ex "break loadfont.c:709" \
    -ex "run" -ex "bt 6" --args /usr/lib/wine/wine64 WORD1.exe.so
Breakpoint 1 (loadfont.c:349) pending.
Breakpoint 2 (loadfont.c:709) pending.
[...]
Breakpoint 2, FSelectFont (pfti=0x7ffff7508460 <vfti>, phfont=0x7ffff7508d60 <rgfce+64>, phdc=0x7ffffe1f6438) at /home/pablo/msword/out/linux-winelib-debug/generated/lowercase-c/loadfont.c:711
711			HFONT hfontSystem = GetStockObject( SYSTEM_FONT );

#0  FSelectFont (pfti=0x7ffff7508460 <vfti>, phfont=0x7ffff7508d60 <rgfce+64>, phdc=0x7ffffe1f6438) at /home/pablo/msword/out/linux-winelib-debug/generated/lowercase-c/loadfont.c:711
#1  0x00007ffff7295071 in C_LoadFcid (fcid=..., pfti=0x7ffff7508460 <vfti>, fWidthsOnly=1) at /home/pablo/msword/out/linux-winelib-debug/generated/lowercase-c/loadfont.c:345
#2  0x00007ffff7294bfc in C_LoadFont (pchp=0x7ffffe1f6630, fWidthsOnly=1) at /home/pablo/msword/out/linux-winelib-debug/generated/lowercase-c/loadfont.c:131
#3  0x00007ffff726dc85 in N_LoadFont (chp=0x7ffffe1f6630, widths_only=1) at /home/pablo/msword/src/port/original/opus_asm_native_adapters.cpp:254
#4  0x00007ffff7299f8c in C_FormatLineDxa (ww=5, doc=7, cp=0, dxa=8640) at /home/pablo/msword/src/Opus/wordtech/format.c:2269
#5  0x00007ffff726d613 in N_FormatLineDxa (ww=5, doc=7, cp=0, dxa=8640) at /home/pablo/msword/src/port/original/opus_asm_resn2_adapters.cpp:186
```

Fixed for future sessions: use lowercase for the 18 sources listed in
`src/CMakeLists.txt` (`CLIPBORD.C CLIPBRD2.C CMD3.C CREATE2.C
DDESRVR.C DLBENUM.C EDIT.C FIELDCMD.C FILE2.C GRSPEC.C LOADFONT.C PIC2.C
RTFIN.C RTFOUT.C RTFRARE.C SCREEN2.C SPELL.C SYSCHG.C`); any other
file under `src/Opus/` uses its real name.

Useful side effect: the startup hot path is now located
unambiguously: `C_FormatLineDxa` (`format.c:2269`) → `N_LoadFont` →
`C_LoadFcid` (`loadfont.c:131/345`) → `FSelectFont` (`loadfont.c:711`).
`format.c:2269` falls within the `1259-2560` stretch that §10 flagged
as the highest-probability remaining and left unread; it goes from
"probable" to confirmed by real execution.

### 12.2 The `info sharedlibrary` reading in §11.3 was contaminated by timing, not by a `gdb`/Wine block

The block already quoted in §11.3 itself shows the contamination: the
reading was taken **after** `[Inferior 1 (process 134178) exited with
code 01]`, and the resulting listing only has
`ld-linux-x86-64.so.2`, not even `libc.so.6`, impossible in a live
dynamic process. That data point alone already invalidated the §11.3
reading, without needing any further proof.

Reproduced here in two distinct ways, both contrasting with the above:

**(a) Same symptom reproduced fresh, with the process actually stuck
in `run` (there is no `wine-preloader` in this environment, see 12.5,
so "timing contamination" and "wine-preloader breaks the protocol" stand
as two independent explanations for the same symptom, not a single
one):** the script with `break LOADFONT.C:349` (uppercase) plus `run` +
`info sharedlibrary` + `info breakpoints` never gets to execute the two
final commands because `run` does not return (the process stays idle,
same as §11.2); the log cuts off after the `fixme:dwmapi` line.

**(b) `info sharedlibrary` read at the right moment (attached to an
already-running process, without waiting for it to exit), WORD1.exe.so
appears with `Syms Read = Yes`:**
```
$ gdb -q --batch -ex "info sharedlibrary" -ex "info line LOADFONT.C:349" \
    -ex "print vsci" -ex "print vsci.hdcScratch" -p <PID>
[...]
From                To                  Syms Read   Shared Object Library
0x00007f1c296b6400  0x00007f1c298188fd  Yes         /lib/x86_64-linux-gnu/libc.so.6
0x00007f1c29895000  0x00007f1c298bc3d1  Yes         /lib64/ld-linux-x86-64.so.2
0x00007f1c295d2c40  0x00007f1c296321a0  Yes (*)     /usr/lib/wine/../x86_64-linux-gnu/wine/x86_64-unix/ntdll.so
0x00007f1c28a867c0  0x00007f1c28d85346  Yes         /home/pablo/.wine/dosdevices/z:/home/pablo/msword/bin/WORD1.exe.so
[... 60 more lines, all Yes or Yes (*), full log in
     /tmp/claude-*/scratchpad/gdb2.log from this session ...]

===== RESOLVE FILE:LINE =====
.../gdb2.txt:6: Error in sourced command file:
No source file named LOADFONT.C.
```

The last error (`No source file named LOADFONT.C`) is the same
fingerprint of the 12.1 problem, confirmed by a second independent path:
the shared object really is loaded and really does have symbols
(`WORD1.exe.so`, `Yes`); what fails to resolve is the uppercase name.

### 12.3 `vsci.hdcScratch` hypothesis, "null" branch ruled out live; "stale" branch still not closed

With the lowercase breakpoints already firing (12.1), the real state
was printed at the two points §10 left as an open question:

**In `FSelectFont`, right before `GetStockObject`/`SelectObject`
(`loadfont.c:711`):**
```
Breakpoint 2, FSelectFont (...) at .../loadfont.c:711
711			HFONT hfontSystem = GetStockObject( SYSTEM_FONT );
$1 = (HDC) 0x7f750df50        # *phdc
$2 = (HDC) 0x3410055          # vsci.hdcScratch
```

**In `C_LoadFcid`, right before `GetTextMetrics` (`loadfont.c:349`):**
```
Breakpoint 1, C_LoadFcid (fcid=..., pfti=0x7ffff7508460 <vfti>, fWidthsOnly=1) at .../loadfont.c:349
349		GetTextMetrics( hdc, (LPTEXTMETRIC) &tm );
$1 = (HDC) 0x120100af
$2 = {fMonochrome = 0, ..., {mdcdScratch = {hdc = 0x3410055, pbmi = ...}, {hdcScratch = 0x3410055, pbmiScratch = ...}}, mdcdBmp = {hdc = 0x1410069, ...}, ..., dxpScreen = 1024, dypScreen = 768, ..., hbrBkgrnd = 0x910005a, ...}
$3 = 1   # fWidthsOnly
```

**"Null" branch: ruled out.** `hdc`, `vsci.hdcScratch`, and the rest of
`vsci` are populated with non-null, reasonable-looking values
(`dxpScreen=1024`, `dypScreen=768`, matching the actual resolution of
the Xvfb `:99` used in this run) at both points.

**"Stale" branch: NOT ruled out, still open.** A freed `HDC` (via
`DeleteDC`/`ReleaseDC` on the same value, at another point in startup)
can still hold a perfectly non-null pointer value; simply inspecting
`print vsci.hdcScratch` does not distinguish "live, valid handle" from
"freed handle, dangling value". Closing this branch requires a real
validity check on the handle, candidates, none tried in this
session: `GetObjectType(hdc)` (returns 0 on an invalid handle),
`GetDeviceCaps` with a return check, or the `WINEDEBUG=+gdi` channel
(see 12.6) placed around these two points to see whether Wine already
reported a `DeleteDC` on `0x3410055` before this point in startup. §10
stands as: **null branch closed, stale branch pending**, not "hypothesis
ruled out" outright.

### 12.4 glibc's heap checker runs under Wine, with limited diagnostic value, not "ruled out just like ASan/valgrind"

Confirmed live, real `WORD1.exe.so` process (PID 141797, launched
from `bin/`, without rebuilding):

```
$ grep -c malloc_debug /proc/141797/maps
5
$ grep malloc_debug /proc/141797/maps
7fa2f0a84000-7fa2f0a86000 r--p 00000000 08:01 12287  /usr/lib/x86_64-linux-gnu/libc_malloc_debug.so.0
7fa2f0a86000-7fa2f0a8d000 r-xp 00002000 08:01 12287  /usr/lib/x86_64-linux-gnu/libc_malloc_debug.so.0
7fa2f0a8d000-7fa2f0a90000 r--p 00009000 08:01 12287  /usr/lib/x86_64-linux-gnu/libc_malloc_debug.so.0
7fa2f0a90000-7fa2f0a91000 r--p 0000b000 08:01 12287  /usr/lib/x86_64-linux-gnu/libc_malloc_debug.so.0
7fa2f0a91000-7fa2f0a92000 rw-p 0000c000 08:01 12287  /usr/lib/x86_64-linux-gnu/libc_malloc_debug.so.0
$ tr '\0' '\n' < /proc/141797/environ | grep -E 'LD_PRELOAD|GLIBC_TUNABLES'
LD_PRELOAD=/lib/x86_64-linux-gnu/libc_malloc_debug.so.0
GLIBC_TUNABLES=glibc.malloc.check=3:glibc.malloc.perturb=165
$ ps -p 141797 -o pid,etimes,stat,cmd
    PID ELAPSED STAT CMD
 141797     154 S    WORD1.exe.so
```

154s alive, `LD_PRELOAD`/`GLIBC_TUNABLES` survive `wine64`'s re-exec,
without aborting. It does not collide with the address-space
reservation like ASan/valgrind (§4, §8) because it does not reserve or
unmap anything, it only instruments glibc's `malloc`.

**But, and this is what qualifies §8, not what confirms it outright,
almost all of Word 1.1a's memory goes through `GlobalAlloc`/`LocalAlloc`/
`HeapAlloc` (the Win16/Win32 API), which Wine resolves with its own
allocator (`Rtl*Heap`, over `ntdll`), not with glibc's `malloc`.**
Direct evidence, `WINEDEBUG=+heap` channel against the same binary:

```
$ DISPLAY=:99 WINEDEBUG=+heap /usr/lib/wine/wine64 WORD1.exe.so
05e4:trace:heap:RtlCreateHeap flags 0x2, addr 0000000000000000, total_size 0, commit_size 0, lock 0000000000000000, params 0000000000000000
05e4:trace:heap:RtlAllocateHeap handle 00007FFFFE220000, flags 0x8, size 0x2000, return 00007FFFFE2208F0, status 0.
05e4:trace:heap:RtlAllocateHeap handle 00007FFFFE220000, flags 0x8, size 0x500, return 00007FFFFE222920, status 0.
05e4:trace:heap:RtlAllocateHeap handle 00007FFFFE220000, flags 0, size 0x10e2, return 00007FFFFE222E50, status 0.
05e4:trace:heap:RtlFreeHeap handle 00007FFFFE220000, flags 0, ptr 00007FFFFE224690, return 1, status 0.
05e4:trace:heap:RtlReAllocateHeap handle 00007FFFFE220000, flags 0x10, ptr 00007FFFFE225860, size 0x1180, return 00007FFFFE225860, status 0.
[... dozens more lines, same pattern, handle 00007FFFFE220000 -- a
     memory region owned by Wine, separate from glibc's heap ...]
```

All the real allocation activity during startup goes through
`RtlAllocateHeap`/`RtlFreeHeap`/`RtlReAllocateHeap`/`RtlSizeHeap` on a
`handle` of its own (`00007FFFFE220000`), managed by Wine's `ntdll.so`,
a metadata layer separate from the glibc `malloc` chunks that
`glibc.malloc.check` inspects. Corruption that damages *that* heap's
metadata (the most likely candidate for a bug in the original Word 1.1a,
which uses `GlobalAlloc`/`HeapAlloc` almost exclusively) **will not be
detected** by `glibc.malloc.check`, no matter how long it runs clean.
The 154s clean run (above) **is not evidence that startup is free of
corruption**, only that, if there is any, it is not going through
glibc's heap.

### 12.5 Absence of `wine-preloader`, specific to this install, not generalized

```
$ find /usr -iname '*preloader*'
[no output]
$ file /usr/lib/wine/wine64
/usr/lib/wine/wine64: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=017a68eb9b041254c9c597213f78aad6f1f32317, for GNU/Linux 3.2.0, stripped
$ wine --version
wine-10.0 (Debian 10.0~repack-6)
```

`wine64` in this package (`wine64 10.0~repack-6`, Debian) is a normal
PIE ELF with the standard `ld-linux` interpreter, no separate
`preloader` binary anywhere under `/usr`. This is specific to this
particular wine-staging 10.0/Debian packaging and **was not generalized
or tested** against Fedora 44/wine-staging 11.0 (the §1-§9 environment,
where §2 does document with direct evidence a real `wine-preloader`
process appearing in `gdb`'s trace). Do not assume this absence holds
on that other install.

### 12.6 `WINEDEBUG=+heap`, verified against the installed binary, `heap.c` source not available locally

```
$ find / -path '*/wine*/dlls/ntdll/heap.c' 2>/dev/null
[no output, exit=1]
$ dpkg -l | grep -i wine
ii  libwine:amd64        10.0~repack-6  amd64  Windows API implementation - library
ii  libwine-dev:amd64     10.0~repack-6  amd64  Windows API implementation - development files
ii  wine                  10.0~repack-6  all    Windows API implementation - standard suite
ii  wine64                10.0~repack-6  amd64  Windows API implementation - 64-bit binary loader
ii  wine64-tools          10.0~repack-6  amd64  Windows API implementation - 64-bit developer tools
```

No Wine source package is installed on this system, `dpkg -l` only
lists binaries/dev-headers. **`heap.c` itself could not be reviewed;
what follows is verified against the installed binary's live
behavior, not against its source.**

The `heap` channel exists and produces real traces (see the full block
in 12.4). Also tested at the other two class levels documented by
`man wine`:

```
$ DISPLAY=:99 timeout 3 env WINEDEBUG=warn+heap /usr/lib/wine/wine64 WORD1.exe.so
[no heap lines in 3s of idle startup]
$ DISPLAY=:99 timeout 3 env WINEDEBUG=err+heap /usr/lib/wine/wine64 WORD1.exe.so
[no heap lines in 3s of idle startup]
```

`warn+heap`/`err+heap` emitted nothing in a 3s clean startup,
consistent with them only speaking when there is something to report,
but **without the source, it cannot be confirmed exactly what condition
triggers a `warn` or `err` on this channel** (e.g. whether it validates
arena integrity on every call, or only reports
`NtAllocateVirtualMemory`/invalid-parameter failures). This is left as
an explicit gap, not assumed.

**Recommended command for milestone 2 of plan v2 (on Fedora, where the
§1 crash does reproduce):**
```
WINEDEBUG=+heap wine WORD1.exe.so 2>heap.trace
```
`trace+heap` (`+heap` is an alias for `trace+heap` per the `WINEDEBUG`
syntax in `man wine`) gives the full log of
`RtlAllocateHeap`/`RtlFreeHeap`/`RtlReAllocateHeap`/`RtlSizeHeap` with
size and returned pointer on every call, enough to bracket the time
window of the corrupting write by cross-referencing that log against
the crash address `addr2line` already gives (§5), without needing
`heap.c`'s source. Combine it with `warn+heap,err+heap` in the same
run (`WINEDEBUG=+heap,warn+heap,err+heap` is redundant since `+heap`
already enables all classes; `WINEDEBUG=+heap` alone is enough) so as
not to miss any message level if the real crash does trigger one.

## 13. `vsci.hdcScratch` "stale" branch, closed in this environment, without using `GetObjectType` (2026-08-13)

Continuation of §10/§12.3, same environment (Debian 13/wine-10.0/gdb
16.3), binary rebuilt after this session's 7 `OpusMem*` migration
commits (`bf7a5e1`..`aab06e5`), **the §1 crash still does not reproduce
here**, re-verified before this analysis (`wine WORD1.exe.so` under
`Xvfb :99`, 10s, no output, same behavior as §11.2). This point does not
touch the main blocker; it closes the secondary item §12.3 left open.

**Failed attempt, documented so it is not repeated:** calling
`GetObjectType(hdc)` from `gdb` (`print GetObjectType(vsci.hdcScratch)`)
fails with `No symbol "GetObjectType" in current context` at the two
§12.1 breakpoints (`loadfont.c:711` and `:349`). Cause, confirmed with a
full, unfiltered `info sharedlibrary` on the same process: in this
Winelib build, `gdi32` does not appear as a native `.so` on its own,
unlike `win32u.so`/`winex11.so`/`winspool.so`
(`/usr/lib/x86_64-linux-gnu/wine/x86_64-unix/`), which do appear in
the list. Also confirmed by `nm -D bin/WORD1.exe.so | grep -w
GetObjectType` (no result) and `GetTextMetrics` (no result), only
`FSelectFont` (a symbol native to `Opus/`) appears exported. Reading:
in this configuration `gdi32` resolves purely via PE (imported through
Wine's PE loading machinery, with no ELF counterpart `gdb` can see), so
`gdb` has nothing to resolve the name through by native symbol lookup;
calling the real function would require rebuilding the import thunk by
hand, not worth the cost against the alternative below.

**Path that did work: `WINEDEBUG=+gdi` over a full run, grep for the
handle value.** Same startup command as the rest of this section
(`cwd=bin/`, `DISPLAY=:99`):

```
$ DISPLAY=:99 timeout 8 env WINEDEBUG=+gdi wine WORD1.exe.so >gdi.trace 2>&1
$ wc -l gdi.trace
45265 gdi.trace
```

`vsci.hdcScratch` is created exactly once, early in startup:

```
0024:trace:gdi:alloc_gdi_handle allocated NTGDI_OBJ_MEMDC 0x3410055 73/65536
```

Same handle value (`0x3410055`) as in the §12.3 `gdb` run,
deterministic in this build/environment, not a coincidence of this
run. It is reused 43 times via `SelectObject` throughout the 45265
lines of the trace (last appearance on line 39537, of 45265), and
**zero** occurrences alongside `NtGdiDeleteObjectApp`/`free_gdi_handle`
in the 3982 lines that do contain those two tokens anywhere in the
trace, verified with `grep -c "free_gdi_handle\|DeleteDC" gdi.trace`
(3982 lines, none with `3410055`) and `grep -n 3410055 gdi.trace | grep -iv
"SelectObject\|alloc_gdi_handle"` (no output). The trace ends with the
process still active, drawing real content onto another DC (`Polygon`
`Rectangle` `SelectObject` onto `000000000D0100CD`, a different window
HDC) until cut off by `timeout`, with no exception, `fault`,
`crash`, or corruption message anywhere in the 45265 lines (`grep -in
"exception|crash|segv|fault|corrupt" gdi.trace`, no result).

**Conclusion: "stale" branch ruled out for this particular run, not in
general.** `vsci.hdcScratch` is alive (never freed) throughout the
whole observed 8s window, including the real use in `FSelectFont`/
`C_LoadFcid` from §12.1/§12.3, it is not a dangling handle in this
startup. §10 now stands as: **null branch closed (§12.3), stale branch
closed for this environment (here)**, the `vsci.hdcScratch` HDC
hypothesis stops being a candidate to explain a crash that this
environment never reproduces anyway. It cannot be generalized to Fedora
(where §1 does reproduce) without repeating this same capture there, it
remains part of milestone 2, pending, not something this result already
covers.

---

## hp-15 session (EndeavourOS/Arch), 2026-08-14: reproduces, and a first named-and-lined candidate

**Environment:** EndeavourOS (Arch, rolling), GCC 16.2.1, wine-staging 11.15, `gdb`
17.2, `valgrind` 3.25.1. Build from `HEAD` (`5fed452`), reconfigured and
rebuilt in this session (`opus_original_engine` 0 errors, `WORD1.exe`/
`WORD1.exe.so` linked). Dedicated `Xvfb :99` (not the desktop session's
real display), installed (`xorg-server-xvfb`) so as not to interfere
with the graphical environment in use.

### 1. It reproduces, a fourth and fifth corruption signature

Four runs with `gdb -q --batch -ex run -ex "bt full" --args wine
WORD1.exe.so`, same startup point up to the failure
(`DwmSetWindowAttribute` stub, same as Fedora/Debian):

| Run | glibc message |
|---|---|
| 1 | `free(): invalid pointer` |
| 2 | `free(): invalid next size (normal)` (same as signature #2 from Fedora, §1) |
| 3 | `free(): invalid next size (normal)` |
| 4 (with debuginfod) | `double free or corruption (!prev)` |

Two new signatures (`invalid pointer`, `double free or corruption (!prev)`)
that did not appear on Fedora or Debian, reinforcing the §1 reading: it
is real, timing-dependent heap corruption, not a deterministic logic
bug. **hp-15 reproduces consistently (4/4)**, unlike the VPS
(Debian/GCC 14.2/wine 10.0, confirmed not to reproduce, `02-fedora-pending.md`),
GCC ≥15 appears to be the relevant variable, not the distro.

### 2. Milestone 2 run for the first time: `WINEDEBUG=+heap`

Never run before in any environment (§2 of `02-fedora-pending.md` left
it as a pending recommendation). Result: **888,073 lines**, real crash
on line 24273, the rest are threads that kept running after this
thread's `abort()` (it is not that the process stayed alive; it is
buffering mixing between Wine's trace channel and glibc's direct
`fprintf` to stderr, the post-crash line order is not chronologically
reliable across threads, but it is reliable within a single thread).

**Finding:** immediately before the crash line **there is no
`RtlFreeHeap` logged**, the corruption is detected in a `free()` that
does not go through Wine's heap wrapper that `+heap` instruments. This
is consistent with what follows (§3): the `free()` that aborts is a
C++ `std::wstring` destructor (direct glibc `malloc`/`free` via
`operator delete`), not a Win32 API `HeapFree`/`GlobalFree` that
`WINEDEBUG=+heap` would have captured.

### 3. Frame #0 symbolized, and something beyond frame #0

`info proc mappings` + `x/3i $pc` at the abort point: `$pc` falls
inside `/usr/lib/libc.so.6` (executable range `0x...c24000`-`0x...d9f000`),
it is internal glibc code (the `malloc_printerr`/`abort`/`raise` path),
as expected. `bt` does not unwind beyond frame #0, also tried with
`debuginfod.archlinux.org` enabled (`set debuginfod enabled on`, symbols
were downloaded to `~/.cache/debuginfod_client/`, 6 build-ids) and it
still does not unwind. **It is not a lack of symbols, it is the
impossibility of unwinding due to broken CFI/frame pointers in glibc's
optimized code at this point**, the same family of problem that already
blocked `winedbg`/`dbghelp` on Fedora (§3 original), now also confirmed
with vanilla gdb + debuginfod on Arch.

**Workaround that did work, manual stack scan:** with the memory map
already captured (`info proc mappings`) and a raw stack dump
(`x/400gx $rsp`), each 8-byte value was classified against the known
executable ranges (libc, `ntdll.dll`, `user32.dll`, `win32u.dll`,
`WORD1.exe.so`) with a Python script. It found a chain of addresses
inside `WORD1.exe.so`, not a real unwind (it is raw stack memory,
which can include garbage from already-returned calls), but cross-checked
with `addr2line -e WORD1.exe.so -f -C` it gives real, mutually
consistent names and lines:

```
new_allocator<wchar_t>::deallocate           new_allocator.h:184
basic_string<wchar_t>::_M_dispose            basic_string.h:299
basic_string<wchar_t>::~basic_string         basic_string.h:920
sync_combo(HWND, HWND, int&)                 opus_win95_chrome.cpp:890  <-- the for loop on line 890
ComboEnumeration::~ComboEnumeration          opus_win95_chrome.cpp:814
locate_source_combos(HWND, ToolbarState&)    opus_win95_chrome.cpp:849
sync_mirrors(HWND, ToolbarState&)            opus_win95_chrome.cpp:924
toolbar_window_proc(HWND, UINT, WPARAM, LPARAM)  opus_win95_chrome.cpp:2557, 2604
Dispatch<2ul>(...)                           opus_asm_wproc.cpp:79
```

The first three frames (deallocate → `_M_dispose` → `~basic_string`)
are exactly the internal path of "a `std::wstring` is destroyed and its
buffer is freed", and the frame that owns it is `sync_combo`, line 890,
which is this block's `for` loop
(`src/port/original/opus_win95_chrome.cpp:884-897`):

```cpp
if (count >= 0 && count != copied_count) {
    std::wstring mirror_text;
    const int mirror_length = GetWindowTextLengthW(mirror);
    mirror_text.resize(static_cast<std::size_t>(mirror_length) + 1);
    GetWindowTextW(mirror, &mirror_text[0], mirror_length + 1);   // line 888
    SendMessageW(mirror, CB_RESETCONTENT, 0, 0);
    for (int index = 0; index < count; ++index) {                // line 890
        const std::wstring item = wide_from_ansi(combo_item(source, index));
        SendMessageW(mirror, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(item.c_str()));
    }                                                              // line 894 -- ~item() per iteration
    SetWindowTextW(mirror, mirror_text.c_str());
    ...
```

**Concrete hypothesis, not verified with further instrumentation:**
`mirror_text` (lines 885-888) is sized with `GetWindowTextLengthW` and
written with `GetWindowTextW` onto the same buffer. MSDN explicitly
documents that `GetWindowTextLength{A,W}` can return a length that does
not match what the opposite variant (`W` vs `A`) ends up writing,
specifically in ANSI/Unicode mixes, exactly the case here, since the
rest of `sync_combo`/`combo_item` uses `SendMessageA` on the same
control. If Wine's reimplementation of this pair of APIs writes more
`wchar_t` than `GetWindowTextLengthW` reported, it is a bounded
heap-buffer-overflow (a few wchar_t) over the `mirror_text` buffer,
exactly the class of corruption that produces `free(): invalid next size`
/ `double free` on a later `free()` call (not necessarily on
`mirror_text` itself, it can manifest on a neighboring chunk, as with any
heap overflow), consistent with the signature varying between runs
(§1: it depends on which chunk sits next to the damaged buffer).

**Not confirmed. Not applied.** `sync_combo` was not instrumented to
verify the actual size written vs. reported (the obvious next step, not
attempted this session), nor was the block's other, weaker candidate
ruled out (`combo_item`/`wide_from_ansi`, reviewed and with correct size
arithmetic on a plain reading, `MultiByteToWideChar` with
`cchMultiByte=-1` already includes the null terminator in its returned
count, no visible off-by-one).

### 4. Hypothesis from §3 (`mirror_text`), instrumented and **refuted**

Temporary instrumentation applied to `sync_combo` (lines 886-888,
reverted afterward, not committed): instead of writing directly into
`mirror_text[0]`, it wrote into an over-reserved `std::vector<wchar_t>`
with 16 canary cells (`0xCDCD`) beyond the `mirror_length + 1` limit
passed as `nMaxCount` to `GetWindowTextW`, also comparing its return
value against `mirror_length`.

```
[sync_combo DIAG] mirror_length=5 copied=5 guard_cells_clobbered=0/16
```

**5/5 runs, the exact same result** (the three combos, style/font/size,
all go through this same instrumented block on every run).
`GetWindowTextW` returns exactly what was requested and **touches no
canary cell**, the `GetWindowTextLengthW`/`GetWindowTextW` pair on
`mirror` behaves correctly in this Wine reimplementation.
**Hypothesis refuted, it is not the source of the corruption.**

The crash **still occurs** with the instrumentation in place (1/5 runs,
`free(): invalid pointer`), the bug is real and still alive, it just is
not this particular overflow. The rest of the §3 stack chain
(`std::wstring` destructor inside `sync_combo`/`locate_source_combos`/
`sync_mirrors`) remains the strongest evidence available; what is
ruled out is specifically the mechanism proposed in §3, not the general
location.

### 5. Where to resume

1. ~~**Next candidate, not tried:** `combo_item()`/`wide_from_ansi()`
   (line ~798-857)~~ **instrumented and refuted, see §6.**
2. ~~**Also untried:** `ComboEnumeration`/
   `collect_original_combos` itself (line 814-828)~~ **instrumented and
   refuted, see §7 (local buffer) and §11 (vector destruction +
   the trip through `LPARAM`, exhausted).**
3. Repeat the same stack scan (`info proc mappings` + `x/400gx $rsp`
   + `addr2line`) on the VPS/Debian once it is understood why it does
   not reproduce there; it might not be "the bug does not reproduce",
   but "it reproduces yet does not manifest as a visible abort" under
   GCC 14 (a different heap layout). Not verified.
4. The manual stack-scan technique (not depending on broken `bt`/unwind)
   and the post-buffer canary technique (not depending on ASan/valgrind,
   both already ruled out on Fedora due to colliding with
   `wine-preloader`, §7 of `02-fedora-pending.md`) remain reusable
   methods for any future crash without reliable DWARF/CFI in this
   project, they are not specific to this bug.

### 6. `combo_item()`/`wide_from_ansi()`, instrumented and **refuted** (same day, hp-15)

Second candidate from §5 (point 1), the other stretch of the same §3
stack chain (`combo_item` feeds `wide_from_ansi`, whose result is the
`item` that `sync_combo` passes to `CB_ADDSTRING` in the `for` loop on
line 890).

Temporary instrumentation applied to both functions
(`src/port/original/opus_win95_chrome.cpp`, reverted afterward, not
committed, diff confirmed clean against `HEAD` after reverting):

- `combo_item()`: a `length + 1`-byte buffer over-reserved with 16
  `0xCD` canary cells, comparing `CB_GETLBTEXT`'s return value
  (`copied`) against `CB_GETLBTEXTLEN` (`length`).
- `wide_from_ansi()`: a buffer of `count` `wchar_t` over-reserved with 16
  `0xCDCD` canary cells, comparing the return value of the second
  call to `MultiByteToWideChar` (`written`) against the first
  (`count`).

**5/5 runs** (`gdb -q --batch -ex run -ex "bt full"`, `Xvfb :99`,
build rebuilt from `HEAD` with the instrumentation): DIAG output
**byte-identical across the 5 runs** (same `md5sum`), more
deterministic than the `mirror_text` instrumentation in §4, which
already showed 1/5 signature variation. Example (one run syncing a
combo with 5 entries):

```
[combo_item DIAG] index=0 length=12 copied=12 guard_cells_clobbered=0/16
[wide_from_ansi DIAG] count=13 written=13 guard_cells_clobbered=0/16
[combo_item DIAG] index=1 length=12 copied=12 guard_cells_clobbered=0/16
[wide_from_ansi DIAG] count=13 written=13 guard_cells_clobbered=0/16
...
```

`copied == length` and `written == count` in the 10 DIAG lines across
the 5 runs, **no canary cell ever touched** (`guard_cells_clobbered=0/16`
in 100% of invocations). The crash **persists in 5/5 runs**, exact same
signature all five times (`free(): invalid next size (normal)`,
`SIGABRT`), even more consistent than the §1 baseline (which varied
between signatures). **Hypothesis refuted:** neither `combo_item()` nor
`wide_from_ansi()` write beyond what their lengths report; the
`CB_GETLBTEXTLEN`/`CB_GETLBTEXT` pair and the double-call
`MultiByteToWideChar` pattern behave correctly in this Wine
reimplementation, just as already confirmed for `GetWindowTextLengthW`/
`GetWindowTextW` in §4.

**Cumulative reading (§4 + this point):** the three explicit-size
read/write blocks inside `sync_combo` and its direct calls
(`mirror_text`, `combo_item`, `wide_from_ansi`) are ruled out as the
source. The §3 stack chain remains the strongest evidence available
(the real crash is a `free()` of a `std::wstring` in that code
neighborhood), but the concrete mechanism is still not isolated. Point 2
of §5 (`ComboEnumeration`/`collect_original_combos`, line 814-828)
remains pending, see §7, also refuted.

**Build restored:** `opus_original_engine` and `WORD1` rebuilt after
reverting the instrumentation, the binary in `bin/` again matches
`HEAD`'s code exactly, no residual instrumentation left.

### 7. `ComboEnumeration`/`collect_original_combos`, instrumented and **refuted** (same day, hp-15)

Point 2 of §5, the last pending candidate from the §3 stack chain. Of
the two pieces in this block (`opus_win95_chrome.cpp:814-828`), only
one has the shape "Win32 API fills a buffer of explicit size" that the
canary method can directly test: the `wchar_t
class_name[64]` in `collect_original_combos`, filled by
`GetClassNameW(candidate, class_name, 64)`. The other, the
`std::vector<HWND> combos` growing via `push_back` on each
`EnumChildWindows` callback, is standard C++ memory management, not
an API writing into our buffer with an explicit size; there is no
external bound to instrument the same way (its own internal capacity
management is not the kind this project has seen fail). Only
`class_name` was instrumented.

Temporary instrumentation (reverted afterward, not committed, diff
confirmed clean against `HEAD`): a `wchar_t[64 + 16]` buffer, 16
`0xCDCD` canary cells beyond the 64 limit passed as `nMaxCount`,
comparing `GetClassNameW`'s return value (`written`) and checking the
canary cells after the call. (Side note: removing the buffer's original
zero-initialization to fill it with the canary pattern required adding
an explicit `class_name[0] = L'\0'` for the `written == 0` case,
`GetClassNameW` does not guarantee terminating the buffer on failure,
and the original code relied on zero-initialization for that case. It
does not affect overflow detection, which is already evaluated before
that branch.)

**5/5 runs**, same method (`gdb -q --batch -ex run -ex "bt full"`,
`Xvfb :99`): DIAG output **byte-identical across the 5 runs**
(same `md5sum`, as in §6), 20 DIAG lines per run (each call to
`EnumChildWindows` walks more candidate windows than what `combo_item`
was processing, hence double the invocations compared to §6). Observed
`written` values: `4, 6, 7, 8, 10, 13`, always well under the 64 limit.
**`guard_cells_clobbered=0/16` in 100% of the 20×5=100 invocations.**
The crash persists in 5/5 runs, exact same signature
(`free(): invalid next size (normal)`, `SIGABRT`).
**Hypothesis refuted:** `GetClassNameW` respects its `nMaxCount` in this
Wine reimplementation, just as already confirmed for the other three API
pairs in §4 and §6.

**Final cumulative reading (§4 + §6 + §7):** with this point the whole
§3 stack chain (`sync_combo` → `locate_source_combos` →
`collect_original_combos`) is exhausted regarding explicit-size writes
onto our own buffers, the four Win32 APIs involved
(`GetWindowTextW`, `CB_GETLBTEXT`, `MultiByteToWideChar`,
`GetClassNameW`) all behave correctly. None of the four is the
source. Two possible readings remain, neither instrumented yet:

1. The real corruption is not in this code block at all, the §3 stack
   chain is raw stack memory (not a real unwind, see §3), so it could
   be showing already-returned calls, not the origin point. It would be
   necessary to repeat the manual stack scan on a fresh run and check
   whether the same chain appears consistently, or varies between
   runs (not verified yet).
2. The corruption is in `std::vector<HWND> combos`'s own management
   (growth/realloc) or in something outside this file that corrupts a
   neighboring chunk that later gets freed here, neither has a direct
   canary method applicable; it would require a different tool (the
   `02-fedora-pending.md` §3 checklist, symbolizing frame #0, remains the
   most promising unexplored avenue).

**Build restored:** `opus_original_engine` and `WORD1` rebuilt again
after reverting this instrumentation too.

### 8. Comparison against the environment that does not reproduce (`/vps`, Debian 13), reopens and re-closes `valgrind`, for a reason different from and broader than `wine-preloader`

With the §3 stack chain exhausted (§4, §6, §7, all four without
result), the complementary angle was tried: instead of continuing to
search on Arch (where the crash reproduces), compare against the
environment that **does not** reproduce (Debian 13/GCC 14.2/wine-10.0,
the VPS at `~/.ssh/config` alias `vps`) to narrow down the environment
variable, as was left pending in `02-fedora-pending.md` ("It is not yet
known which of these variables... is the one that makes the
difference").

**Rebuild and baseline reconfirmation.** The VPS repo was already up to
date with `ae8b0cb` (this session's commits, `d260424`/`ddd28dc`, are
docs only, no code diff, the build is equivalent).
`opus_original_engine`/`WORD1` rebuilt clean on the VPS.

**New tooling note, specific to the VPS:** `gdb -q --batch -ex run
--args wine WORD1.exe.so` fails there with `"/usr/bin/wine": not in
executable format`, in this Debian package, `/usr/bin/wine` resolves
(via `update-alternatives`) to `/usr/bin/wine-stable`, a **POSIX shell
script** that decides at runtime whether to use `wine32` or
`wine64` (`wine32` is missing, so it falls back to `wine64`). `gdb --args`
needs to be able to load the executable as a BFD object for the implicit
`run` command, and a script is not an ELF object, hence the error.
**Fix:** invoke `/usr/lib/wine/wine64` directly with
`WINELOADER=/usr/lib/wine/wine64` exported (which is what the script
ends up doing anyway). Does not apply on Arch, where `/usr/bin/wine` is
the real binary.

**Baseline reconfirmed, 5/5 runs** (`gdb -q --batch -ex run -ex "bt
full" --args /usr/lib/wine/wine64 WORD1.exe.so`, the same `Xvfb :99`
already running from an earlier VPS session): all five reach the same
startup point (`fixme:dwmapi:DwmSetWindowAttribute ... stub`) and
afterward **do not crash**, timeout at 60s (`exit 124`), no further
output. Matches exactly what was already documented in §11.2, now
reconfirmed on the current `HEAD` (7 memory-migration commits after the
§11.2 build).

**Important clarification on what "does not reproduce" means here:**
the process does not get stuck or blocked at that point, it reaches a
genuinely restful state (a Windows message loop waiting for user input
that, under `Xvfb` with no interaction, never comes). It is the normal
behavior of an idle GUI app, not a different kind of startup failure.
It confirms, independently (not just repeats) the §11.2 reading: the
startup code, including the `sync_combo`/`locate_source_combos` block
this document has spent three sections instrumenting, runs to
completion without aborting in this environment.

**Side finding: there is no `wine-preloader` in this Debian package.**
`02-fedora-pending.md` §7 attributes the ASan/valgrind block
specifically to `wine-preloader`'s address-space reservation on
Fedora. Searching for that binary on the VPS
(`/usr/lib/wine/`) finds nothing, and it also does not exist on
Arch/hp-15 (also verified, same result). This opens the possibility
that the valgrind block is not universal, only specific to Fedora's
packaging, worth reopening the question.

**Valgrind on the VPS, runs clean, 240s, zero errors.** Without
`wine-preloader` in the way, `valgrind --error-exitcode=99` starts
without a problem, reaches the same startup point, and runs 240s at
rest (the same idle state as above) without logging a single memory
error. Initially promising, but see the next point before reading it as
"the code is clean there".

**Direct control on Arch (where the crash does occur), valgrind does
not see it.** To avoid over-interpreting the VPS's clean result (is it
clean because there is no bug, or because valgrind is not looking at
what needs looking at?), the same `valgrind --error-exitcode=99` was run
directly against the Arch binary, where the crash **does** reproduce.
Result: the crash **occurs just the same under valgrind**, the same
glibc `free(): invalid pointer` prints on standard output, followed by
the same broken `dbghelp` backtrace attempt already documented in
hp-15 §3 (`elf_search_auxv can't find symbol`, `dwarf2_get_cie wrong CIE
pointer`), but **valgrind's log records no error at all**, neither
before nor during. Verified with `-v` that `malloc`/`free`
interception is indeed active (`REDIR:
... libc.so.6:malloc redirected...`, `REDIR: ... libc.so.6:free
redirected...`, both logged *before* `ntdll.so` loads, so they cover
all the code that runs afterward, including
`WORD1.exe.so`). **The exact cause was not investigated** for why
memcheck does not see this corruption with interception confirmedly
active, unverified hypotheses: it could be a `free()` that does not go
through `libc.so.6`'s redirected symbol via some internal Wine/Winelib
path, or the bug could depend on the exact glibc heap layout/timing in
a way that valgrind's instrumentation (much slower, with its own
allocator) simply does not trigger.

**Consequence: the optimistic reading of the previous point is
retracted.** The clean 240s result on the VPS **is not evidence** that
the `sync_combo`/`locate_source_combos` block is bug-free there, it is
evidence that valgrind is not a useful tool for this specific
corruption, in any environment tested so far, for a broader reason
than the `wine-preloader` block already documented for Fedora.
`valgrind` is re-closed as an avenue, with this new, broader reason,
updated in `02-fedora-pending.md` §7.

**The only thing that stands as a solid result from this section:** the
independent reconfirmation that Debian/GCC 14.2/wine-10.0 does not
reproduce (§11.2 still held on the current `HEAD`) and that it reaches
the same startup point as Fedora/Arch cleanly before settling into
normal rest. The underlying question, whether it is the Wine version,
the GCC version, or some other environment difference that is the
causal variable, remains open. No attempt was made to isolate it by
installing a newer Wine or a GCC ≥15 on the VPS: both require
system-level changes (the WineHQ repo + enabling i386 multiarch, or
backports/sid for GCC) on a machine shared with other services, left
pending an explicit decision before touching system packages there, not
attempted in this session.

**Processes cleaned up:** none left residual, either on the VPS
(`timeout` killed everything, verified with `pgrep`) or locally
(verified the same way).

### 9. Symbolizing frame #0, done, with line-level precision; refutes the "Wine DLL" hypothesis from `02-fedora-pending.md` §3

A pending item carried over from Fedora (§5/§7 of this document, and §3
of `02-fedora-pending.md`), where frame #0's address
(`0x00006FFFFFC1B75F`) did not fall inside `WORD1` and it was
hypothesized, without confirmation, to be `user32`/`gdi32`/`ntdll`.
hp-15 §3 had already narrowed the region to `libc.so.6` by eyeballing
`$pc`; this session takes it to an exact symbol and line.

**Method:** `gdb -q --batch -ex "set debuginfod enabled on" -ex run -ex
"print/x \$pc" -ex "info proc mappings" --args wine WORD1.exe.so`,
identify the row of `info proc mappings` that contains `$pc` (`libc.so.6`'s
base is the start of the `r-xp` mapping minus its file offset), subtract
the base from `$pc` to get the offset within the file, and
`addr2line -e <debuginfo cached by debuginfod> -f -C -p <offset>`, the
full `debuginfo` (not just dynamic symbols) was already in
`~/.cache/debuginfod_client/` from the hp-15 §3 verification, indexed by
build-id (`503200d7fda94a5dc6058d7e0694e5d1dcb2e372`, confirmed with
`readelf -n`). `nm -D` on the system `.so` (without debug info) gives a
misleading result (`pthread_key_delete`, the nearest exported dynamic
symbol), do not use that path without the real `debuginfo`.

**Result, 3/3 runs, two distinct signatures** (`free(): invalid
pointer`, `free(): invalid next size (normal)`): **`$pc` identical
across all three** (`0x00007ffff7c9a17c`), resolved to
`__pthread_kill_implementation` in `nptl/pthread_kill.c:44`. This is
the generic `abort() → raise() → pthread_kill()` tail, the same for
any glibc abort regardless of which heap check triggered it,
consistent with the four signatures from §1/hp-15-§1 all sharing this
same frame #0.

**Conclusion:** frame #0 **is not a Wine DLL**, the
`02-fedora-pending.md` §3 hypothesis (based on an address from an old
Fedora build that no longer applies, `WORD1+0x1FD57C`) is refuted. It
is, as hp-15 §3 already suggested more coarsely, internal glibc code,
and being the generic signal-delivery machinery, **it does not by
itself carry any information about the origin of the corruption**: any
corrupt `free()`/`malloc()` ends up exactly here. The useful point in
the chain remains further up (`malloc_printerr`/`_int_free`, and from
there to the real caller in `WORD1` code), which is where hp-15 §3's
manual stack scan (not a real unwind, but cross-referenced with
`addr2line` against `WORD1.exe.so`) had already found the
`sync_combo`/`locate_source_combos` chain, exhausted in §4/§6/§7 of
this document without result.

**`02-fedora-pending.md` §3 item closed** with this result, no longer
pending a retry on Fedora, the address/symbol is consistent across
environments (the same generic class of glibc address), only changing
numerically due to ASLR/glibc version, not in nature.

### 10. `C_FormatLineDxa` instrumented, clean negative result: it **is not executed** before this startup's crash

Resumes the original §7 candidate ("review `C_FormatLineDxa` and its
neighborhood by hand"), which had been read but never instrumented in
§9/§10 (the Fedora session, before hp-15 shifted the focus to
`sync_combo`/toolbar). `src/Opus/wordtech/format.c` is a restricted
tree (`CLAUDE.md`), explicit authorization confirmed with the user
before editing, without going through the issue process since the
instruction was direct.

**Temporary instrumentation (reverted afterward, not committed, diff
confirmed clean against `HEAD`):**

1. An entry log on every call (a counter + `ww`/`doc`/`cp`/`dxa`),
   before any early `return`.
2. A log at each of the function's two early `return`s (the
   re-entrancy guard `vrf.fInFormatLine`, line ~540; the cache-hit
   "Just did this one", line ~591) and at the point where the code
   proceeds into full execution.
3. A real canary (not of cells, but against the actual heap
   allocation) at the only point in the function that writes into the
   shared `vhgrpchr` buffer **without a range check**, the code's own
   comment says so explicitly: `/* Note: no need to check for
   sufficient space */` (line ~2601, the end-of-line `chrmEnd`
   terminator). Compared `bchrBreak + cbCHR` (what the code is about to
   write) against `CbOfH(vhgrpchr)` (the handle's real allocated size,
   via `OpusCbOfH`, not `vbchrMax`, which is only the count the code
   itself keeps and could be out of sync with reality under an
   LP64-size bug like the one already confirmed in
   `bitapp.h:29`).

**Result, 5/5 runs (two batches, the first with only the point-3
canary, the second also adding the entry/exit log from points 1-2):
zero DIAG lines across the ten combined runs.** The crash occurs with
the usual signature and startup point
(`DwmSetWindowAttribute` stub → `free()`/`SIGABRT`), confirming that the
capture pipeline works (the rest of Wine's output is visible normally),
the absence of DIAG is not an instrumentation problem.

**Conclusion: `C_FormatLineDxa` is never called** during this specific
startup (a freshly opened blank document, no typing or interaction),
it does not even reach the re-entrancy guard evaluation, which is the
function's first executable line. Consistent with there being no text
to paginate yet: the crash occurs entirely during window/toolbar
construction (the §3 `sync_combo`/`locate_source_combos` chain,
exhausted in §4/§6/§7), before the blank document needs its first
line-formatting pass.

**This does not rule out real bugs inside `C_FormatLineDxa`**, it only
rules out that it is the cause of *this* specific startup crash. It
remains a legitimate candidate for any crash involving real pagination
(with typed text), a path that `opus_word1_ui_test --typing`/
`--font-typing` (`02-fedora-pending.md` §5, never run) would actually
reach, but it is a different bug, with a different repro, not the one
this document has been tracking since §1.

**Build restored** after reverting both batches of instrumentation.

### 11. `~ComboEnumeration` revisited, `std::vector<HWND>` destruction confirmed clean; the trip through `LPARAM` also refuted

Point 2 of §5 (`ComboEnumeration`/`collect_original_combos`), taken
further than in §7, where only the `GetClassNameW` buffer inside
`collect_original_combos` had been instrumented (refuted). This time
the target is the destruction of `std::vector<HWND> combos` itself,
the `ComboEnumeration::~ComboEnumeration` frame appears literally in
the §3 stack chain, and, separately, the trip of the `&enumeration`
pointer through `LPARAM` in `EnumChildWindows` (an LP64-class bug
precedent never before tested for this specific pointer,
`bitapp.h:29`).

**Temporary instrumentation** (`opus_win95_chrome.cpp`, reverted
afterward, diff confirmed clean):

1. Logging `&enumeration` (as a pointer and as `LPARAM`) right before
   `EnumChildWindows`, and logging the `parameter` received in
   `collect_original_combos`, to compare both values.
2. Replacing the vector's implicit destruction (on exiting
   `locate_source_combos`) with an explicit, instrumented
   `std::vector<HWND>().swap(enumeration.combos)`, the swap-with-empty
   idiom forces a real `delete[]` at a point we control, with logging
   immediately before and after.

**Result, 5/5 runs:**

- **`locate_source_combos` is called twice per run** (never before
  documented): the first with `enumeration.combos` empty (0 combos
  found, the child-window enumeration runs before the toolbar is fully
  populated), the second with 3 (`style`/`font`/`size`, consistent with
  what was already seen in §4/§6). Both times, the `parameter` received
  in `collect_original_combos` **matches exactly** the original
  `&enumeration` logged before the call, no truncation or corruption,
  across the 38 combined invocations per run over the two rounds.
  **The `LPARAM` trip hypothesis is refuted.**
- **The forced vector destruction completes without aborting both
  times, in 5/5 runs**, regardless of that run's crash signature
  (`free(): invalid pointer` / `free(): invalid next size`), the
  `after combos dtor, ok` log always prints. **`~ComboEnumeration` is
  not where the abort occurs.**
- **No DIAG line ever appears after the crash message** in any run,
  confirming the abort occurs *after* both calls to
  `locate_source_combos` (with their destructions) have already
  finished cleanly, in code further along the §3 stack chain
  (`sync_mirrors`/`toolbar_window_proc`) that **none of the hp-15
  sessions has instrumented yet.**

**Conclusion:** this closes point 2 of §5 completely, this time
thoroughly (not just the local buffer of §7, also the shared resource's
destruction and the pointer-passing mechanism). The full §3 chain
inside `sync_combo`/`locate_source_combos`
(`mirror_text`, `combo_item`, `wide_from_ansi`, `GetClassNameW`,
`LPARAM`, `~ComboEnumeration`) is exhausted without finding the cause.
The most useful new data point from this session is the **time
bracketing**: the abort occurs strictly after the second call to
`locate_source_combos`, not inside it, the natural next candidate is
instrumenting `sync_mirrors`/`toolbar_window_proc` itself (the code
that calls `locate_source_combos` and that keeps running afterward),
not going back to the same five already-refuted points.

**Build restored** after reverting this instrumentation too.

### 12. `sync_combo`/`sync_mirrors` revisited, major finding: the crash occurs *inside* the `CB_ADDSTRING` loop, at a different index each run, on valid data

Follows the concrete lead left by §11 (the abort occurs after
`locate_source_combos` finishes cleanly twice). Point 1 of §5
(`combo_item()`/`wide_from_ansi()`) had been considered exhausted in
§6, but only the pair inside the population `for` loop (line ~890-892)
had been tested there. This session found a **third** length/text pair
inside `sync_combo`, never touched before: `GetWindowTextLengthA(source)`/
`GetWindowTextA(source, ...)` in the `else` branch of
`!combo_or_child_has_focus(mirror)` (line ~903-905), instrumented the
same way as the previous ones (a 16-cell canary). **Zero DIAG lines in
5/5 runs**, that branch is never reached before the crash.

**Follow-up instrumentation** (branch tracing in `sync_combo`, no
canary) revealed why, and along the way, this session's real finding:

- `sync_combo` is called **repeatedly** before the crash: first several
  times with `source == nullptr` (the three combos, before
  `locate_source_combos` resolves them, consistent with §11), then once
  more with `source` already resolved (`0x100c0`/`0x200e4`/`0x300cc` in
  the three runs). At that point, **`SendMessageA(source, CB_GETCOUNT, 0,
  0)` returns `count=1395`**, identical in 3/3 runs, a number far above
  what one would expect for a font/style/size combo of a 1989 word
  processor.
- With `count=1395` the population `for` loop (lines 890-894, already
  walked in §6 but only up to `index=4`) was instrumented again with
  per-iteration tracing. **The first ~16-20 length values from
  `combo_item` are identical to those already seen in §4/§6** (12, 12,
  5, 19, 24, 20, 4, 9, 7, 11, 8, 20, 11, 21, 17, 16, ...), they are real
  font/style names, not garbage or uninitialized memory. **The crash
  occurs *inside* this loop, at a different index each run: 5, 14, and
  15** across three consecutive runs with the same build, while
  processing valid data, neither near the 1395 limit nor in an
  out-of-range index zone.

**Reading, changes the underlying diagnosis:** every write this project
directly controls and can instrument (`mirror_text`, `combo_item`,
`wide_from_ansi`, `GetClassNameW`, the third pair in `sync_combo`, the
`LPARAM` trip, `ComboEnumeration`'s destruction) has come out clean,
repeatedly, across separate sessions. And yet the crash persists, **at
a variable point inside a loop that makes the same Win32 call
(`CB_ADDSTRING`) repeatedly over data that is itself valid.** That
combination, same data, same code, a failure point that moves between
runs, while every local boundary check comes out clean, is the
characteristic signature of **accumulated heap corruption inside
Wine's implementation of the ComboBox control** (the internal path that
`CB_ADDSTRING`/`CB_RESETCONTENT` exercise in `user32`/`comctl32` under
wine-staging 11.15), not a bug in `Opus`/`port` code that this
investigation can keep instrumenting with local canaries. **Not
confirmed with a tool that looks directly at Wine's heap** (the
`+heap` and `valgrind` attempts from §2/§8 saw nothing, but for reasons
already documented as inconclusive for this class of bug), it is an
inference by elimination, not direct proof.

**Open question, not pursued this session:** where does `count=1395`
come from? It could be a real (if unusually high) reflection of the
number of fonts Wine enumerates on this system via fontconfig, or it
could itself be a symptom that the original `source` combo already had
its internal list damaged by an earlier `CB_RESETCONTENT`/`CB_ADDSTRING`
cycle on the *same* handle at another point in startup, it was not
compared against the real count of fonts installed on this system, nor
was it checked whether `source` (the original Word combo, not the
mirror) receives its own `CB_ADDSTRING` load elsewhere in the code
before this point.

**Practical consequence for the next session:** continuing to
instrument `Opus`/`port` code with local canaries is no longer the most
promising avenue, the eight candidates of that class (§4, §6, §7, §11,
and the third pair in this section) all came out clean. The remaining
avenues, in order from most to least direct:

1. Assess whether `count=1395` is plausible (count real system fonts,
   `fc-list | wc -l` or equivalent), cheap, not tried.
2. Repeat the same experiment with a different `source` or forcing
   `count` to a small value (a temporary diagnostic patch, revert
   afterward) to see whether the crash disappears, would confirm or
   refute that the loop's volume is the relevant variable, not its
   content.
3. Instrument directly around the `CB_ADDSTRING` call with a bounded
   heap consistency check (not the whole process, only the immediate
   surroundings of that call), something neither `+heap` nor `valgrind`
   managed to give in this investigation when looking at the whole
   process.
4. Consider the hypothesis of a real Wine bug (`wine-staging
   11.15`/GCC 16 in the `COMBOBOX_InsertString` path or similar) and
   try another Wine version in this same Arch environment, not tried,
   would require installing a different Wine (a system change, to be
   confirmed with the user before touching it, as already left pending
   for the VPS in §8).

**Build restored** after reverting all instrumentation from this
section (three batches: the third pair's canary, branch tracing, loop
tracing).

**Follow-up, point 1 from the list above, answered:** in this same
environment (hp-15, EndeavourOS), `fc-list | wc -l` gives **2553**
installed font faces, `fc-list : family | sort -u | wc -l` gives
**1965** unique family names. `count=1395` falls **within** that range
(less than both totals), not far-fetched as a reflection of a real
font enumeration, unlike what would be a clearly impossible value
(negative, `INT_MAX`, etc.). It does not fully close the question:
`fc-match "Courier New"` resolves by substitution
(`Liberation Mono`) but `fc-list | grep -ic courier` gives **0**,
"Courier New" (the name `combo_contains` looks for to classify the
combo as `source_font`, and which does appear as real data in the §12
`for` loop) is not a literal installed family name on this system, so
the combo is not enumerating raw `fc-list` 1:1, it is Wine's GDI
enumeration (`EnumFontFamilies` or equivalent), which can generate one
entry per font×charset/script combination and therefore a total larger
or different from `fc-list`'s. **Plausible order of magnitude, exact
provenance unconfirmed.**

**Follow-up, point 2 from the §12 list, answered: refuted.**
Control experiment: `count` intercepted right after
`SendMessageA(source, CB_GETCOUNT, 0, 0)` and forced to `20` when it
exceeds that value (`opus_win95_chrome.cpp`, temporary instrumentation
reverted afterward, diff confirmed clean), leaving the rest of
`sync_combo` intact, the population loop really runs, just 20 times
instead of 1395.

**5/5 runs, the cap was applied** (the log `capping count from 1395 to
20` present in all five) **and the crash occurred exactly the same all
five times**, the usual signatures (`free(): invalid next size`,
`free(): invalid pointer`, `malloc(): unaligned tcache chunk detected`).
**The loop's volume is not the relevant variable**, with 20 iterations
instead of 1395 the result is identical. This **directly rules out**
the §12 reading of "corruption accumulated inside Wine's ComboBox
implementation from the volume of repeated `CB_ADDSTRING` calls",
if it were a matter of volume/accumulated wear, capping to 20 should
have changed something (even just the signature or the frequency), and
it changed absolutely nothing.

**Corrected reading:** the corruption does not depend on *how many*
times `CB_ADDSTRING` is called in this loop, it is consistent with it
already being present **before** reaching this point (the `free()`
that aborts fires at the first opportunity it gets to find the
corruption, whether that opportunity is iteration 1 or 1395 makes no
difference), not with this specific loop *causing* it by accumulation.
This reopens with more force the alternative reading that §5 (point 3)
and §12 already left open without pursuing: the real origin is
elsewhere, and everything this investigation has been instrumenting
inside `sync_combo`/`locate_source_combos`/`collect_original_combos`
(eight refuted candidates in total) may be entirely innocent, the
`free()` we see abort there is only the first point of contact with a
heap already damaged by something that runs **before** in startup, not
within this chain of functions at all.

**Practical consequence:** stop instrumenting code within the
`sync_combo`/`locate_source_combos` chain, with this result, continuing
there no longer has support. The avenue with the most support left is
to narrow backward: what runs *before* the first call to
`sync_mirrors` in startup (creation of toolbar windows/controls,
`WM_CREATE` of `toolbar_window_proc`, or even further back), not
pursued yet.

**Build restored** after reverting this instrumentation.

### 13. What runs before `sync_mirrors`, bisecting `WM_CREATE`: **`WM_CREATE` itself, including its first call to `sync_mirrors`, comes out clean**

Directly resumes the pending item from §12 ("narrow backward what runs
before the first call to `sync_mirrors`"). `sync_mirrors` is called
from two places in `toolbar_window_proc`: once inside `WM_CREATE`
(line 2590, unconditional, when the toolbar is created), and again
inside `WM_TIMER` (line 2602, every 350 ms via `SetTimer(window,
kSyncTimer, 350, nullptr)`, set at the end of `WM_CREATE` itself). Until
this session it was not clear which of the two calls reaches the
`count=1395` iteration from §12, both routes go through the same
`sync_mirrors`.

**Temporary instrumentation** (`opus_win95_chrome.cpp`, reverted
afterward, diff confirmed clean): an `fprintf` checkpoint after each
significant step of `WM_CREATE`'s body, `ToolbarState` allocation, the
sprite's `LoadImageW`, `CreateFontIndirectW`, each of the four
`create_combo`/`create_zoom_combo` calls, the initial
`SetWindowTextW`s, the `WM_SETFONT`s, `position_combos`, and finally
the call to `sync_mirrors` itself (before and after).

**Result, 5/5 runs: the full checkpoint sequence always prints,
including `sync_mirrors returned ok` as the last line before the
crash.** That is: **the whole body of `WM_CREATE`, state allocation,
sprite loading, font creation, creating the four combos, initial text,
applying the font, positioning, and the first full call to
`sync_mirrors`, finishes with no problem in all five runs.** The crash
occurs **after** `WM_CREATE` returns `0`.

**Reconciling with §12, not a contradiction, a more precise
localization:** on the first call to `sync_mirrors` (inside
`WM_CREATE`), `locate_source_combos` still finds no real combos
(§11: "the first call... empty"), the three `sync_combo` calls within
that first pass do an immediate `early-return` (`source == nullptr`)
and never get close to the `CB_ADDSTRING` loop. The iteration with
`count=1395` that crashes in §12 **has to be the one from the second
call to `sync_mirrors`, triggered by `WM_TIMER` 350 ms later**, it is
the only other route into `sync_combo`, consistent with
`locate_source_combos` finding the three real combos on its second
invocation (also documented in §11).

**Consequence:** the window of interest is no longer "everything that
runs before `sync_mirrors`" in the abstract, it is specifically **what
happens between `WM_CREATE` returning (the toolbar just created, the
window visible) and the 350 ms timer firing the second call to
`sync_mirrors`**. During that interval Wine's message loop keeps
pumping: other windows may be being created (the document pane, other
controls), `WM_PAINT`/`WM_SIZE` being processed, or any other `WORD1`
startup code running in parallel, none of it instrumented yet.
Combined with the §12 result (the `sync_combo` loop's volume is not the
relevant variable), the reading most consistent with all the evidence
gathered remains: the corruption does not originate inside the
`toolbar_window_proc`→
`sync_mirrors`→`sync_combo`→`locate_source_combos`→
`collect_original_combos` chain at all (nine candidates refuted there
across all sessions), it originates in other code running in parallel
during that 350 ms interval, and `sync_combo`'s first `free()` is only
the first point of contact with the damage.

**Not pursued this session:** instrumenting `WM_TIMER` itself (a log
before/after its call to `sync_mirrors`, to directly confirm it is that
call that crashes and not some third, unaccounted-for invocation)
and/or instrumenting what other windows/panes get created or what
messages get processed in the 350 ms interval between `WM_CREATE` and
the first `WM_TIMER`.

**Build restored** after reverting this instrumentation.

### 14. `WM_TIMER` instrumented, confirmed: it crashes on `tick#1`, inside the `sync_mirrors` call itself, before reaching anything else

Directly closes the question §13 left open. Temporary instrumentation
(`opus_win95_chrome.cpp`, reverted afterward, diff confirmed clean):
checkpoints at every step of `WM_TIMER`'s body, tick counter,
`suppress_sync_until`/`GetTickCount64()` values, before/after
`sync_mirrors`, before/after `subclass_all_document_panes`, and the
three conditional branches (ruler refresh, page-view startup, ruler
horizontal paint).

**Result, 5/5 runs: the crash occurs on the very first `tick#1`**
(`suppress_sync_until=0`, so the condition never suppresses anything),
**immediately after "calling sync_mirrors" and before
"sync_mirrors returned ok"**, that is, inside that call, it never gets
to return. **None of the five runs reach
`subclass_all_document_panes` or any of the later branches**, the
crash occurs before the code gets a chance to execute them.

**Unambiguously confirms the §13 hypothesis:** it is the second call to
`sync_mirrors` (the one from `WM_TIMER`, not from `WM_CREATE`) that
reaches the `count=1395` from §12 and crashes, there is no third route
or anything else inside `WM_TIMER` itself involved.
`subclass_all_document_panes` and the rest of `WM_TIMER`'s body are
**ruled out as candidates** for this same reason: they never execute
before the crash in any run.

**Consequence, sharpens the search window that remained open:** since
`WM_TIMER` calls `sync_mirrors` as its first step, nothing in
`WM_TIMER`'s own handler runs "in parallel" before the crash. The real
window of interest goes back to being, as in §13, **the ~350 ms
interval between `WM_CREATE` returning and this first `WM_TIMER`
arriving**, code that runs entirely outside `toolbar_window_proc`
(Wine's message pump, creation of other windows/panes, or other
`WORD1` startup code), not instrumented yet by this investigation. With
this, the ten candidates inside the
`toolbar_window_proc`/`sync_mirrors`/`sync_combo`/
`locate_source_combos`/`collect_original_combos` chain are exhausted
without exception, any next step chasing the corrupt-heap lead needs to
look outside this chain of functions.

**Build restored** after reverting this instrumentation.

### 15. Real backtrace of the crashing call, it is not the 350 ms `WM_TIMER`: it is a synchronous call from `FCreateMw`; corrects §14

Resumes the pending item from §14 ("instrument what other
windows/panes get created or what messages get processed in the 350 ms
interval"). Before instrumenting that interval, the more direct
avenue was tried first, a breakpoint on `sync_mirrors` itself with
`gdb` to capture the real backtrace at the moment of the crashing call,
and that avenue revealed that the premise of §13/§14 (that there are
two calls separated by ~350 ms governed by `SetTimer`) is incorrect.

**New blocker, not seen in previous sessions: file:line breakpoints
stopped resolving in this session.** `break
opus_win95_chrome.cpp:916` (`sync_mirrors`) and `break
opus_win95_chrome.cpp:2829` (`OpusCreateWin95Chrome`, `extern "C"`, not
in an anonymous namespace, ruling out the hypothesis that it was a
symbol-visibility problem) stay `<PENDING>` forever across 5 separate
runs under `gdb -x script --args wine WORD1.exe.so` (`set breakpoint
pending on`), even with both combined in the same script. `info
sharedlibrary` at the exact moment of `SIGABRT` reports **"No shared
libraries loaded at this time"**, not even `libc.so.6` appears
registered, with the process running real code. Also reproduced with a
trivial unrelated command (`break main` on `wine cmd /c "echo
hi"`): same result, eternal `<PENDING>`, zero shared libraries. It is a
generic `gdb`↔wine integration regression in this specific session, not
specific to `WORD1.exe.so` or to anonymous namespaces, which directly
contradicts what §12.1/§12.2 documented working in an environment
described as the same one (EndeavourOS, wine-staging 11.15, `gdb`
17.2). The cause of the regression was not investigated (it could be a
package update between sessions). Additionally, `gdb -p <PID>` on an
already-launched process fails with `ptrace: Operation not permitted`,
`/proc/sys/kernel/yama/ptrace_scope` is `1` in this environment, a Yama
block independent of the previous one; not touched (would require
privileges and explicit authorization, out of scope).

**Alternative technique used, no gdb, no touching `src/Opus/`:**
temporary instrumentation in `opus_win95_chrome.cpp` (anonymous
namespace, in `src/port/`, no restriction) with glibc's
`backtrace()` from `<execinfo.h>` (available because this file compiles
as native C++ under `winegcc`, nothing MSVC-specific) at three points:
before the call to `sync_mirrors` inside `WM_CREATE` (line 2590), before
the call inside `WM_TIMER` (line 2602), and, the one that turned out to
be decisive, at the start of `OpusSyncWin95Toolbar()` (line 2802),
which does `SendMessageW(vhwndWin95Toolbar, WM_TIMER, kSyncTimer, 0)`
**synchronously and directly**, without going through any message
queue or through `SetTimer`. Each capture also prints
`GetTickCount64()` and the address of the diagnostic function itself
(`&ChromeTraceDiag`) as a reference to be able to subtract each run's
ASLR and resolve the raw addresses against the static binary with
`addr2line -e WORD1.exe.so -f -C` (the same technique as §3, now
applied proactively instead of against an already-corrupted core).
Instrumentation reverted afterward
(`git checkout -- src/port/original/opus_win95_chrome.cpp`, clean diff
confirmed) and build restored.

**Result, 3/3 runs, the exact same pattern:**

```
[CHROME TRACE] WM_CREATE before sync_mirrors                           tick=T
[CHROME TRACE] OpusSyncWin95Toolbar direct call (init2.c) before ...   tick=T+~260..282ms
[CHROME TRACE] WM_TIMER before sync_mirrors                            tick=T+~260..282ms   <-- SAME tick, to the millisecond
free(): invalid pointer   (or double free or corruption (!prev))
```

The marker placed inside `OpusSyncWin95Toolbar` and the marker placed
inside the `case WM_TIMER` of `toolbar_window_proc` print **the exact
same `GetTickCount64()`** all 3 times, there is no way these are two
distinct events separated by real work; it is the same call seen from
two instrumentation points. Also, the measured real interval
(~260-282 ms) is **less than the 350 ms** of the `SetTimer`, a genuine
queued `WM_TIMER` never fires before the requested interval, which was
already a sign that this was not the mechanism. The
`SetTimer(window, kSyncTimer, 350, ...)` remains alive and armed, but
**the process dies before it ever gets a chance to fire even once**,
the "tick#1" that §14 identified and attributed to the real timer was
never the real timer.

**The capture inside `OpusSyncWin95Toolbar` unwinds 13 full frames**
(unlike the captures in `WM_CREATE`/`WM_TIMER`, which cut off at 3, the
same broken-CFI unwind limit at the Wine Unix/PE ABI boundary that
already blocked `gdb` in §3; here it does not apply because
`OpusSyncWin95Toolbar` is reached through a direct C function call,
without crossing that boundary until the final frame). Resolved with
`addr2line` against each run's real offset (base = `ChromeTraceDiag`'s
live address minus its static address, `nm -C WORD1.exe.so`):

```
__wine_spec_exe_wentry
wmain
wWinMain                    opus_original_startup_probe.cpp:512
OpusOriginalWinMain         wproc.c:516
FInitWinInfo                wproc.c:775
FInitPart2                  init2.c:659        (ElNewFile(stType, fFalse) -- a new, untitled document)
ElNewFile                   open.c:1353
FCreateMw                   open.c:595         (creating the document window -- NOT YET shown)
EndStartup1                 open.c:591         (direct call, not via the init2.c:703 EndStartup() wrapper)
DisplayRibbonInit           init2.c:817
OpusSyncWin95Toolbar        opus_win95_chrome.cpp:2803
ChromeTraceDiag              (this session's probe)
```

**Exact call-site location, confirmed by reading
`open.c:584-600`:** inside `FCreateMw`, under the comment `/* BEGIN
VISUAL DISPLAY OF WINDOW */`, there are two separate calls,
`EndStartup1()` on line 591 (if `vhwndStartup != NULL`) and
`EndStartup2()` on line 639, with the document window's real
creation/display **in between** (`ShowWindow(hwndMw, ...)` on line 600
is *after* `EndStartup1()`). That is: `sync_mirrors` crashes while
`FCreateMw` is still constructing the startup's first document window,
**before that window ever gets shown on screen**, triggered by the
side effect of
`EndStartup1()`→`DisplayRibbonInit()`→`OpusSyncWin95Toolbar()`, which
exists specifically to hide the classic ribbon and sync the Win95
toolbar as soon as the startup splash ends.

**Concrete correction to §13/§14:** there is no "second call to
`sync_mirrors` via `WM_TIMER` 350 ms later", there is a **single
extra synchronous call**, triggered by `SendMessageW` (not by the real
timer), that arrives recursively through a normal C function-call
route (`FInitPart2`→`ElNewFile`→`FCreateMw`→`EndStartup1`→
`DisplayRibbonInit`→`OpusSyncWin95Toolbar`), entirely inside
`FInitPart2` and never going through the top-level
`GetMessage`/`DispatchMessage`. The §13 reading ("the window of
interest is the ~350 ms interval during which Wine's message pump keeps
running") is refuted: there is no message pumping in between at all
between the two calls to `sync_mirrors` that do occur (the empty one
from `WM_CREATE`, and this one), it is a single synchronous branch of
execution on the main thread itself.

**Consequence, precise focus for the next session:** the candidate is
no longer "unidentified code running in parallel", it is the
construction of the first document window itself. `FCreateMw`
(`open.c`, between the Scribble `'D'`/`'E'` at line 566 and the point at
line 591) builds state (document, active `ww`, `hmwd`, controls) that
is **not yet finished** when `EndStartup1` triggers
`sync_mirrors`/`sync_combo` over the toolbar's combos, and
`locate_source_combos` does find real combos at this point (unlike the
first, empty call inside `WM_CREATE`, §13), suggesting that the "font"
controls the toolbar mirrors already exist by then, but the
document/window backing them may not. The obvious next step, not
attempted this session: instrument which concrete controls
`state->source_style` / `source_font` / `source_size` are at the moment
of the crash (it is already known that `locate_source_combos` finds
them, §12; still missing is identifying *which* `HWND`s they are and
whether they belong to the half-built document window) and/or review
what heap mutation occurs in the `FCreateMw` stretch between lines 566
and 591 that could leave a shared structure in an inconsistent state
for the later `free()` inside `sync_combo`.

**Not pursued this session:** diagnosing the `gdb` regression
documented above (it blocks continuing to use interactive breakpoints
until resolved); confirming whether the
`EndStartup1()`/`DisplayRibbonInit()` call at `open.c:591` is *always*
the one that crashes or whether on some run the real `SetTimer` gets to
fire first (not observed in 3/3, but not impossible either given that
the margin is only ~70-90 ms).

### 16. Concrete identity of the source_style/source_font/source_size HWNDs, the crash is always in `sync_combo(font)`, and `source_style` is never even attempted

Resumes the "obvious next step" that closed §15: instrument which
concrete HWNDs resolve `state.source_style`/`source_font`/`source_size`
at the moment of the crash, and whether they belong to the document
window `FCreateMw` is still constructing.

**Technique:** same family as §15, temporary instrumentation in
`opus_win95_chrome.cpp` (anonymous namespace, `src/port/`, reverted at
the end, clean build confirmed before and after). This time with
`fprintf(stderr, ...)` + `fflush` (not `backtrace()`) at three points:
`ChromeDumpHwndOwnership(label, hwnd)`, a new helper function that
dumps `HWND`, `IsWindow`, `IsWindowVisible`, `GetClassNameW`,
`GetWindowTextW`, `GetWindowRect`, and climbs the `GetParent` chain up
to the root (max 8 levels), called from inside `locate_source_combos`
(for each candidate found and for each of the three roles' final
result) and from inside `sync_mirrors` (right before each of the three
calls to `sync_combo`); and a second round with additional
instrumentation directly inside `sync_combo`, dumping the `mirror`,
printing `count`/`copied_count`, and adding a line per iteration of the
`CB_ADDSTRING` loop with the index and the real ANSI string about to be
copied.

**New gotcha, not documented before in this file:** `wchar_t` under
winelib compiles to 2 bytes (`-fshort-wchar`, to match Win32's `WCHAR`),
but this `.cpp`'s CRT is the system's native glibc, whose
`printf`/`fprintf` with `%ls` assumes a 4-byte `wchar_t`. Dumping a
`WCHAR` buffer directly with `%ls` produces garbage (illegible box
characters), it is not data corruption, it is a type-width
mismatch in the dump itself. **Solution:** convert with
`WideCharToMultiByte` to ANSI before printing with `%s` (the same
pattern as the `ansi_from_wide` function already existing in this file,
just applied in a local helper because `ChromeDumpHwndOwnership` sits
earlier in the file than `ansi_from_wide`). Any future instrumentation
in winelib code that prints wide text must go through this conversion.

**Result, 4/4 runs (`locate_source_combos`) + 3/3 detailed runs
(`sync_combo`), identical pattern:**

On the first call to `sync_mirrors` (inside the toolbar's own
`WM_CREATE`) `locate_source_combos` finds **0 candidates**, consistent
with §13, and the three roles stay `null`; the three `sync_combo` calls
do nothing (the `source == nullptr` guard).

On the second call (the synchronous one identified in §15, via
`FCreateMw`→`EndStartup1`→`DisplayRibbonInit`→`OpusSyncWin95Toolbar`),
`locate_source_combos` always finds exactly **3 candidates** of class
`ComboBox`, with this identity stable across runs (only the exact
`HWND` values change, not the structure):

| Candidate | rect | visible | parent chain | classified as |
|---|---|---|---|---|
| A | (52,50,204,71) | yes | `OpusSdmDialog` → `OpusApp("Microsoft Word")` | `source_font` (contains "Courier New" and "Arial") |
| B | (254,50,310,71) | yes | `OpusSdmDialog` → `OpusApp` | `source_size` (contains "24" and "72") |
| C | (6196,113,6348,134) | **no** | `OpusSdmDialog` → `OpusMwd` → `OpusDesk` → `OpusApp` | **none**, `source_style` stays `null` |

**A and B are legitimate, already-populated controls** from the
classic ribbon (`OpusSdmDialog`, a dialog directly under `OpusApp`),
nothing to do with the half-built document window. The closing §15
hypothesis ("the `source_*` might belong to the still-unfinished
document window") **is not confirmed for A/B**.

**C does fit that hypothesis**, it is off-screen
(`rect.left = 6196`, a typical "not yet positioned" value), invisible,
and hangs off a distinct, deeper class chain (`OpusMwd`/`OpusDesk`)
that does not appear on A/B, it is almost certainly a control of the
document window (`hwndMw`/`ww`) that `FCreateMw` is constructing at
that instant. But **C is never classified as `source_style`** in any of
the 4 runs, `locate_source_combos`'s `else if` requires
`combo_contains(combo, "Normal")` or `CB_GETCOUNT > 0`, and C meets
neither at this exact point (consistent with being a freshly created
combo, with no items yet). So `state.source_style` stays `null` all 4
times, and the first `sync_combo` (style) is a safe no-op. **C is a
witness that the document window is half-built, but not the cause of
the crash.**

**The crash is always inside `sync_combo(mirror=state.font_combo,
source=state.source_font)`**, never in style (no-op, above) nor does
it ever reach size (the flow dies at font first). The `mirror`
(`state.font_combo`) is confirmed valid, visible, a child of
`OpusWin95Toolbar`/`OpusApp`, and already has the text `"Arial"` (set
in its own `WM_CREATE`), not a stale or reused handle. The `source`
(candidate A) reports **`count=1395`**, matching exactly the
independent finding from an earlier session (`fc-list` on this same
environment, commit `7185ce1`), confirming A is the real system-font
source via `fc-list`, and that both findings point to the same control.

**The `CB_ADDSTRING` loop index where it crashes, with real per-iteration
data:** the 3 detailed runs die in the neighborhood of
**idx=14/1395 ("DejaVu Sans Light")** or **idx=15/1395 ("DejaVu Sans
Mono")**, never before, never after, in 3/3. The glibc message varies
between runs (`free(): invalid pointer`, `free(): invalid next size
(normal)`, `double free or corruption (!prev)`), the three are
distinct detectors of the same kind of damage (corrupt heap metadata),
not evidence of three distinct bugs.

**Reading of this narrow band (14-15 of 1395), not pursued beyond
noting it:** an index that varies by ±1 between runs but stays within
such a narrow band, rather than scattered across the 1395 iterations,
or always fixed at 0, is the typical signature of heap corruption
**detected late**: the `free()`/`malloc()` that actually writes out of
bounds may have occurred well before (even outside `sync_combo`,
perhaps inside `locate_source_combos` itself walking 1395
`combo_contains`/`CB_FINDSTRINGEXACT`, or in earlier code in the
`FCreateMw`→...→`OpusSyncWin95Toolbar` chain), leaving a chunk with
inconsistent metadata; the `CB_ADDSTRING`/`combo_item`/`wide_from_ansi`
loop in `sync_combo` simply does enough allocate/free churn that, after
roughly the same number of operations each time, the allocator reuses
or tries to consolidate that already-damaged chunk and glibc's detector
fires. This **does not rule out** that the bug is in `sync_combo`
itself, but it does open a concrete hypothesis the next session has not
tried: use a detector that catches the real write at the moment it
happens (`valgrind`, already available in this environment, version
3.25.1 per previous sessions, or `MALLOC_CHECK_=3`/`mallopt` with more
aggressive checking) instead of waiting for glibc to detect it late by
chance of index.

**Consequence, focus for the next session:** (1) run under `valgrind`
(not tried in any session of this series so far despite being
available) to localize the real out-of-bounds write, instead of
continuing to read glibc's late detection point; (2) if valgrind is not
viable due to the Winelib/Wine overlap (possible, given this project's
history of tooling friction, see the `gdb` block in §15), instrument
`locate_source_combos` itself with the same technique from this
section (its 1395 `combo_contains` calls invoke
`SendMessageA(..., CB_FINDSTRINGEXACT, ...)` on the same 1395-item
combo, twice per candidate, comparable work volume to the loop that
was instrumented) to rule out that the corruption had already occurred
there, before `sync_combo` "discovers" it; (3) confirm C's identity
(the discarded candidate, `OpusMwd`/`OpusDesk`) against `FCreateMw`'s
code in `open.c`, suspected to be the style combo of the new document
window, but not confirmed against source this session.

**Not pursued this session:** running under `valgrind` (mentioned above
as a plan, not executed); confirming C against `open.c`; determining
whether the 14-15 band holds under a different system font set (would
depend on what lies between `fc-list` and these positions, so it is not
necessarily stable across machines).

### 17. `valgrind`, three attempts, none reach the crash; conclusion: not viable in this environment without more infrastructure work

Resumes the plan that closed §16: run under `valgrind` (3.25.1, already
available in this environment) to catch the real out-of-bounds write
instead of continuing to read glibc's late detection.

**Attempt 1, no mitigations, `--track-origins=yes`, 240s timeout:**
`valgrind --tool=memcheck --track-origins=yes --trace-children=yes
--error-exitcode=99 wine WORD1.exe.so`. Result: **timeout, never gets
to crash.** The largest log (the real `WORD1.exe.so` process,
distinguishable by `Command:` in the log) shows that in 240 real
seconds it only got as far as `NtQueryDirectoryFile`/registry access
(`wineboot`/`explorer` startup), without even getting to create the
application window. Loading
`libnvidia-glcore`/`libGLX_nvidia`/`libEGL_nvidia` (this machine's
proprietary GPU driver, see `CLAUDE.md`, GTX 1050 Max-Q) under
instrumentation generates **93,442 allocs / 49,463 frees** just in its
load-time constructors, dominating the available time. The 6 "Invalid
write/read" that did appear all point to
`Address 0x... is on thread 1's stack`, originating in
`__wine_syscall_dispatcher`, Wine's Unix↔PE stack-switching trampoline
in its syscall dispatcher, a **well-documented** false positive of
`valgrind` against Wine (Wine manually switches the stack pointer when
crossing that boundary; `valgrind` does not understand it and flags it
as an invalid access). There is no Wine suppressions file installed on
this system (`find / -iname "*wine*.supp"` gives nothing; this Arch's
`wine-staging` package does not include one) to filter out this noise.

**Attempt 2, forcing the EGL vendor to mesa to avoid loading the nvidia
blob:** `__EGL_VENDOR_LIBRARY_FILENAMES=.../50_mesa.json
LIBGL_ALWAYS_SOFTWARE=1`, `--leak-check=no` (less overhead), 1100s
timeout in background. Result: **much faster**, in ~2 real minutes it
reached real window/menu-creation code (`NtUserCreateWindowEx`,
`calc_menu_bar_size`, `DrawTextW`, far past where attempt 1 got to), but
died with a **different failure, unrelated to the bug being chased**:
`nodrv_CreateWindow`, *"Application tried to create a window, but no
driver could be loaded"* / *"The explorer process failed to start"*,
followed by Opus's own error handling, `Win32 error 1400` in
`init2.c:324`. Forcing the EGL vendor to mesa broke `winex11.drv`'s
loading before Word could reach the document window, a new environment
problem, induced by the mitigation itself, unrelated to `sync_combo`.

**Attempt 3, only `LIBGL_ALWAYS_SOFTWARE=1`, without forcing the EGL
vendor (to isolate whether attempt 2's problem was the vendor override
or something else):** same result, **the same `nodrv_CreateWindow`**,
this time in under 90 real seconds (not a timeout; the process died on
its own, exit code 137 with no clear explanation, no evidence of OOM in
`dmesg`/kernel dmesg, plenty of available memory per `free -h`). The
failure repeating **without** the EGL vendor override rules that
override out as the cause; the most plausible suspect is that attempt
1, dying by `timeout` (`SIGTERM`) midway through
`wineboot`/`explorer` initialization, left the shared `WINEPREFIX` in a
half-written state (registry, locks, `explorer.exe` state) that
attempts 2 and 3 inherited, **not verified**, only the explanation most
consistent with the available evidence.

**Conclusion of this session: `valgrind` is not viable here without
additional infrastructure work, not attempted.** Three runs, zero
reached the real crash point (`sync_combo`/`CB_ADDSTRING`). The only
"Invalid write/read" observed are the known
`__wine_syscall_dispatcher` false positive. For this avenue to be
viable it would need, at minimum: (1) a dedicated, disposable
`WINEPREFIX` for `valgrind` runs (so a run killed midway does not
poison the next one); (2) Wine's official suppressions file
(`tools/valgrind/wine.supp` in Wine's source tree, not packaged in this
Arch install, would need to be extracted from wine-staging 11.15's
source or built by hand) to eliminate the
`__wine_syscall_dispatcher` noise; (3) understanding why avoiding the
nvidia blob breaks the window-driver's loading, not investigated
whether it is `LIBGL_ALWAYS_SOFTWARE` itself, the contaminated
`WINEPREFIX`, or some other interaction.

**Recommendation for the next session, given the cost already
incurred:** do not keep insisting with `valgrind` as a first step. The
manual instrumentation technique from §15/§16 (`fprintf`/`backtrace()`
+ `addr2line`, no `gdb`, no `valgrind`) already localized the bug with
reasonable precision (`sync_combo`, `CB_ADDSTRING` loop, index band
14-15 of 1395) in native runs under a second. More return on the same
effort likely comes from (a) auditing by hand the
`sync_combo`/`combo_item` code and what Wine does internally in its
*builtin* `COMBOBOX`/listbox implementation around
`CB_ADDSTRING`/`CB_RESETCONTENT` in that operation range, or (b) if
`valgrind` is revisited, doing so only after resolving (1) and (2)
above, not as a quick exploration.

**Not pursued this session:** creating a dedicated `WINEPREFIX` for
`valgrind` (a larger infrastructure change, not attempted without
agreeing on it first); extracting/generating Wine's suppressions file;
diagnosing attempt 3's exit code 137; trying `AddressSanitizer` as a
lighter-weight alternative to `valgrind` (mentioned as an idea, not
evaluated, uncertain whether winegcc/Wine tolerate ASan's shadow-memory
model well).

### 18. `valgrind`, complete infrastructure built, definitive conclusion: incompatible with this `winex11.drv` in this environment, not a tuning problem

Explicit instruction: "apply whatever is necessary to make valgrind
available and continue". The missing infrastructure was built and 2
more attempts (4 and 5) were made with it, plus a native control run
that isolates the real cause.

**Infrastructure built:**
- **Disposable `WINEPREFIX`, pre-warmed outside `valgrind`:** `wineboot
  --init` run natively (without `valgrind`) in a fresh prefix, then
  snapshotted via `tar` into a restorable "pristine" `.tar.gz`,
  restorable within seconds. This removes from the `valgrind` run the
  entire slow `wineboot`/registry/`explorer` startup phase that
  dominated attempt 1 in §17.
- **Wine's real suppressions file:** not packaged on this Arch
  (confirmed, §17); obtained from
  [`austin987/wine-valgrind-scripts`](https://github.com/austin987/wine-valgrind-scripts)
  (a Wine maintainer, a collection of suppressions based on Dan Kegel's
  original scripts), `valgrind-suppressions-external` (nvidia/mesa
  driver noise, glibc, etc.) + `valgrind-suppressions-ignore`
  (Wine's intentional behavior), combined into a single file.
- **`--vex-iropt-register-updates=allregs-at-mem-access`:** the flag
  that collection's own `vg-wrapper.sh` uses to run Wine under
  `valgrind`, dampens false positives from VEX's (`valgrind`'s
  instrumentation engine) register-update model against Wine's manual
  context switches.
- **An additional suppression written by hand**, based directly on the
  evidence from the 3 §17 runs (not from the external file, which
  predates this function name): 5 `Memcheck:Addr{1,2,4,8,16}` entries
  with `fun:__wine_syscall_dispatcher`, the exact trampoline that
  produced 100% of the false positives observed so far.

**Attempt 4** (a freshly pre-warmed prefix, `LIBGL_ALWAYS_SOFTWARE=1`,
all the infrastructure above): **the same `nodrv_CreateWindow` as in
attempts 2/3 from §17**, this time with a prefix genuinely never
touched by `valgrind` before, **refutes the §17 hypothesis** that a
`WINEPREFIX` contaminated by attempt 1 (killed midway through startup)
was the cause.

**Attempt 5** (the same pristine prefix restored again, **with no GL/EGL
override at all** this time, to isolate whether the GL/EGL
vendor override itself was the real cause, Xvfb restarted with
`+iglx` in case Xvfb's default rejection of indirect GLX contexts
mattered): **the same `nodrv_CreateWindow` again**, in under 3 real
minutes. This rules out both the EGL vendor override and
`LIBGL_ALWAYS_SOFTWARE` as the cause, neither was present this time.

**Decisive control run:** the same pristine prefix restored once more,
same Xvfb (`+iglx`), but **without `valgrind`**, `wine
WORD1.exe.so` directly. Result: **it reaches the real, known crash
cleanly** (`elf_search_auxv can't find symbol in module`, Wine's crash
handler trying to symbolize after `sync_combo`'s `SIGABRT`, the same
signature as §15/§16). That is: **with the exact same prefix and the
same Xvfb, removing `valgrind` is the only thing needed for window
creation to work.**

**Definitive conclusion of this series of attempts (5 total between
§17 and this section): it is not a configuration, suppressions, prefix,
or GL environment-variable problem, `valgrind` itself breaks
`winex11.drv`'s loading in this environment's combination of
wine-staging 11.15 / Xvfb / `valgrind` 3.25.1.** The exact root cause
was not investigated beyond isolating that `valgrind` is the factor
(not tested, for example, whether it is specifically X11's `MIT-SHM`
extension, a known and independent friction point between `valgrind`
and X shared memory, and a reasonable hypothesis since `winex11.drv`
typically uses `XShm` on initialization, nor whether a real X server
instead of `Xvfb` changes the result).

**This closes the `valgrind` avenue for this investigation, with solid
evidence rather than a suspicion.** The §17 recommendation stands, now
with more weight: continue with §15/§16's native manual instrumentation
(already localized the bug in `sync_combo`/`CB_ADDSTRING`, index band
14-15 of 1395, in sub-second native runs, with no tooling friction). If
`valgrind` is ever revisited, the starting point is no longer "fix it"
but a more specific, bounded question: why exactly does it break
`winex11.drv`, `XShm`, Wine's threading model, or something else,
before investing more time.

**Not pursued this session:** isolating `MIT-SHM` as the cause (trying
`Xvfb ... -extension MIT-SHM` or equivalent); testing against a real X
server; AddressSanitizer as an alternative (still not evaluated,
mentioned in §17).

### 19. Auditing `sync_combo` against Wine's real *builtin* `COMBOBOX`/`LISTBOX`, clean; the most active candidate remains the port's own code

With `valgrind` closed (§18), resumes that section's recommendation:
audit `sync_combo`/`combo_item` line by line against Wine's real
implementation, instead of continuing to fight with dynamic tools.
Source obtained from `wine-mirror/wine` (a GitHub mirror of the
official tree, `gitlab.winehq.org` sits behind an anti-bot that blocks
automated fetches), `dlls/user32/combo.c` and `dlls/user32/listbox.c`,
`master` branch. It is not the exact wine-staging 11.15 version line by
line, but this part of the code (the classic *builtin* control, no
design changes in years) is stable across versions for the purposes of
this audit, the exact tag was not sought.

**Finding 1, the `CB_GETLBTEXTLEN`/`CB_GETLBTEXT` (ANSI) pair is
internally consistent; there is no size mismatch.** `combo.c` delegates
both directly to `LB_GETTEXTLEN`/`LB_GETTEXT` on the internal
`hWndLBox` (the combo is a thin wrapper over a hidden `LISTBOX`).
In `listbox.c`, `LISTBOX_GetText` (`listbox.c:863`):
- Length-only mode (`buffer == NULL`, for `CB_GETLBTEXTLEN`):
  `WideCharToMultiByte(CP_ACP, 0, str, len, NULL, 0, ...)` with `len =
  lstrlenW(str)`, **excludes** the null terminator.
- Real-write mode (`CB_GETLBTEXT`): `WideCharToMultiByte(CP_ACP, 0,
  str, -1, buffer, 0x7FFFFFFF, ...)`, converts the **whole string,
  including the null** (`-1` = null-terminated string) into a
  destination size Wine treats as "trust the caller to have sized it
  correctly" (there is **no** bounds check against a real buffer size,
  that is the real Win32 contract for `LB_GETTEXT`, not an oversight
  of Wine). The null terminator occupies exactly 1 byte in any code
  page, the arithmetic `bytes_written = bytes_reported_by_LEN + 1` is
  an invariant, not a coincidence.

`combo_item` (`opus_win95_chrome.cpp`) sizes its `std::vector<char>` to
`length + 1` using exactly `CB_GETLBTEXTLEN`'s value, matching exactly
what `CB_GETLBTEXT` is going to write. **No overflow here.**

**Finding 2, `CB_ADDSTRING` copies the string into its own storage; no
ownership transfer to our buffer.** `LISTBOX_InsertString`
(`listbox.c:1692`): `HeapAlloc(GetProcessHeap(), 0, (lstrlenW(str)+1) *
sizeof(WCHAR))` followed by `lstrcpyW(new_str, str)`, Wine **copies**
`str`'s content (our `item.c_str()`, a temporary `std::wstring` from
`sync_combo`) into its own allocation (`new_str`), and only `new_str`
is stored in `descr->u.items[index].str`. It retains no pointer at all
into our `std::wstring`; our `item` can be destroyed (and is, at the
end of each `for` iteration) without leaving a dangling pointer on
Wine's side. **No use-after-free through this path.**

**Finding 3, ruled out with concrete evidence: growth of the internal
item array (`resize_storage`) is not in play.** `listbox.c:151`: the
`descr->u.items` array grows in blocks of `LB_ARRAY_GRANULARITY = 16`
via plain `realloc()` (consistent with it being glibc's `realloc`/`free`,
not Wine's `HeapAlloc`/`HeapFree`, see Finding 4). `LISTBOX_InsertItem`
(`listbox.c:1626`) calls `resize_storage(descr, nb_items + 1)` on
*every* insertion, but that function only reallocates if `items_size`
needs to grow, the first insertion (`nb_items` 0→1) already reserves
**16 slots** at once (`(1 + 15) & ~15 = 16`), so items 0 through 15
(sixteen insertions) go through **without any additional `realloc()`**.
The second `realloc()` would only happen when inserting item 17
(index 16), and **none of this series' runs ever reached that point**:
the crash always falls at index 14 or 15 (§16), that is, with
`nb_items` between 15 and 16, **before** the array's second
`realloc()` gets a chance to run. This positively rules out the mirror
combo's item-array growth as the crash mechanism, not because it was
not looked at, but because the evidence (the maximum observed index)
excludes that code running again before the process dies.

**Finding 4, architectural note, not resolved: two allocator families
coexisting on the hot path.** The item array (`resize_storage`) uses
plain glibc `realloc()`/`free()`, matching directly the observed crash
messages' signature (`free(): invalid pointer`, exactly glibc
vocabulary). Each item's individual strings (`new_str` from
`LISTBOX_InsertString`, freed in `LISTBOX_DeleteItem` via
`HeapFree(GetProcessHeap(),...)`) use Wine's own heap
(`ntdll`/`RtlAllocateHeap`), architecturally a *different* allocator
from glibc's `malloc`, mixing pointers between the two families
(`free()` on something from `HeapAlloc`, or `HeapFree()` on something
from `malloc`) would produce exactly this class of corruption. **No
such mix was found** in the code reached by `sync_combo`/`combo_item`'s
real usage (Findings 1-2) or in the port's own code (`wide_from_ansi`,
`ansi_from_wide`, `combo_item`, reviewed, each sizes its buffer with
the same length-query call before writing, no mismatch). Noted as a
relevant architectural fact for any future hypothesis, not as a
positive bug finding.

**Additional, cheap, no-tooling-friction check:** `MALLOC_CHECK_=3`
(enables glibc consistency checking on every `malloc`/`free`/
`realloc`, without touching `ptrace` or Wine's syscall dispatcher,
therefore without the §18 block) over 3 native runs: **same message,
same crash point, no change.** Not conclusive on its own (a chunk
corrupted well before and not touched again until this point would give
the same result with or without `MALLOC_CHECK_`, because the check only
runs when that specific chunk is touched), but it is consistent with,
not contradicting, the real write and its detection being close in
execution time, rather than far apart.

**Audit conclusion: Wine's *builtin* code reached by `sync_combo` is
clean** for the most likely bug patterns (ANSI/Unicode length
mismatch, use-after-free from improper ownership transfer, item-array
growth reentrancy). No line of Wine was found to explain the
corruption. This **redirects the focus** toward: (a) the port's own
code in the stretch not yet audited at this level of detail,
`locate_source_combos`'s 1395×2 calls to
`combo_contains`/`CB_FINDSTRINGEXACT` on the 1395-font combo, flagged
as pending in §16 and not yet reviewed line by line against Wine's real
`LISTBOX_FindString`/`LISTBOX_FindStringPos`; or (b) that the cause is
genuinely outside this code path entirely (some heap mutation during
`FCreateMw`'s construction, already flagged as suspicious in §15).

**Not pursued this session:** auditing `LISTBOX_FindString`/
`CB_FINDSTRINGEXACT` against `locate_source_combos` at the same level
of detail as here; experimentally confirming whether Wine's
`HeapAlloc`/`HeapFree` in this specific build (winelib, syscall-based
`ntdll`) ultimately delegate to glibc's `malloc`/`free` or manage a
completely separate arena (would determine whether Finding 4 is a real
bug avenue or an architectural dead end).

### 20. Auditing `locate_source_combos` against Wine's real `CB_FINDSTRINGEXACT`, also clean

Closes the explicit pending item §19 left open: auditing
`combo_contains` (used by `locate_source_combos` up to 2 times per
candidate, up to 1395 items per combo) against the real
`CB_FINDSTRINGEXACT` implementation. Same pair of sources as §19
(`combo.c`/`listbox.c` from `wine-mirror/wine`, `master` branch).

**Unlike `CB_GETLBTEXT` (§19), `CB_FINDSTRINGEXACT` is a read-only
query, there is no buffer-size contract to break.** `combo_contains`
sends the search string (our `const char* value`, read-only) and
receives back an index or `CB_ERR`; it never receives data written
into a buffer of ours. That already structurally reduces the bug
surface compared to §19's case.

**Real path:** `combo.c` forwards `CB_FINDSTRINGEXACT` to
`LB_FINDSTRINGEXACT` on the internal `hWndLBox` (the same pattern as
everything else). In `listbox.c:2913`, the `LB_FINDSTRINGEXACT` handler
does the ANSI→Unicode conversion of the search string **entirely
within itself**, with size correctly computed at each step, without
the unbounded-destination-size trick that does appear in
`CB_GETLBTEXT`:

```c
LPSTR textA = (LPSTR)lParam;
INT countW = MultiByteToWideChar(CP_ACP, 0, textA, -1, NULL, 0);
if((textW = HeapAlloc(GetProcessHeap(), 0, countW * sizeof(WCHAR))))
    MultiByteToWideChar(CP_ACP, 0, textA, -1, textW, countW);
ret = LISTBOX_FindString( descr, wParam, textW, TRUE );
if(!unicode && HAS_STRINGS(descr))
    HeapFree(GetProcessHeap(), 0, textW);
```

Size query (`NULL, 0`) → `HeapAlloc` of the exact returned size →
real conversion into that same size → `HeapFree` at the end of the same
block, no state persists between calls. `LISTBOX_FindString`
(`listbox.c:1022`, `exact` branch) does a linear
`LISTBOX_lstrcmpiW`/`CompareStringW` sweep item by item, without
mutating the combo, without touching the item array, without growing
it. The up to 1395×2 calls per candidate in `locate_source_combos` are,
each, a self-contained round trip that leaves no state or live
allocation beyond its own return.

**Conclusion: also clean.** With `sync_combo` (§19) and now
`locate_source_combos`/`combo_contains` audited line by line against
Wine's real code and no explainable bug pattern found in either, the
entire toolbar-mirroring path (everything `sync_mirrors` executes) is
ruled out as a local origin for the bug. The remaining candidate, not
yet audited at this level of detail, is the one §15 flagged from the
start: what `FCreateMw` (`open.c`, between lines 566 and 591) builds or
leaves half-built before
`EndStartup1`→`DisplayRibbonInit`→`OpusSyncWin95Toolbar` triggers this
chain, or, alternatively, that the real corruption is already written
before reaching here at all and this code is only the first place that
"discovers" it by touching the heap.

**Not pursued this session:** auditing the `FCreateMw` stretch in
`open.c:566-591` line by line (it is in `src/Opus/`, restricted,
requires explicit authorization before touching it, and this audit was
read-only against external Wine source, not against `src/Opus/`).

### 21. Verification in the local Debian 13 container, the crash does not reproduce; confirms §8 (VPS) was not an isolated case

Explicit instruction: use the local `debian13` container (hp-15, see
`CLAUDE.md`) to investigate the "known gap" that same documentation had
left open, `WORD1.exe.so` launched there was left with a single thread
blocked reading its own internal pipe (`fd 9`) for 45+ seconds, without
reaching either the Arch crash or the rest state documented in §8 for
the VPS, and close it with `gdb`, the next step that had been noted but
not executed.

**Build:** `bin/WORD1.exe.so` in the container was already on the same
`HEAD` as the host (`536072a`), rebuilt inside the container at 14:41
in this same session, Qt 6.8.2 confirmed via `ldd` (matches what
`CLAUDE.md`'s "gotcha" requires about not mixing host/container
binaries).

**Launch:** `DISPLAY=:59 wine WORD1.exe.so` under an already-active
`Xvfb` in the container, backgrounded with `nohup ... &` (not `setsid`:
`setsid` inside a `machinectl shell` session did not survive the
session closing in the first two attempts, empty process and log,
exact cause not investigated; plain `nohup` did survive, verified with
`kill -0` before and after closing the session). Expected, benign
warnings, already documented in `CLAUDE.md` (missing `wine32`/multiarch,
`getaddrinfo` unable to resolve hostname).

**Process state, confirmed via `/proc` without needing `gdb` yet:** a
single thread (`Threads: 1`), `State: S (sleeping)`, `wchan:
anon_pipe_read`. `ls -la /proc/<pid>/fd` confirms exactly what
`CLAUDE.md` described: `fd 9` is the read end of `pipe:[297758]`,
`fd 10` is the write end of the same pipe, both opened by the process
itself.

**`sudo gdb -p <pid> --batch -ex "bt full"` inside the container**
(`ptrace_scope=1` blocks attaching as a normal user, just as §15
documented for hp-15/Arch, `sudo` was needed):

```
__internal_syscall_cancel (fd=9) → __syscall_cancel → __GI___libc_read
→ ntdll.so (no symbols, 3 frames) → NtWaitForMultipleObjects
→ win32u.so (no symbols) → NtUserGetMessage
→ __wine_syscall_dispatcher
```

**Reading:** this is Wine's real, documented mechanism for
`GetMessage()`, the client thread waits on a Win32 kernel object
blocked in `read()` on a pipe the process itself owns at both ends (the
write end is passed to `wineserver` over the protocol socket for it to
signal; the client keeps its own copy). It is not a hang: it is the
normal rest state of a `GetMessage()` waiting for input that, under
`Xvfb` with no interaction, never comes, the same class of state §8
documented for the VPS, only there it had not been confirmed with a
tool that looked inside the process. The "known gap" in `CLAUDE.md`
(the container behaving differently from the VPS) is closed: there is
no third behavior signature, there are two Debian 13 environments
reaching the same healthy state, one of them just not instrumented
until now.

**Visual confirmation, beyond the backtrace:** `DISPLAY=:59 xwininfo -root
-tree` lists 22 real `word1.exe` windows, including
`0x800001 "Microsoft Word - Document1": 760x542+0+26`, correct title,
reasonable document size, visible. `x11-apps`/`imagemagick` were
installed on the container (`apt-get install`, not present) to capture
with `xwd -root` + `convert` to PNG and read the image directly: the
window shows the full menu bar (File Edit View Insert Format Utilities
Window), the toolbar with style/font/size controls
(`Normal`/`Arial`/`10`) and the formatting buttons, the ruler, and the
blinking cursor at the start of the document, the same
`OpusWin95Toolbar` whose `sync_mirrors`/`sync_combo` reproducibly
crashes on hp-15/Arch (§12-20) ran here end to end without corrupting
anything. A solid black box appears in the middle of the document page
in the capture; it was not investigated whether it is an artifact of
the `xwd`→PNG conversion under this color depth or something real in
the render, the rest of the window (chrome, menus, bar text) looks
correct, so it was not read as a sign of a problem. Screenshots deleted
from the project tree after reading them (not part of the repo).

**Consequence, changes the blocker's status for the supported
platform:** sections 1-20 of this document are a real, valid diagnosis
of a real bug, but that bug has only reproduced on hp-15 (Arch,
wine-staging 11.15/16, no longer a target of this project). On the two
available Debian 13 surfaces (VPS, local container), with the same code
(`536072a`, no `src/Opus/`/`src/port/` diffs between sessions since
§12), `WORD1` starts up, builds the full document window (including the
`FCreateMw`→`EndStartup1`→`DisplayRibbonInit`→
`OpusSyncWin95Toolbar`→`sync_mirrors`→`sync_combo`→`locate_source_combos`
stretch, exactly the chain this document spent nine sections
instrumenting) and reaches a healthy rest state. It was not
investigated *why* Arch/wine-staging does corrupt the heap and Debian
13/vanilla wine does not, the open hypotheses in §17-20 (two allocator
families, different *builtin* `ComboBox`/`ListBox` code between Wine
versions) remain unconfirmed, but they stop being blocking: the project
no longer validates on Arch. README.md updated to reflect this
(Winelib is the active focus again; Qt core paused).

**Not pursued this session:** sending real input (keyboard/mouse via
synthetic `xdotool` or similar, not installed) to confirm interaction
beyond window construction; identifying the cause of the black box in
the capture; investigating why `setsid` did not survive the
`machinectl shell` session closing in the first two launch attempts.

### 22. `word1_startup_blocked` actually run on Debian 13 (container), 0/9 pass, but for new reasons distinct from heap corruption

Resumes §21: with startup confirmed healthy on Debian 13, it is worth
checking whether the 9 tests under the `word1_startup_blocked` label
(registered as non-gating under the premise "`WORD1` always crashes")
would actually pass now. Rebuilt everything from scratch inside the
container (`rm -rf out/linux-winelib-debug` + reconfigure + build of
`opus_original_engine`, `WORD1`, `opus_word1_ui_test`) to eliminate any
leftover host cache from the day before.

**Tooling note:** launching long-running background processes inside a
`machinectl shell` session (`nohup ... &`, with or without `setsid`)
did not consistently survive that session closing in this work, a
337-TU build was interrupted with `"ninja: build
stopped: interrupted by user."` despite `nohup`. `systemd-run
--machine=debian13 --uid=pablo ...` (launches a transient unit inside
the container's own systemd, independent of any PTY/session) proved
reliable in 5/5 uses. Use that mechanism, not `machinectl shell
... &`, for future background work in this container.

**Result: `ctest -L word1_startup_blocked --timeout 90`, 0/9 pass, but
no test shows the heap-corruption signature from sections 1-20.**
Three distinct patterns:

- **`word1_port_smoke_test`**: Times out at 90.10s (ctest's own
  timeout). `WORD1.exe.so --self-test` runs, does not crash, reaches
  the same `NtUserGetMessage`/`anon_pipe_read` rest state from §21, and
  stays there, the test has no success condition that is met by
  "started and stayed healthy", it just runs out the clock.
- **`opus_word1_ui_test`** (no flag, the base driver): `ctest` reports
  "Timeout 20.01 sec" (matches almost exactly the `TIMEOUT 20` from
  `set_tests_properties`, not the 8000ms of the test's own internal
  `wait_for_window()`, see
  `port/original/opus_word1_ui_test.cpp:633-637`), with
  `"WORD1 main window did not appear"` in the output. The title it
  looks for (`"Microsoft Word - Document1"`) is exactly the same one
  §21 confirmed visible via `xwininfo`, the window does exist. The
  discrepancy between the 8s internal timeout and `ctest`'s reported
  ~20s is not explained; not instrumented further this session.
- **The remaining 7 tests** (`--clipboard`, `--typing`, `--interaction`,
  `--selection`, `--font-typing`, `--about`, `--save-as`): fail in
  0.04-0.09s with `"unknown test mode"`, `wmain`'s argument parsing in
  `opus_word1_ui_test.cpp:576-621` rejects exactly the flags
  `src/CMakeLists.txt:1580-1607` passes to it (`argument_count == 3`
  with `arguments[2]` compared via `wcscmp` against each flag).
  Verified by reading the generated `CTestTestfile.cmake` that the
  correct flag really is passed (e.g. `"--clipboard"`). Exact cause not
  isolated, `argc`/`argv` were not instrumented inside `wmain` to see
  what actually arrives from the Wine/`winegcc` side when running the
  `.exe` via `ctest`. A test-harness CLI parsing bug, unrelated to the
  heap corruption or to Wine/`GetMessage`.

**Consequence:** the `word1_startup_blocked` label is still 0/9 today,
but for completely different and new reasons, none of them the heap
corruption from sections 1-20. The smoke test and the base ui_test need
a success condition that recognizes "healthy rest state" as a valid
result, not just waiting for the process to finish on its own. The 7
flagged tests need the CLI parsing bug fixed before even getting to
launch `WORD1`. No test code was touched this session
(`src/port/original/opus_word1_ui_test.cpp` and `src/CMakeLists.txt`
are not in the restricted tree, so a fix there would not need
authorization if resumed).

**Not pursued this session:** instrumenting real `argc`/`argv` inside
`opus_word1_ui_test.cpp`'s `wmain` to see the exact cause of "unknown
test mode"; measuring the real time between `CreateProcessW` and the
"Microsoft Word - Document1" window becoming visible, to know whether
8s is insufficient or `wait_for_window` has another problem;
redesigning `word1_port_smoke_test`/`opus_word1_ui_test`'s success
condition so a healthy rest state counts as a pass.

### 23. Root cause of "unknown test mode", glibc's 4 bytes against the real 2-byte `WCHAR`; fixed and verified; uncovers a broader problem in the same file

Resumes pending item #1 from §22. Temporary instrumentation in
`opus_word1_ui_test.cpp` (reverted on close, final diff clean except
for the real fix): `fprintf`/a manual loop printing each `wchar_t` of
`arguments[2]` as `%04x`, without going through `wcslen`/`%ls`.

**Finding, confirmed with direct evidence:** `arguments[2]` for
`--clipboard` contains 11 correct UTF-16 units (`002d 002d 0063
006c 0069 0070 0062 006f 0061 0072 0064` = `"--clipboard"`), the
pointer-to-pointer advances correctly in 2-byte steps, confirming
`sizeof(wchar_t)` is 2 in this TU (`-fshort-wchar` from `winegcc`, like
the rest of the project). But `std::wcslen(arguments[2])` on the same
line, same pointer, returns **6**, not 11. The only consistent
explanation: `wcslen`/`wcscmp`/`wcsstr` from `<cwchar>` resolve to the
**glibc** symbol, compiled with its own native, **4-byte**, `wchar_t`,
that function ignores the caller's local type size (it is already
compiled code, not a template) and reads memory 4 bytes at a time. Over
a real 24-byte buffer (11 units × 2 plus a null × 2), reading in
4-byte steps lands exactly one `uint32` beyond the buffer (offset 24),
and whatever is there (adjacent memory, not necessarily zero) decides
where the string "ends" according to `wcslen`, in this run it
happened to be `len=6`. Same mechanism for `%ls` in
`fprintf`/`std::wcerr` (see §16, where it had already been documented
as a *dump* problem; this confirms it is a **logic** problem, not just a
printing one).

**Fix applied and verified, root cause, not symptom:** replaced the 10
`std::wcscmp(arguments[2], L"--flag")` comparisons with
`lstrcmpW(arguments[2], L"--flag")` (Win32/`kernel32`, already available
without changing `target_link_libraries`, which only had `user32
gdi32`). **7/7 runs with each flag (`--clipboard`, `--typing`,
`--interaction`, `--selection`, `--font-typing`, `--about`,
`--save-as`), none prints `"unknown test mode"` again.** Closes the
exact cause §22 left open.

**The same pattern was found and fixed in two more places in this same
file, not explicitly requested but necessary to keep the investigation
moving:**
- `find_window_callback` (used by `wait_for_window`, which looks for
  the `"Microsoft Word - Document1"` window): `std::wcscmp`/`std::wcsstr`
  on real `class_name`/`caption` (from `GetClassNameW`/
  `GetWindowTextW`) replaced by `lstrcmpW` and a local
  `wide_contains()` (a manual loop, not depending on any `<cwchar>`
  function).
- The `_wcsicmp` macro (defined as `#define _wcsicmp wcscasecmp`, used
  in `find_descendant_by_class`/`collect_descendants_by_class`/
  `control_has_class`): changed to `#define _wcsicmp lstrcmpiW`.

**A third site with the same pattern is identified but not fixed, and
is, with evidence, the real cause of the bug behind "unknown test
mode":** `std::wstring command_line = L"\"" +
std::wstring(arguments[1]) + L"\"";` (building the command line for
`CreateProcessW`, right before that call). With the `argv` fix already
applied, `--clipboard` stops printing "unknown test mode" but crashes
with `malloc(): invalid size (unsorted)` +
`virtual_setup_exception stack overflow`, **before even reaching the
first instrumentation line placed right before
`CreateProcessW`** (confirmed by placing a marker there and in
`find_descendant_by_class`: neither printed in 5/5 runs of the crash).
The only thing between the end of argument parsing and that marker is
building `command_line`, a `std::wstring` built from `arguments[1]`
(a real `WCHAR*`). The `std::wstring(const wchar_t*)` constructor
internally calls `char_traits<wchar_t>::length()` to size the buffer,
if that resolution falls into the same native 4-byte implementation as
`wcslen`, the resulting `std::wstring` ends up with a corrupt
size/capacity from construction, consistent with the heap corruption
observed later (not necessarily on the same line that writes it, it is
the typical signature of late-detected corruption, same as in §16).
**Not confirmed with the same direct evidence as the three sites above
(the byte-by-byte dump was not applied to this specific
`std::wstring`), but it is the reading most consistent with the
available evidence.**

**Scope: the problem is file-wide, not one line.** A `grep` found
`std::wstring`/`std::wcerr` at 6 more sites in this file
(`send_physical_text`, lines ~1430/1602/1849 with literals/variables
`std::wstring`, `log_window_callback` with `std::wcerr <<`), all
potentially with the same problem, not audited one by one this
session. The general rule for any code in this file (and, possibly,
any `.cpp` under `src/port/` compiled with `wineg++`/`-fshort-wchar`
that includes `<cwchar>`/`<string>` instead of using only raw
`wchar_t*` pointers or the Win32 functions
`lstrcmpW`/`lstrcmpiW`/`lstrlenW`/`lstrcpyW`): **any function from
`<cwchar>`/`<string>`/`<iostream>` that touches a real `wchar_t*`'s
content (not just its address) is suspect by default**, regardless of
the `wchar_t` type looking "correct" (2 bytes) in that same TU's source
code.

**Not pursued this session:** confirming with a direct dump that
`std::wstring(arguments[1])` is the exact corruption site (rather than
just inferring it by elimination); auditing and fixing the 6 remaining
`std::wstring`/`std::wcerr` sites in this file; revisiting whether, with
this fully resolved, `opus_word1_ui_test` in `--clipboard` mode (and
the other 6) really pass against `WORD1` on Debian 13.

**Build restored to the three real fixes** (`_wcsicmp` macro,
`wide_contains`+`find_window_callback`, `lstrcmpW` in `argv` parsing),
all temporary instrumentation (`[DIAG]`/`[DRIVER]` markers,
`find_descendant_by_class`'s `depth` parameter) reverted, diff
confirmed clean except for those three changes.

### 24. `std::wstring(arguments[1])` confirmed as the cause of the `malloc()`, fixed and verified; uncovers a fourth bug, of a different family (`CreateProcessW` returns `PROCESS_INFORMATION` zeroed out)

Resumes the pending item from §23: confirm with a direct dump that
`std::wstring(arguments[1])` is the real corruption site, not just
infer it by elimination.

**Confirmed with direct evidence:** temporary instrumentation isolating
the construction (`const std::wstring diag_probe(arguments[1]);` right
before the real `command_line`, reverted on close) shows
`diag_probe.size()=22` against `lstrlenW(arguments[1])=32`, the real
value is 32 (`"/home/pablo/msword/bin/WORD1.exe"`). The indexed content
(`diag_probe[i]` for `i` in `[0,22)`) is the correct, real prefix of the
string (`"/home/pablo/msword/bin"`, not garbage), confirming that
`operator[]` (direct access, not going through `char_traits`) works
fine in this TU, but the `std::wstring` **constructor** miscalculated
the length at construction time (the same underlying mechanism as
§23: `char_traits<wchar_t>::length()`'s resolution falls into glibc,
4 bytes, not into this TU's real 2-byte `wchar_t`).

**Fix applied and verified, root cause:** replaced
`std::wstring command_line = L"\"" + std::wstring(arguments[1]) +
L"\";` with a `wchar_t command_line[MAX_PATH + 4]` buffer built by hand
with `lstrcpyW`/`lstrcatW` (Win32/`kernel32`, already in the link).
**3/3 runs of `--clipboard`, none shows `malloc(): invalid
size (unsorted)` or Wine's `stack overflow` again**, they reach
`wait_for_window` cleanly and fail there with a normal message
(`"WORD1 main window did not appear"`, exit code 3), not a crash.
Confirms this `std::wstring` was indeed the real site of the heap
corruption appearing after the §23 fix, not a mere co-occurrence.

**A fourth bug, of a completely different family, uncovered by fixing
this one:** with the crash resolved, `--clipboard` (and by extension
the other 6 flagged modes) still do not pass, consistently, even
raising `wait_for_window`'s timeout from 8000ms to 30000ms (rules out a
timing problem; §21 already showed the window appears fine within that
time window). Confirmed with `xwininfo -root -tree` **during** the
run: the `"Microsoft Word - Document1"` window exists, on the same
display, with the exact title being searched for, just as in §21. The
problem is not that the window does not exist: it is that
`find_process_window`/`find_window_callback` never find it, because
they filter by `process_id`, and that `process_id` arrives as
**zero**. Traced to the source: `CreateProcessW` **returns success**
(no `"CreateProcessW failed"` is printed) but leaves
`PROCESS_INFORMATION` completely zeroed,
`dwProcessId=0 dwThreadId=0 hProcess=0`, confirmed with a direct print
right after the call (temporary instrumentation, reverted). It is not
the same mechanism as §16/§23/§24: `PROCESS_INFORMATION` has no
`wchar_t` member at all, it is just `HANDLE`/`DWORD`, `wchar_t`'s width
cannot explain this. Unexplored candidates: some particularity of
`CreateProcessW` under Wine specifically when the calling process is a
**console**-subsystem executable (`-mconsole -municode`, as documented
for this target in `src/CMakeLists.txt`) creating a **GUI**-subsystem
child (`WORD1.exe`, `-mwindows`); or a real limitation of this Wine
version (`wine` 10.0 vanilla in the container) for nested process
creation that does not manifest when launching `wine WORD1.exe.so`
directly from a Linux shell (as in §21/§22, which do work).

**Instrumentation reverted, build restored to only the two real fixes
from this section plus the three from §23** (`command_line` with a
manual buffer; `_wcsicmp`→`lstrcmpiW`; `wide_contains`+
`find_window_callback`; `lstrcmpW` in `argv` parsing), diff confirmed
clean.

**Not pursued this session:** isolating whether the creator's
console-vs-GUI subsystem is the relevant variable (trying, for example,
compiling a minimal version of `opus_word1_ui_test` without
`-mconsole` to see whether `dwProcessId` stops being zero, a non-trivial
build change, not attempted); comparing against `CreateProcessW`'s
behavior on wine-staging (host, Arch) to see whether it is specific to
vanilla `wine` 10.0; reviewing Wine's source code
(`dlls/kernelbase/process.c` or equivalent) for `lpProcessInformation`
handling on the path this case actually takes.

### 25. `dwProcessId=0` isolated to the binary, not the harness, reproduced with a minimal program; conclusion: Wine behavior, not a bug in this project

Resumes §24 with the question left open: is it `opus_word1_ui_test`
(compiled `wmain`/`-mconsole -municode`) that causes the zeroed
`PROCESS_INFORMATION`, or is it specific to launching `WORD1.exe`? Two
quick variants were tried first, neither changed the symptom:

- **Unconditional `GetLastError()`:** revealed `14007`
  (`ERROR_SXS_KEY_NOT_FOUND`, an activation-context/"side by side"
  manifest context) on the first try, seemed like a real lead, but...
- **`lpApplicationName = nullptr`** (passing everything through
  `lpCommandLine`, the most common `CreateProcessW` usage pattern): the
  `14007` disappears (`GetLastError()=0`, clean success), but
  `dwProcessId`/`hProcess` **remain zero**. The `14007` was noise, a
  side effect of passing `lpApplicationName` explicitly, not the real
  cause. Reverted (no reason to keep it, it fixes nothing and changes
  the executable-search semantics).

**Decisive isolation, a minimal program, entirely outside this file:**
two ~20-line `.c` files, compiled directly with `winegcc` (no
`-mconsole`/`-municode`, a normal `main()`, nothing from this project
except the target binary), to separate "harness" from "target":

```c
STARTUPINFOW si = {0}; si.cb = sizeof(si);
PROCESS_INFORMATION pi = {0};
BOOL ok = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
```

- **Target `notepad.exe`** (Wine builtin, `C:\windows\system32\notepad.exe`):
  `ok=1 err=0 pid=292 tid=296 hProcess=0x40 hThread=0x44`, **works
  perfectly**, real PID and handles.
- **Same program, target `WORD1.exe`** (this project's `.exe`/`.exe.so`,
  without touching the harness at all): `ok=1 err=0 pid=0 tid=4263507920
  hProcess=(nil) hThread=(nil)`, **same symptom as in
  `opus_word1_ui_test`**, with a program that does not share a single
  line of code with it.

**Conclusion:** the bug is not in `opus_word1_ui_test.cpp` or in how it
is compiled (`wmain`, `-mconsole -municode` are ruled out as the
variable, the minimal program does not use them and fails the same
way). It is specific to **creating a process for this project's
`.exe`/`.exe.so` from inside an already-running Wine process**, against
`notepad.exe` (a Wine builtin) which works fine in the same call, same
environment, same moment. The most plausible difference between the
two targets: `notepad.exe` is a Wine *builtin* executable (with its own
internal load path, likely without going through fork+exec of an
external `.so`), while `WORD1.exe.so` is a native ELF built externally
by this project, the `CreateProcessW` path for that second case
(spawning a third-party `.exe.so` as a subprocess) appears not to relay
`dwProcessId`/`hProcess` back to the caller correctly in
`wine` 10.0~repack-6 of this container, even though the real child does
get created and does get to show its window (confirmed in §24 with
`xwininfo` during the run).

**This is no longer a straightforwardly fixable application bug**,
unlike §23/§24 (glibc's 4 bytes vs a 2-byte `WCHAR`, a one-line fix
each), here no flag or `CreateProcessW` usage pattern was found and
tried that avoids the problem; it looks like a real limitation/behavior
of this Wine version for this specific kind of nested process creation.
**Pragmatic path, not attempted this session:** stop depending on
`dwProcessId`/`hProcess` returned by `CreateProcessW` to locate
`WORD1`'s window, `find_window_callback` already filters by title
(`"Microsoft Word -
Document1"`, reasonably specific) via `wide_contains`; removing or
relaxing the `process_id` filter would make the match title-only,
which we already know finds the right window (§21, §24). A real, not
cosmetic, trade-off: without the PID filter, a leftover window from a
previous run (crashed or not cleaned up) with the same title could give
a false positive, flagged for an explicit decision, not applied
unilaterally.

**Cleanup:** the minimal program's two `.c`/`.exe`/`.exe.so` files
(`minimal_cp_test*`) were written temporarily at the repo root to
compile them with `winegcc` from there (they need to be inside the tree
for the `WINEPREFIX`'s relative paths to resolve the same way as the
rest of this investigation) and deleted on close, never `git add`ed.
`opus_word1_ui_test.cpp`'s instrumentation (canary values,
`GetLastError()`/struct-size prints, the `lpApplicationName=nullptr`
experiment) reverted; build restored to the two real fixes from
§23-24, diff confirmed clean.

**Not pursued this session:** implementing the title-only matching
workaround (a design decision, not a technical one, pending
agreement); confirming whether the same minimal program fails the same
way on the VPS or on the host's wine-staging (would isolate whether it
is specific to vanilla `wine` 10.0~repack-6 or more general); reviewing
Wine's `dlls/kernelbase/process.c` (or the GitHub mirror, same pattern
as §19/§20) for the exact path taken when creating a process for an
external ELF instead of a builtin module.

### 26. Title-matching workaround applied, unblocks 8/9 tests up to real interaction logic; the 9th (typing) reveals another `std::wstring` site

Applies the workaround §25 left noted but unimplemented: in
`find_window_callback`, the `process_id` filter is now skipped when
`search.process_id == 0` (the known, broken case from §24-25), it stays
unchanged, more precise, when the PID does arrive valid. Four lines,
with a comment explaining why and pointing to §24-25.

**Verified with the full suite (`ctest -L word1_startup_blocked`,
`opus_word1_ui_test` rebuilt from the fix, same environment as §22).**
Still 0/9, but the contrast with §22 is total: before, 7/9 failed
instantly with `"unknown test mode"` and 2/9 with
`"WORD1 main window did not appear"` (not even attempting
anything). Now:

| Test | Before (§22) | Now (§26) |
|---|---|---|
| `word1_port_smoke_test` | Timeout 90s (unchanged, unrelated, still no success condition for a healthy rest state) | Timeout 90s |
| `opus_word1_ui_test` (base) | Timeout 20s, window not found | Timeout 20s, `"File New dialog did not appear"` |
| `--clipboard` | `"unknown test mode"` (0.09s) | Timeout 20s, `"Ctrl+A did not execute Select All"` |
| `--typing` | `"unknown test mode"` (0.04s) | Timeout 20s, **`malloc(): invalid size (unsorted)` + stack overflow** |
| `--interaction` | `"unknown test mode"` (0.04s) | Timeout 25s, `"could not prepare the native window move test"` |
| `--selection` | `"unknown test mode"` (0.04s) | Timeout 20s, `"typing did not leave a canonical insertion selection"` |
| `--font-typing` | `"unknown test mode"` (0.04s) | Timeout 20s, `"font typing test could not find the ribbon controls"` |
| `--about` | `"unknown test mode"` (0.04s) | Timeout 20s, `"Help About dialog did not appear"` (+ extra diagnostics: `mainWindow=1 responsive=1 stage=0`) |
| `--save-as` | `"unknown test mode"` (0.04s) | Timeout 20s, `"File Save As dialog did not appear"` (+ `Save As stage=0`) |

**8 of 9 now reach the test's real interaction logic**, specific,
meaningful failure messages, each from its own actual check (not
generic, not parsing-related). This is, for the first time in this
series of sessions, the test harness working as designed: every
remaining failure is a real question about `WORD1`'s behavior on
Debian 13 (does Ctrl+A not select all?, does the Save As dialog not
appear?, etc.), not a harness problem in itself. None of the 9 passes
yet, but the kind of work remaining changed entirely, from "fix the
test harness" to "audit `WORD1`'s real behavior".

**The `--typing` case (test 13) is the exception, it still crashes,
the same signature as §24** (`malloc(): invalid size (unsorted)` +
`virtual_setup_exception stack overflow`, practically identical
address/stack range). It is not the same bug as §24 (that one is
already fixed and verified there), it is, almost certainly, one of the
"6+ more `std::wstring`/`std::wcerr` sites" that §23-24 left flagged as
suspicious without auditing, specifically the candidate at line ~1430
(`const std::wstring sentence = L"physical keyboard
input line one";`, inside code that only runs in `--typing` mode),
matching that it is exactly *this* test, and no other, that triggers
it: it is the only code path that ever constructs that particular
`std::wstring`.

**Practical consequence for the next session, in order of leverage:**
1. Audit the `std::wstring` site in the `--typing` path
   (line ~1430 and `send_physical_text`) with the same fix pattern as
   §24 (a manual buffer + `lstrcpyW`/`lstrcatW`, or building the
   `std::wstring` letter by letter with `+=` on short literals if the
   real usage does not need concatenation, not confirmed which applies
   without reading the code in more detail).
2. The remaining 6 `std::wstring`/`std::wcerr` sites (not necessarily
   in the path of any test yet, but any new mode that exercises them
   will reproduce the same crash).
3. The 7 "real" failure messages in the table above, each is now an
   investigation into `WORD1`'s own behavior, not the harness; start
   with `--about` and `--save-as`, which already come with extra
   diagnostics (`stage=`, `responsive=`) useful for narrowing without
   instrumenting again.

**Not pursued this session:** auditing or fixing the `--typing`
`std::wstring` site; investigating any of the 7 real behavior failures;
confirming whether the design trade-off flagged in §25 (a false
positive from a leftover window from a previous run) ever manifested at
any point in this run, not observed, but not actively looked for
either.

### 27. The `--typing` `std::wstring`, it was not the line ~1445 suspected in §26, it was the accumulator loop from §1864; fixed and verified

Resumes pending item #1 from §26. The candidate §26 flagged (`const
std::wstring sentence = L"physical keyboard input line one";`, line
~1445) turned out to be a simple literal assignment, not on the
`typing_mode` path at all (it belongs to another mode). The real site,
after reading the full body of `if (typing_mode)`:

```cpp
std::wstring text;
for (int line = 0; line != 40; ++line) {
    text += L"original line ";
    if (line < 10) { text += L'0'; }
    text += std::to_wstring(line);
    text += L'\r';
}
```

A `std::wstring` growing via `operator+=` over 160 steps (40
iterations × 4 mutations each), each possibly triggering an internal
reallocation whose size is computed with the same
`char_traits<wchar_t>` glibc machinery (4 bytes) that §23-24 already
found broken against this TU's real 2-byte `wchar_t`,
`std::to_wstring` also goes through that machinery. With 160
opportunities for corruption instead of a single construction, it fits
that this would be, of the unaudited sites, the one most likely to
manifest first.

**Fix applied, same pattern as §24, no `std::wstring` at all:**
replaced with a fixed `wchar_t text[40 * 20 + 1]`, filled with
`wsprintfW(line_text, L"original line %02d\r", line)` (Win32/`user32`,
already in the link) for each line and copied by hand into the main
buffer with a cursor pointer. `wsprintfW`'s `%02d` reproduces exactly
the manual padding the original code did (a leading zero only if
`line < 10`). The following loop, which already walked `text`
tolerating `L'\0'` (`if (character != L'\0')`), needed no changes, a
fixed `wchar_t[]` with a zeroed tail fits directly with that existing
guard.

**Verified 4/4 runs of `--typing` (one, two concurrent, one again):
none crashes.** Before: `malloc(): invalid
size (unsorted)` + `virtual_setup_exception stack overflow`
consistently. Now: exit code 13
(`"active document pane has no focus"`) or 15 (`"could not post a
character to the document"`) depending on the run, both real, specific
application failures, of the same class as the other 7 from §26, not
crashes. The variation between 13 and 15 was not investigated (it could
well be a genuine race condition in the test itself against the
window's focus, unrelated to this fix).

**With this, all 9 `word1_startup_blocked` tests consistently reach
real interaction logic**, none crashes anymore from the wide-char
pattern of §23-24/§27. 6 more `std::wstring`/`std::wcerr` sites remain
unaudited in the file (lines ~1617/1627, `log_window_callback`, and
`send_physical_text`'s uses with short literals via implicit
conversion), none confirmed as problematic, they might never get
exercised in practice (short literals are less likely to need
reallocation) or they might fail the same way if some new mode
exercises them under more load.

**Build restored, clean diff**, no temporary instrumentation this
time, the fix was written and verified directly since the site was
already identified with reasonable certainty by reading the code.

**Not pursued this session:** auditing the remaining 6
`std::wstring`/`std::wcerr` sites; investigating the 13 vs. 15
variation between `--typing` runs; resuming any of the 7 real behavior
failures §26 left as a pending work list.

### 28. A fourth independent environment, a virgin machine, confirms a clean build and real startup outside hp-15 and the project's container (2026-08-14)

Verification explicitly requested to test the port on a Debian 13
distinct from the two already used in this series: neither hp-15
(EndeavourOS/Arch, hp-15 session) nor "the project's Debian 13
container" (the one that established the README's finding, that the
startup crash does not reproduce outside Arch). This session runs on a
third machine, a Debian 13 (trixie) VM / kernel 6.12.101 /
GCC 14.2.0, **with no build tooling preinstalled**, not even `git` or
`gcc` were present at the start. This is, so far, the closest to
"clean room" of all the runs recorded in this series.

**Clean clone, carrying nothing over from hp-15 or any prior
checkout:** `gh repo clone jphonorato/msword` (origin
`jphonorato/msword`, upstream `jmarshall23/msword` added automatically
by `gh`).

**Toolchain installed from scratch via `apt`:** `git`,
`build-essential`, `cmake` (3.31.6), `ninja-build`, `wine`/`wine64-tools`
(the `wine-devel` package the README names **does not exist on Debian**,
the correct equivalent is `wine64-tools`, which does provide
`winegcc`, `wineg++`, `wrc`, `winebuild`; worth having the README
reflect this), `qt6-base-dev`. `wineboot --init` with `WINEARCH=win64`
creates the prefix with a non-fatal error (`failed to open
L"C:\windows\syswow64\rundll32.exe"`) due to the absence of
`wine32`/i386 multiarch support on this machine, `system.reg` and
`drive_c` are generated anyway and nothing that follows is blocked.

**Contradicts the "CI blocker" documented in the README (`src/port/tools/host/`
missing on a clean clone):** `cmake --preset linux-winelib-debug`
configured with no errors on the first try, no manual intervention.
`src/port/tools/host/`'s `CMakeLists.txt` is indeed tracked in git,
`.gitignore` only excludes the generated `build/`
(`src/port/tools/host/build/`), not the whole directory as the README
implies. It was not investigated why the blocker does reproduce in CI
(could be a problem specific to the GitHub Actions environment, not to
the repo's source), it stands as an unresolved discrepancy between what
is documented and what was observed here.

**Build, both targets, 0 errors:**

```
cmake --build --preset linux-winelib-debug --target opus_original_engine
# 337/337, only expected K&R C warnings (untyped PASCAL/NATIVE,
# redefined macros, incompatible pointers)

cmake --build --preset linux-winelib-debug --target WORD1
# 127/127, 0 errors
```

Artifacts: `bin/WORD1.exe` (697 B, Winelib wrapper) +
`bin/WORD1.exe.so` (14.6 MB, ELF 64-bit LSB, 7040 exported symbols
via `nm -D`).

**Real startup, verified with `xwininfo` and a screenshot
(`xwd` + ImageMagick), not just by absence of a crash:** `./WORD1.exe`
opens "Microsoft Word - Document1" (1280×739) with full chrome, the
menu bar (File/Edit/View/Insert/Format/Utilities/Window/Help),
toolbar with icons, style ("Normal") and font
("Arial", 10 pt) selectors, ruler, blinking text cursor, status
bar. No heap corruption, no timeout, no intervention, matches the
README's finding that the startup blocker does not reproduce on
Debian 13, now confirmed on a third, distinct Debian 13
machine.

**With this, the finding "does not reproduce on Debian 13" stops
depending on a single machine/container**, it holds on an environment
provisioned from scratch, with no prior project state.

**Not pursued this session:** why the `src/port/tools/host/` CI
blocker does reproduce on GitHub Actions if `CMakeLists.txt` is
tracked; installing `wine32`/multiarch to resolve the `wineboot`
warning; running `word1_startup_blocked` or any of the 9 interaction
tests on this machine; updating the `wine-devel` → `wine64-tools`
package name in the README's Requirements.

### 29. "word1_port_smoke_test" was not hanging for lack of a success condition, it was hanging because std::wcsstr never detected --self-test

Same bug class as §23-24/27, unaudited in
`src/port/original/opus_original_startup_probe.cpp`. The `--self-test`
fast path in `wWinMain` (around lines 431-432) used
`std::wcsstr(command_line, L"--self-test")` and
`std::wcsstr(GetCommandLineW(), L"--self-test")`. Under winegcc,
`wchar_t` is a 2-byte WCHAR (`-fshort-wchar`), but glibc's `std::wcsstr`
operates on a native 4-byte `wchar_t`: the search does not find the
flag even though it is in the real command line, `wWinMain` falls
through to the full GUI startup (`GetMessage`), and the test sits for
~90s until ctest's default timeout (the test does **not** have a
`TIMEOUT` property).

**Direct repro (debian13 container, DISPLAY=:59, build in
`/home/pablo/build-debian13-verify`):**

```
# Before the fix
timeout 15 /usr/lib/wine/wine64 ./WORD1.exe.so --self-test
# EXIT=124 ELAPSED=15  (timeout kills the process)
# /tmp/word1_self_test.txt absent -- the self-test branch never ran
# (a DwmSetWindowAttribute stub appears -> real GUI startup)

# After the fix (CommandLineHasFlag helper, manual 2-byte loop)
timeout 15 /usr/lib/wine/wine64 ./WORD1.exe.so --self-test
# EXIT=0 ELAPSED=0
# /tmp/word1_self_test.txt:
#   module=0x7f9c8ee80000 CmdHelp=0x7f9c8ef4007a CmdAbout=0x7f9c8ef4050a
```

**Fix:** a local `CommandLineHasFlag()` (same pattern as
`wide_contains()` in `opus_word1_ui_test.cpp` §23) replaces the two
`std::wcsstr` calls. This file is Linux/Winelib-only in practice for
the probe; `src/Opus/` was not touched.

**ctest (Step 5):**

```
ctest --test-dir /home/pablo/build-debian13-verify -R word1_port_smoke_test --output-on-failure
# 1/1 Test #10: word1_port_smoke_test ............   Passed    0.10 sec
# CTEST_EXIT=0
```

Closes gap B of the winelib-startup-blocked plan: the smoke test stops
being a silent ~90s timeout and becomes a sub-second check.

### 30. Closing the std::wstring/std::wcerr audit in opus_word1_ui_test.cpp, 4 more sites (9 via send_physical_text) not crashing in 4/4 runs

Closes gap A of the winelib-startup-blocked plan (the
`std::wstring`/`std::wcerr` sites §23-24/§27 left unaudited in
`src/port/original/opus_word1_ui_test.cpp`). Same bug class as
§23-24/§27/§29: a 2-byte `wchar_t` under winegcc (`-fshort-wchar`)
against glibc/`std::` machinery built for a 4-byte `wchar_t`.

**Prior grep (Step 1), live hits vs. comments:**

| Line | Hit | Status |
|---|---|---|
| 129 | `std::wcerr << L"window class='" ...` in `log_window_callback` | live |
| 514 | `bool send_physical_text(const std::wstring& text)` | live (9 call sites with `L"..."` literals) |
| 671/675 | comments about the §24 fix | comment |
| 1445 | `const std::wstring sentence = ...` in `selection_mode` | live |
| 1617 | `const std::wstring physical_text = ...` in `interaction_mode` | live |
| 1865/1870 | comments about the §27 fix | comment |

**Sites fixed:**

1. **`send_physical_text`**, signature changed from `const std::wstring&`
   to `const wchar_t*`; `for (character : text)` loop replaced by an
   index bounded by `lstrlenW`. The 9 call sites with `L"..."` literals
   link without constructing a temporary `std::wstring`.
2. **`sentence` in `selection_mode`**, changed to `const wchar_t* const` +
   `lstrlenW` + an index-based loop (including the later use of
   `sentence.size()`, now `sentence_length`).
3. **`physical_text` in `interaction_mode`**, changed to
   `const wchar_t* const`; the pass to `send_physical_text` and the
   computation of `expected_cp_after_typing` (previously
   `physical_text.size()` + a range-for counting `L'\r'`) switch to
   `lstrlenW` + an index-based loop.
4. **`log_window_callback`**, `std::wcerr` with `WCHAR` buffers changed
   to `WideCharToMultiByte(CP_ACP, ...)` into `char[]` buffers and
   `std::cerr` (the same narrow-diagnostic pattern as the rest of the
   file).

Post-fix grep: only mentions left are in deliberate comments (§24/§27
and the callback's note).

**Note on the brief:** the plan said `physical_text` was only passed to
`send_physical_text`. In the real code it was also used with `.size()`
and a range-for to count `L'\r'` when computing the expected CP; those
uses were migrated to the same pointer + `lstrlenW` pattern (without
reintroducing `std::wstring`).

**ctest (Step 6, debian13 container, DISPLAY=:59, build at
`/home/pablo/build-debian13-verify`):**

```
1/9 Test #10: word1_port_smoke_test ................   Passed    0.10 sec
2/9 Test #11: opus_word1_ui_test ...................***Timeout  20.02 sec
File New dialog did not appear
3/9 Test #12: opus_word1_clipboard_shortcut_test ...***Timeout  20.02 sec
Ctrl+A did not execute Select All
4/9 Test #13: opus_word1_typing_test ...............***Timeout  20.02 sec
could not post a character to the document
5/9 Test #14: opus_word1_interaction_test ..........***Timeout  25.02 sec
could not prepare the native window move test
6/9 Test #15: opus_word1_selection_test ............***Timeout  20.02 sec
typing did not leave a canonical insertion selection
7/9 Test #16: opus_word1_font_typing_test ..........***Timeout  20.00 sec
font typing test could not find the ribbon controls
8/9 Test #17: opus_word1_about_test ................***Timeout  20.01 sec
Help About dialog did not appear
9/9 Test #18: opus_word1_save_as_test ..............***Timeout  20.00 sec
File Save As dialog did not appear
# 11% tests passed, 8 tests failed out of 9
# CTEST_EXIT=8
# Label Time Summary: word1_startup_blocked = 165.21 sec*proc (9 tests)
```

Matches the table from §26/§27 (real behavior messages; typing is
still in the 13/15 family from §27: this run gives `"could not
post a character to the document"`). **No crash** of the
`malloc(): invalid size` / stack overflow class. The smoke test passes
(Task 1 / §29). This task does **not** fix any of the 8 behavior
failures, it only removes the residual wide-char risk in the harness so
Tasks 3-10 can trust the failure messages.

### 31. `opus_x64_runtime_test` (gating), it was not an infrastructure hang, it was an id collision with `kIddOpen` (2026-08-19)

**Investigated at explicit request, on exia**, after having ended up
documented in 9 different places in this project (§27 further below in
this same file, and 5 sections of
`03-word1-startup-blocked-behavior.md`) as "gating, hangs without
printing anything, pre-existing, unrelated to anything, not
investigated". Every mention cited the same evidence: `rc=124` under
`timeout 40`, zero output. Nobody had investigated it beyond rebuilding
the binary.

**First finding, and this session's recurring pattern:** the binary at
`build/tests/Debug/opus_x64_runtime_test.exe.so` was dated August 13,
rebuilding it with
`cmake --build --target opus_x64_runtime_test` and running it
**without** `DISPLAY` gives `exit=20` in 2.7s, not a hang
(`return 20;` in `opus_x64_runtime_test.cpp:282`, `CreateWindowExA`
returns `nullptr` because of `nodrv_CreateWindow`, no display driver,
correct, expected behavior without `DISPLAY`).

**The real hang only shows up with a real `DISPLAY`** (its own,
isolated `Xvfb :77` with `openbox`, not the shared `:99`). With that,
`CreateWindowExA` does succeed and execution moves past
`return 20`, up to a real block (`rc=124`). **This is exactly the
environment CI uses** (`.github/workflows/build.yml:47-53` starts
`Xvfb :99` before every `ctest`), the hang reported in 9 places in this
project was real there, not a development-machine artifact.

**`gdb -p <pid> --batch -ex "thread apply all bt"`** (same pattern as
§21/§25 of this document) confirms the single thread parked in
`NtUserGetMessage`, the same normal `GetMessage()` rest state
`CLAUDE.md` and this document already document extensively for WORD1
itself. It is not a crash or corruption: something is waiting for a
message that never arrives.

**Root cause, in the code, not just the symptom:** two synthetic
dialog templates in `opus_x64_runtime_test.cpp` reuse `hid` values
that collide with real constants from
`opus_sdm_runtime.cpp`:

```c
constexpr Word kIddNewDoc = 2;
constexpr Word kIddOpen = 3;
constexpr Word kIddSaveAs = 4;
```

The first template (`modal_template`, tested by
`ModalRuntimeProbe`) used `hid=3`, exactly `kIddOpen`.
`TmcDoDlgDli` (`opus_sdm_runtime.cpp:2565`) has a special case that is
evaluated **before** its own native message loop:

```cpp
if (dialog->modal &&
    (dialog->hid == kIddOpen || dialog->hid == kIddSaveAs)) {
    const Tmc common_result = run_word95_common_file_dialog(*dialog);
    ...
```

With `hid==kIddOpen`, any call to `TmcDoDlgDli`, regardless of the
caller only wanting *any* real hid to activate `native_modal=true` via
`create_dialog_host`/`materialize_open_template`, not specifically the
open-file dialog, gets redirected straight to
`run_word95_common_file_dialog`, which blocks the thread inside
`GetOpenFileNameA`, the **real** Win32/Wine file dialog. Nothing in
this headless harness moves that dialog (no Cancel click, no Escape),
it sits waiting for real input forever. This special case (§26 of
`03-word1-startup-blocked-behavior.md` documents when
`run_word95_common_file_dialog` was integrated) is **newer** than this
test: `hid=3` worked when the test was written, before
`kIddOpen`/`kIddSaveAs` had this shortcut, a real, silent regression
from a task that never touched this file nor knew this test existed.

The second template (`new_modal_template`, tested by
`NewModalRuntimeProbe`) uses `hid=2` (`kIddNewDoc`), **not** a
collision, it is intentional: the probe itself verifies
(`new_modal_controls_present`) that `materialize_new_template`'s real
controls (`0x0402`-`0x0405`) exist under that hid, so it needs the real
New Doc template applied. `kIddNewDoc` does not have the
`run_word95_common_file_dialog` shortcut (only
`kIddOpen`/`kIddSaveAs` do), so it never had this problem.

**Fix:** `modal_template` changes its `hid` from `3` (`kIddOpen`) to
`44` (`kIddAbout`), any of the other 5 real hids without the file
shortcut would have worked, since `ModalRuntimeProbe` does not check
for any specific dialog control (unlike `new_modal_template`); About
was chosen as the most verified path of this whole session (Task 3 of
the winelib plan). `new_modal_template` stays unchanged.

**Verified:**
```
DISPLAY=:77 wine opus_x64_runtime_test.exe.so
    exit=0   (before: rc=124, a 15s+ hang confirmed, zero output)

DISPLAY=:99 ctest --output-on-failure   (the whole CTestTestfile, not just the label)
    18/18... no, 16/18 -- the 2 failures are the already-documented and
    expected ones from --interaction and --font-typing (§12/§8 of
    03-comportamiento). All 9 gating tests, including
    opus_x64_runtime_test, pass for the first time in this project.
```

File: `src/port/original/opus_x64_runtime_test.cpp` only, no change in
`opus_sdm_runtime.cpp` or in `Opus/`. The `kIddOpen`/`kIddSaveAs`
special case in `TmcDoDlgDli` stays as is: it is correct production
code (the port DOES need Open/Save As to use the real Win32 dialog),
the bug was entirely in the test reusing an id that a later, unrelated
change made significant.

**Lesson to re-read before assuming "unrelated, pre-existing" in this
project:** twice in this same session (here, and earlier with
`opus_word1_ui_test.exe.so` in `docs/port-linux/03-word1-startup-
blocked-behavior.md` §5) a "confirmed pre-existing hang" turned out to
be a binary from days earlier never rebuilt with
`cmake --build --target <that specific target>`, `cmake --build
--target WORD1` (or any other target) does not rebuild sibling test
targets.

Closes gap A of the winelib-startup-blocked plan.
