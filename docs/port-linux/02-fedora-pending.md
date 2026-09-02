# Outstanding items requiring Fedora: heap corruption diagnosis at WORD1 startup

**UPDATED 2026-08-14, the title's premise is partially superseded.** The
hp-15 session (`01-...md`, "hp-15 Session" section) reproduced the crash on
**EndeavourOS/Arch** (GCC 16.2.1, wine-staging 11.15), not only on Fedora,
confirming that the relevant variable is GCC ≥15, not the specific distro.
Items 1 and 2 of this document, which were listed as blocked pending
Fedora, have already been run there and are marked **done** below, with
the real result instead of the pending instruction. Fedora itself remains
untested, the document is not invalidated, it just stops being the only
path. The remaining items (3-7) stay open as-is, regardless of which
environment they run in.

Continuation checklist for `01-heap-corruption-startup-diagnosis.md`.
Everything below remained blocked, unresolved, or could not be generalized
from the sandbox used in the most recent sessions (Debian 13 "trixie",
GCC 14.2.0, wine-10.0 Debian repack, `gdb` 16.3), which **does not
reproduce the crash** (§11.2, §13). The crash does reproduce, consistently,
on:

- **Fedora 44**
- **GCC 16.1.1**
- **wine-staging 11.0**
- `gdb` 17.2
- `valgrind` 3.27.1
- **EndeavourOS (Arch), GCC 16.2.1, wine-staging 11.15** (hp-15, see above)

It is not yet known which of these variables (Wine version, GCC version, or
`wine.conf`/DPI/theme config) makes the difference against Debian, not
investigated (§11.2). Matching the full environment is the only confirmed
way to reproduce, not just the Wine version. What can be stated now
(hp-15): **Fedora specifically is not required**, GCC ≥15 is enough.

Parenthetical references (`§N`) are sections of
`01-heap-corruption-startup-diagnosis.md`.

---

## 0. Prerequisites: already resolved on `main`, do not repeat the work

The three build blockers that appeared when rebuilding in an environment
other than Fedora (§11.1) are already committed on `main`, no need to
rediscover them:

- `wrc: codepage 1252 not supported` → fixed with `--nls-dir` (`a0191b0`).
  **Verify on Fedora**: the hardcoded path is
  `/usr/share/wine/nls`, confirm it exists there in Fedora's
  `wine-staging` package before assuming it applies unchanged.
- `GetCurrentThreadStackLimits was not declared` → local declaration
  kept (`a0191b0`). In Wine 11.0/Fedora the function **is** declared
  natively (unlike Wine 10.0/Debian, which was the reason for the fix);
  confirm the local declaration does not clash (redefinition) with the
  real one from that `windows.h`.
- `opus_word1_ui_test.cpp:379` `LONG`/`20L` cast, plus `std::min`,
  undeclared `_wcsicmp`, and `wmain` without `extern "C"` → fixed and
  **linking** (`6ff2b53`). This unblocks triggering `C_FormatLineDxa`
  via `--typing`/`--font-typing` instead of launching `WORD1` empty (see
  §5, point below).

In addition, since the last Fedora session (§1-§9) **7 commits** of
`Global*` → `OpusMem*` migration were added (`bf7a5e1`..`aab06e5`, plus
`opus_shell_spine` already linked into `WORD1` since `7966f3b`), the
binary to diagnose on Fedora is no longer the same as in §1-§9. Rebuild
from `HEAD` of `main`, do not reuse an old binary.

```bash
cd src
cmake --preset linux-winelib-debug
cmake --build --preset linux-winelib-debug --target opus_original_engine
cmake --build --preset linux-winelib-debug --target WORD1
```

---

## 1. Confirm the crash still reproduces (baseline): DONE (hp-15)

Run on EndeavourOS/Arch from `HEAD` (`5fed452`), 4 runs with
`gdb -q --batch -ex run -ex "bt full" --args wine WORD1.exe.so`: reproduces
4/4, with **two new signatures** in addition to the already-known ones
(`free(): invalid pointer`, `double free or corruption (!prev)`), see
`01-...md`, "hp-15 Session" §1. Confirms that the `Global*`→`OpusMem*`
migration from the 7 prior commits did not change the underlying behavior:
it is still real, timing-dependent heap corruption. No need to repeat this
on Fedora except to compare specific signatures.

---

## 2. Milestone 2 of plan v2: `WINEDEBUG=+heap`: DONE (hp-15), negative but informative result

Run for the first time in any environment (hp-15, `01-...md` §2):
**888,073 lines**, real crash at line 24273. **Finding:** no `RtlFreeHeap`
is logged immediately before the crash, the `free()` that aborts is the
destructor of a C++ `std::wstring` (`operator delete`/glibc direct), it
does not go through Wine's own heap that `+heap` instruments.
**Consequence for the rest of the plan:** `+heap` has no further
diagnostic value for this bug family (any corruption originating in a C++
`std::wstring`/`std::vector` is invisible to this tool), do not retry
without a new hypothesis directly involving Win32 memory
(`GlobalAlloc`/`HeapAlloc`). The path that did give a concrete lead was
the manual stack scan (`01-...md` §3), not this.

---

## 3. Symbolize frame #0: DONE (hp-15 cont., `01-...md` §9), it was not a Wine DLL

This section's hypothesis (a Wine DLL, `user32`/`gdi32`/`ntdll`) was about
an address from a Fedora build that no longer exists, it was ruled out.
With `info proc mappings` + the base of `libc.so.6` + `addr2line` against
the real `debuginfo` (cached by `debuginfod`, not the system's
symbol-less `.so`), frame #0 resolves to
`__pthread_kill_implementation` (`nptl/pthread_kill.c:44`), the generic
`abort()`→`raise()`→`pthread_kill()` tail, identical in 3/3 runs with two
different crash signatures. This is expected information (any glibc heap
abort ends up there) and adds nothing further about the origin, see
`01-...md` §9 for the detail and why there is no need to retry this on
Fedora.

---

## 4. File:line breakpoints: use the §12.1 shortcut, not the old §11.4 plan

The 3-step plan §11.4 left recommended (breakpoint by absolute address, or
instrumenting `LOADFONT.C` with `fprintf` under authorization) **is not
needed**, it was **ruled out as unnecessary** in §12.1, although that was
ruled out on Debian (without the real crash). The mechanism (18 source
symlinks case-shimmed to `generated/lowercase-c/*.c`, DWARF records the
path in lowercase) is a property of the build, not the environment, so it
should apply the same way on Fedora:

```bash
gdb -q --batch -ex "set breakpoint pending on" \
    -ex "break loadfont.c:349" -ex "break loadfont.c:709" \
    -ex "run" -ex "bt 6" --args wine WORD1.exe.so
```

**In lowercase.** `break LOADFONT.C:349` (uppercase, as had been done on
Fedora in §11.3 before isolating the cause) never resolves, it stays
`<PENDING>` forever. Confirm this first on Fedora before assuming the
problem from §2/§11.3 (`info sharedlibrary` empty due to
`wine-preloader`) still applies the same way, Wine 11.0/Fedora may
behave differently from Wine 10.0/Debian here, not verified.

With this, close the `vsci.hdcScratch` hypothesis (§10) **with the real
crash reproducing**, not as in §13 (which closed it only for a run
without a crash on Debian, not generalizable). Repeat §13's approach
right there:

```bash
DISPLAY=:99 WINEDEBUG=+gdi wine WORD1.exe.so >gdi.trace 2>&1
```

and `grep` the `vsci.hdcScratch` handle value (obtained from `gdb` itself
at the breakpoint) looking for whether it appears alongside
`NtGdiDeleteObjectApp`/`free_gdi_handle` **before** the real crash occurs:
on Debian there was never a crash to cross-check this against, on
Fedora there is.

---

## 5. Trigger `C_FormatLineDxa` with `opus_word1_ui_test`: now confirmed necessary, not just "may give a better repro"

Now that `opus_word1_ui_test` compiles and links (`6ff2b53`, see §0), the
path §11.1 left blocked is available: use `--typing`/`--font-typing` to
force a real line-formatting path instead of relying on empty startup
exercising it on its own.

**Confirmed on hp-15 cont. (`01-...md` §10): with empty startup
`C_FormatLineDxa` is never called**, instrumented with an entry log,
zero invocations in 5/5 runs of the §1 crash. This document's crash
(startup, blank document) occurs entirely in window/toolbar construction,
not in line formatting. This path stops being "may give a better repro"
and becomes **the only way to exercise `C_FormatLineDxa` at all**,
necessary if this function is to keep being followed as a candidate (for
a bug distinct from §1's, with real typed text), not as a shortcut to
reproduce the startup crash.

---

## 6. Secondary item, not confirmed for Fedora: `opus_shell_memory_foreign_test`

Not directly related to the heap corruption, but another item that was
made conditional on an environment with multiarch support (P6 of the
memory checklist-audit,
`docs/superpowers/specs/2026-08-11-opus-memory-passthrough-checklist-audit.md`):
the test compiles and links, but could not be run in this session's
Debian sandbox because `wine32`/multiarch is missing
(`it looks like wine32 is missing... apt-get install wine32:i386`,
reproduced again when rebuilding `WORD1` in this session). If Fedora
already has 32-bit support installed (not confirmed, not assumed), run:

```bash
ctest --test-dir out/linux-winelib-debug -R opus_shell_memory_foreign_test
```

and confirm it green, this would be the first real run of that test
since it was implemented.

---

## 7. Explicitly ruled out: do not retry without new evidence

- **ASan.** Tested on Fedora (trivial, isolated Winelib binary, §8): fails
  in ASan's own initialization (`AddressSanitizer failed to deallocate`)
  due to a clash with `wine-preloader`'s address-space reservation, 5
  combinations of `ASAN_OPTIONS` tried, same result in all 5. Not an
  issue with this build, it is a Winelib x86-64/ASan incompatibility.
  There is no pending flag reconfiguration to try; a different approach
  (not identified) would be needed to reopen this path.
- **`valgrind --trace-children`.** Clashes with the same `wine-preloader`
  reservation (§4), but this was specific to Fedora's packaging, not
  universal (see next point).
- **`valgrind` without `wine-preloader`, hp-15 cont. session (`01-...md`
  §8).** Neither the VPS (Debian 13) nor Arch/hp-15 have
  `wine-preloader`, there `valgrind` does start. Tested on both: 240s
  clean on the VPS (does not reproduce), and **directly against the real
  crash on Arch**, the crash happens the same under valgrind (same
  glibc `free(): invalid pointer`), but valgrind's log records no error
  at all, with `malloc`/`free` interception confirmedly active (`-v`
  showed the `REDIR`s before `ntdll.so` loaded). **Ruled out for a reason
  broader than `wine-preloader`**, exact cause not investigated, not
  worth retrying without first understanding why memcheck does not see
  this corruption with interception active.
- **`glibc.malloc.check` (`LD_PRELOAD=libc_malloc_debug.so`).** Runs
  without clashing with anything (§12.4), but with limited diagnostic
  value: almost all of Word 1.1a's memory goes through Wine's own heap
  (`Rtl*Heap`/`ntdll`), not glibc's `malloc`, an out-of-range `write` in
  *that* heap will not be detected by this checker no matter how long it
  runs clean. Use `WINEDEBUG=+heap` (§2 of this document) instead, not
  this.
