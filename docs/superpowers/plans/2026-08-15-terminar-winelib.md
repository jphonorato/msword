# Terminar el port Winelib (word1_startup_blocked) Implementation Plan

> **Status 2026-08-15 (paused, usage cap):** Tasks 1–2 done
> (`09a4283`, `7de323f`). Task 3 mid-flight: font MessageBox fixed
> (`134cddc`); H1 rejected / `vhpllbs` already smashed at first
> `FInsertInPl` (`5bebdd9`). About still AVs. Resume from
> `docs/port-linux/03-comportamiento-word1-startup-blocked.md`
> “Cómo retomar”. Tasks 4–10 not started.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the gap between "Winelib build compiles" (done, Fases 0-5 of
`docs/port-linux/00-reconocimiento.md`) and "Winelib port works" — fix the
two remaining test-harness reliability bugs and root-cause the 7 real WORD1
behavioral bugs the `word1_startup_blocked` test suite already surfaced with
specific, actionable failure messages.

**Architecture:** Every task follows `superpowers:systematic-debugging`
(Phase 1: root cause before any fix) against real, reproducible failures —
no speculative fixes. Test-harness bugs (Tasks 1-2) reuse an established,
already-proven fix pattern (see Global Constraints). Behavioral bugs (Tasks
3-10) are investigated in dependency order: shared dialog infrastructure
first (one root cause may explain three symptoms), then independent,
narrower bugs. All work happens on the Debian 13 `debian13` nspawn
container (see `CLAUDE.md`) — Arch/host is not the target and currently has
its own unrelated, already-fixed build breakage (stale CMake cache, fixed
2026-08-15, not in scope here).

**Tech Stack:** Winelib (winegcc/wineg++), C (`Opus/`, restricted tree —
guarded edits only), C++20 (`src/port/original/*.cpp`), CTest, GNU/Linux
(Debian 13 trixie, GCC 14.2.0, Wine 10.0~repack-6).

**Spec:** `docs/port-linux/00-reconocimiento.md` (§8 "Plan por fases", Fases
0-6 and their success criteria) and
`docs/port-linux/01-diagnostico-heap-corruption-arranque.md` (§21-27, the
current, accurate state — §8's own "Gaps sin corregir" section at the file's
end is stale, superseded by work done since; do not use it as a source of
truth).

## Global Constraints

- **The established wide-char fix pattern (do not reinvent):** any TU under
  `src/port/original/*.cpp` compiled with `wineg++` has `wchar_t` as the
  real Win32 2-byte `WCHAR` (`-fshort-wchar`), but glibc's linked
  `wcslen`/`wcscmp`/`wcsstr`/`wcerr`/`std::wstring`'s `char_traits<wchar_t>`
  machinery operates on the *native* 4-byte `wchar_t` regardless of this
  TU's local type width — they silently misread real `WCHAR` data. Verified
  empirically 2026-08-14 (`wcslen()` on an 11-code-unit string returned 6).
  Fix: use `lstrcmpW`/`lstrcmpiW`/`lstrlenW`/`lstrcpyW`/`wsprintfW` (Win32
  native, ABI-correct) or a manual loop — never `std::wstring`,
  `<cwchar>`/`<string>` wide-char facilities, or `std::wcerr` on real
  `WCHAR` content. Reference implementation already in the tree:
  `wide_contains()` in `src/port/original/opus_word1_ui_test.cpp:58-71`.
  Full writeup: `docs/port-linux/01-diagnostico-heap-corruption-arranque.md`
  §23-24, §27.
- **`src/Opus/` and `src/OpusEtAl/` are restricted trees** — any edit needs
  explicit authorization (file an issue first per `CLAUDE.md`) and must be
  guarded:
  ```c
  #if defined(__GNUC__) && !defined(_MSC_VER)
  /* Linux-only change */
  #else
  /* original behavior, unchanged */
  #endif
  ```
  `src/port/original/*.cpp` is NOT restricted — it's the compatibility
  layer, edit freely there.
- **Build/verify only inside the `debian13` container**, using a separate
  `-B` dir to avoid clobbering the host's own build output (see `CLAUDE.md`
  "Local Debian 13 dev container" section for the exact commands). For
  anything that needs to stay alive across a `machinectl shell` session
  (running WORD1 to interact with it), use `systemd-run --machine=debian13
  --uid=pablo --gid=pablo ...`, not a backgrounded `machinectl shell`
  command — the latter does not reliably survive session close.
- **Every task ends with a real `ctest` run**, not a manual "looks fixed."
  Use `ctest --test-dir <build-dir> -R <test-name> --output-on-failure`.
- **Every fix gets logged in prose**, matching the existing docs' style
  (dated, numbered `###` sections, root cause stated before the fix, what
  was verified and how). Task 1-2 continue the existing numbered thread in
  `01-diagnostico-heap-corruption-arranque.md` (next available: §28). Tasks
  3-10 start a new file, `docs/port-linux/03-comportamiento-word1-startup-blocked.md`
  (that document's original topic — heap corruption at startup — is closed;
  don't keep growing an unrelated 3000+ line file with a new topic).
- **Commit message convention** (matches recent git log): `fix(port):
  <file> -- corrige <qué>: era <causa raíz en una frase>`. One commit per
  task, after its `ctest` run is green (or, if the task's honest outcome is
  "root cause found but fix deferred," say so in the commit body — do not
  claim a fix that isn't verified).

---

## File Structure

No new source files. Every task modifies one or more of:

- `src/port/original/opus_word1_ui_test.cpp` — the UI test harness (Tasks 1-B is elsewhere; wide-char audit is here)
- `src/port/original/opus_original_startup_probe.cpp` — WORD1's own entry point / `wWinMain`, `--self-test` handling (Task 1)
- `src/port/original/opus_sdm_runtime.cpp` — the C++ SDM/dialog/ribbon runtime that replaced the segmented Win16 dialog manager; prime suspect for Tasks 3-8 (dialog materialization, combo/list population, selection/focus routing)
- `src/Opus/*.c` — original Word source, touched only if a task's root cause traces there, and only guarded (Global Constraints)
- `docs/port-linux/01-diagnostico-heap-corruption-arranque.md` — append-only, Tasks 1-2 (§28, §29)
- `docs/port-linux/03-comportamiento-word1-startup-blocked.md` — new file, created by Task 3, appended by Tasks 4-10

---

### Task 1: Fix `--self-test` detection in `opus_original_startup_probe.cpp` (closes gap B — `word1_port_smoke_test`)

**Files:**
- Modify: `src/port/original/opus_original_startup_probe.cpp:431-432`
- Test: `word1_port_smoke_test` (registered `src/CMakeLists.txt:1574-1575`, **no `TIMEOUT` property set** — relies on ctest's default, which is why it silently runs long instead of failing fast)

**Interfaces:**
- Consumes: the established wide-char fix pattern (Global Constraints) and its reference implementation, `wide_contains()`.
- Produces: a verified-working `--self-test` fast path in WORD1's own entry point. Tasks 3-10 do not depend on this (they drive WORD1 through `opus_word1_ui_test.cpp`'s `CreateProcessW`, a separate path that never passes `--self-test`), but a green `word1_port_smoke_test` becomes a fast smoke check any later task can run before investing time in a deeper investigation.

- [x] **Step 1: Confirm the hypothesis by reading the current code**

```bash
sed -n '425,470p' /home/pablo/msword/src/port/original/opus_original_startup_probe.cpp
```

Confirm lines 431-432 still read:
```cpp
if ((command_line != nullptr &&
     std::wcsstr(command_line, L"--self-test") != nullptr) ||
    std::wcsstr(GetCommandLineW(), L"--self-test") != nullptr) {
```
This is the exact anti-pattern `opus_word1_ui_test.cpp:48-57` already
documents and fixed elsewhere in the tree — `std::wcsstr` misreads real
2-byte `WCHAR` data through glibc's 4-byte-`wchar_t` machinery, so it can
fail to find `--self-test` even when it's really there, meaning `wWinMain`
falls through past line 469 into full GUI startup (`GetMessage` loop) —
which never returns under a headless/no-input display, matching the
observed ~90s timeout exactly.

- [x] **Step 2: Reproduce the current failure directly (bypass ctest's timeout)**

Inside the `debian13` container (see `CLAUDE.md` for the exact
`systemd-run`/separate-`-B`-dir pattern):

```bash
sudo systemd-run --machine=debian13 --uid=pablo --gid=pablo \
  --working-directory=/home/pablo/msword/bin \
  --unit=selftest-repro \
  --setenv=DISPLAY=:59 \
  /bin/bash -c 'timeout 15 wine WORD1.exe.so --self-test; echo "EXIT=$?"'
```

Expected (bug present): no exit within 15s (`timeout` kills it), or an exit
code that isn't 0/2/3/4 (the four documented `--self-test` return values at
lines 460-468). Check `Z:\tmp\word1_self_test.txt` inside the wine prefix —
if the bug is present, that file was never written (the self-test branch
never ran).

- [x] **Step 3: Apply the fix — reuse the established manual-search pattern**

Add a local helper (same technique as `wide_contains`, but a plain
"contains" check is enough here — no need to import the whole function,
just its loop):

```cpp
namespace {
bool CommandLineHasFlag(const wchar_t* command_line, const wchar_t* flag) {
    if (command_line == nullptr || flag == nullptr || *flag == L'\0') {
        return false;
    }
    for (const wchar_t* start = command_line; *start != L'\0'; ++start) {
        const wchar_t* h = start;
        const wchar_t* n = flag;
        while (*h != L'\0' && *n != L'\0' && *h == *n) {
            ++h;
            ++n;
        }
        if (*n == L'\0') {
            return true;
        }
    }
    return false;
}
}  // namespace
```

Place it above `wWinMain` (near the top of the file, alongside the other
free functions). Replace lines 431-432:

```cpp
if ((command_line != nullptr &&
     CommandLineHasFlag(command_line, L"--self-test")) ||
    CommandLineHasFlag(GetCommandLineW(), L"--self-test")) {
```

This file is `src/port/original/`, not `src/Opus/` — no `#if
defined(__GNUC__)` guard needed, and no MSVC path to preserve (this probe
is Linux/Winelib-only; confirm with `grep -n "MSVC\|_MSC_VER"
opus_original_startup_probe.cpp` before editing, to be sure).

- [x] **Step 4: Rebuild and re-run the direct repro from Step 2**

```bash
cd /home/pablo/msword/src
cmake --build --preset linux-winelib-debug --target WORD1
```
(Run this *inside* the container against the container's own `-B` dir per
`CLAUDE.md` — do not let it clobber the host build.) Then repeat Step 2's
`systemd-run` command. Expected now: exits within ~1s, exit code 0, and
`Z:\tmp\word1_self_test.txt` contains a line like `module=0x... CmdHelp=0x...
CmdAbout=0x...` with non-null pointers.

- [x] **Step 5: Verify via ctest**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R word1_port_smoke_test --output-on-failure
```
Expected: `Passed`, well under a second — not a 90s timeout.

- [x] **Step 6: Document**

Append `### 28. "word1_port_smoke_test" no colgaba por falta de condición
de éxito — colgaba porque std::wcsstr nunca detectaba --self-test` to
`docs/port-linux/01-diagnostico-heap-corruption-arranque.md`, continuing
its numbering. State: the hypothesis (same bug class as §23-24/27, just
unaudited in this file), the Step 2 repro evidence (before/after), and the
Step 5 ctest result.

- [x] **Step 7: Commit**

```bash
cd /home/pablo/msword
git add src/port/original/opus_original_startup_probe.cpp \
        docs/port-linux/01-diagnostico-heap-corruption-arranque.md
git commit -m "fix(port): opus_original_startup_probe.cpp -- corrige --self-test nunca detectado: era std::wcsstr contra WCHAR real de 2 bytes"
```

---

### Task 2: Audit and fix remaining wide-char risk sites in `opus_word1_ui_test.cpp` (closes gap A)

**Files:**
- Modify: `src/port/original/opus_word1_ui_test.cpp` — specifically:
  - Line 129: `std::wcerr << L"window class='" ...` (in the window-logging callback)
  - Line 514: `bool send_physical_text(const std::wstring& text)` — signature itself; every one of its 9 call sites (lines 903, 953, 1013, 1059, 1153, 1256, 1305, 1409, 1627) implicitly constructs a temporary `std::wstring` from a `L"..."` literal to bind to this parameter, each an independent risk site
  - Line 1445: `const std::wstring sentence = L"physical keyboard input line one";` (inside `selection_mode`)
  - Line 1617: `const std::wstring physical_text = L"physical keyboard input line one\r..."` (inside `interaction_mode`)

**Interfaces:**
- Consumes: Task 1's fix pattern (same technique, different call sites).
- Produces: a version of `opus_word1_ui_test.cpp` where none of the 8
  `word1_startup_blocked` UI tests can crash from this bug class regardless
  of which code path a future change exercises. This is a prerequisite for
  trusting *any* failure message from Tasks 3-10 as the real, final
  behavior (not a masked crash).

- [x] **Step 1: Confirm current scope with a fresh grep (catch drift since this plan was written)**

```bash
grep -n "std::wstring\|std::wcerr" /home/pablo/msword/src/port/original/opus_word1_ui_test.cpp
```
Cross-check the count/lines against the 4 listed above (2 are comments
about already-fixed code, not live risk — read each hit before assuming
it's live).

- [x] **Step 2: Fix `send_physical_text`'s signature — removes 9 risk sites in one change**

Change the signature (line 514) from `const std::wstring&` to a raw
pointer, and its internal loop from range-`for` over the string to a
`lstrlenW`-bounded index loop:

```cpp
bool send_physical_text(const wchar_t* text) {
    const int length = lstrlenW(text);
    for (int index = 0; index < length; ++index) {
        const wchar_t character = text[index];
        WORD virtual_key = 0;
        if (character >= L'a' && character <= L'z') {
            virtual_key = static_cast<WORD>(character - L'a' + L'A');
        } else if (character >= L'0' && character <= L'9') {
            virtual_key = static_cast<WORD>(character);
        } else if (character == L' ') {
            virtual_key = VK_SPACE;
        } else if (character == L'\r') {
            virtual_key = VK_RETURN;
        } else {
            return false;
        }
        if (!send_virtual_key(virtual_key)) {
            return false;
        }
        Sleep(character == L'\r' ? 300 : 50);
    }
    return true;
}
```

None of the 9 call sites need to change — a `const wchar_t*` parameter
binds directly to a `L"..."` literal with no implicit `std::wstring`
construction. The one call site passing a variable (line 1627,
`send_physical_text(physical_text)`) is fixed in Step 4 below when
`physical_text` itself stops being a `std::wstring`.

- [x] **Step 3: Fix the `sentence` variable in `selection_mode` (line 1445-1452)**

Replace:
```cpp
const std::wstring sentence = L"physical keyboard input line one";
for (const wchar_t character : sentence) {
```
with:
```cpp
const wchar_t* const sentence = L"physical keyboard input line one";
const int sentence_length = lstrlenW(sentence);
for (int index = 0; index < sentence_length; ++index) {
    const wchar_t character = sentence[index];
```
(Close the loop body's existing brace structure unchanged — only the
declaration and loop header change.)

- [x] **Step 4: Fix the `physical_text` variable in `interaction_mode` (line 1617-1619)**

Replace:
```cpp
const std::wstring physical_text =
    L"physical keyboard input line one\rphysical keyboard input "
    L"line two\rphysical keyboard input line three";
```
with:
```cpp
const wchar_t* const physical_text =
    L"physical keyboard input line one\rphysical keyboard input "
    L"line two\rphysical keyboard input line three";
```
No other change needed at this site — `send_physical_text(physical_text)`
at line 1627 now binds directly (Step 2's new signature), and this
declaration was never iterated with a range-`for` (checked: it's only
passed straight into `send_physical_text`).

- [x] **Step 5: Fix or verify-safe the `std::wcerr` at line 129**

Read the full function containing line 129 (`log_window_callback` or
similar — confirm the name with
`sed -n '110,140p' src/port/original/opus_word1_ui_test.cpp`). If it prints
real `WCHAR` window class/caption content (it does, per the surrounding
`class_name`/`caption` variable names already seen), replace the
`std::wcerr <<` chain with `fwprintf(stderr, ...)` using `%ls` — `fwprintf`
resolves to glibc's own wide-print machinery consistently for both format
string and its own internal buffering, but the *arguments* being real
`WCHAR` data is the same risk as `wcslen`. Safest: convert to narrow via
`WideCharToMultiByte` (already used elsewhere in this codebase, e.g.
`opus_sdm_runtime.cpp` `refresh_font_control_value`) and print with
`std::cerr`/`fprintf`, matching the pattern other diagnostic output in this
file already uses. Write the minimal version — this is a log line, not
user-facing behavior, so a simple narrow conversion is enough; do not
over-engineer it.

- [x] **Step 6: Rebuild inside the container and run the full label**

```bash
cmake --build --preset linux-winelib-debug --target opus_word1_ui_test
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
```
Expected: same 8 specific real-behavior failure messages as
`01-diagnostico-heap-corruption-arranque.md` §26/§27's table (this task
does not fix any of them — it only removes crash risk). If any test now
crashes differently or a message changed unexpectedly, stop and treat that
as new information, not noise — re-open Phase 1 investigation before
continuing.

- [x] **Step 7: Document**

Append `### 29. Cierre del audit de std::wstring/std::wcerr en
opus_word1_ui_test.cpp — 4 sitios más (9 vía send_physical_text) sin
crashear en las 4/4 corridas` to
`docs/port-linux/01-diagnostico-heap-corruption-arranque.md`. List the
exact sites fixed (Steps 2-5) and the Step 6 ctest transcript (or a
representative excerpt) as evidence nothing regressed.

- [x] **Step 8: Commit**

```bash
git add src/port/original/opus_word1_ui_test.cpp \
        docs/port-linux/01-diagnostico-heap-corruption-arranque.md
git commit -m "fix(port): opus_word1_ui_test.cpp -- cierra el audit de std::wstring/std::wcerr: send_physical_text, sentence, physical_text, log callback"
```

---

### Task 3: Root-cause "modal dialog doesn't appear" via `--about` (highest-leverage behavioral bug)

**Files:**
- Read/instrument: `src/port/original/opus_sdm_runtime.cpp` — specifically
  the shared dialog-sizing/materialization block at lines 338-397 (`dialog.hid
  == kIddOpen || kIddNewDoc || kIddSaveAs || kIddAbout`), and
  `materialize_about_template()` (line 891 onward).
- Create: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`
  (this task creates it — see Global Constraints for why it's a new file).

**Interfaces:**
- Consumes: a green `word1_startup_blocked` label from Task 2 (real
  messages, no crashes).
- Produces: a finding — "shared root cause" or "independent per-dialog" —
  that Tasks 4 and 5 read before deciding whether they're verification-only
  or need their own full Phase 1 investigation.

- [x] **Step 1: Reproduce with full diagnostic capture**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_about_test --output-on-failure
```
The failure message already carries `stage=0 responsive=1` (per
`01-diagnostico-heap-corruption-arranque.md` §26's table) — find exactly
where `stage`/`responsive` are computed in
`src/port/original/opus_word1_ui_test.cpp`'s `about_mode` block (`grep -n
"about_mode" opus_word1_ui_test.cpp`) to know precisely what `stage=0`
means before forming a hypothesis.

- [x] **Step 2: Invoke `superpowers:systematic-debugging` explicitly for this bug**

Follow its Phase 1 in full: read `materialize_about_template` completely
(not just skim), trace what triggers it (which `WM_COMMAND`/menu action is
supposed to invoke `kIddAbout`), and check whether the dialog window is
created at all (`IsWindow`) versus created-but-invisible versus
never-reached. Use the same interactive-verification technique already
proven this session: run WORD1 directly in the container under a real
Xvfb display (`DISPLAY=:59` or whichever is live —
`ps aux | grep Xvfb` first), trigger Help > About manually via `xdotool`,
and screenshot with `xwd`/`convert` (copy the PNG into
`/home/pablo/msword/build/` — bind-mounted — so it can be read from the
host). Compare against a direct `wine WORD1.exe.so` launch (bypassing the
test harness entirely) to isolate harness-vs-app.

- [x] **Step 3: Determine shared-vs-independent root cause**

Specifically check: does the same code path handle `kIddNewDoc` and
`kIddSaveAs` identically at the point of failure (the line 338-397 shared
block), or does `kIddAbout` diverge before reaching whatever is broken?
This determines whether Tasks 4-5 are "verify the same fix" or "independent
investigations."

- [ ] **Step 4: Apply the minimal fix for the confirmed root cause**

No placeholder here by design — the exact fix depends on Step 2/3's
finding, which is not yet known. Follow `systematic-debugging` Phase 4:
single hypothesis, minimal change, re-verify before declaring done. If the
root cause traces into `src/Opus/` (unlikely — the SDM runtime shim in
`src/port/original/` is the newer, more probable location — but possible),
stop and get authorization per the restricted-tree rule before editing.

- [ ] **Step 5: Verify**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_about_test --output-on-failure
```
Expected: `Passed`.

- [ ] **Step 6: Document (creates the new file)**

Create `docs/port-linux/03-comportamiento-word1-startup-blocked.md` with a
short header (mirror `00-reconocimiento.md`'s opening style: what this
file is, links back to `01-diagnostico-...md` §26 as the origin of the
7-item list) and `## 1. --about: "Help About dialog did not appear"` with
the full root cause, fix, and verification.

- [ ] **Step 7: Commit**

```bash
git add src/port/original/opus_sdm_runtime.cpp \
        docs/port-linux/03-comportamiento-word1-startup-blocked.md
git commit -m "fix(port): opus_sdm_runtime.cpp -- corrige 'Help About dialog did not appear': <causa raíz real, una frase>"
```

---

### Task 4: Verify/fix File > New dialog (`opus_word1_ui_test` base mode)

**Files:**
- Same as Task 3 (`opus_sdm_runtime.cpp`, `materialize_new_template` at line 810).
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

**Interfaces:**
- Consumes: Task 3's shared-vs-independent finding.
- Produces: File > New confirmed working (or its own independent root
  cause + fix, if Task 3 found the bugs are not shared).

- [ ] **Step 1: If Task 3 found a shared root cause, verify only**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_ui_test --output-on-failure
```
If this now passes without any further change, skip to Step 4.

- [ ] **Step 2: If independent, repeat Task 3's Steps 1-2 against this dialog**

Reproduce, invoke `systematic-debugging` Phase 1, inspect
`materialize_new_template` (line 810) the same way.

- [ ] **Step 3: Apply and verify the fix**

Same discipline as Task 3 Step 4-5.

- [ ] **Step 4: Document**

Append `## 2. File > New: "File New dialog did not appear"` to
`docs/port-linux/03-comportamiento-word1-startup-blocked.md` — state
plainly whether this was "same root cause as #1, fixed by Task 3" or an
independent investigation, with evidence either way.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "fix(port): opus_sdm_runtime.cpp -- corrige 'File New dialog did not appear': <causa raíz o 'mismo root cause que About, ver Task 3'>"
```
(If Step 1's verify-only passed with zero code changes, this is a
documentation-only commit — say so in the message, don't fabricate a code
change.)

---

### Task 5: Verify/fix Save As dialog (`opus_word1_save_as_test`)

**Files:**
- Same as Task 3 (`opus_sdm_runtime.cpp`, `materialize_save_as_template` at line 1082).
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

**Interfaces:**
- Consumes: Task 3's finding (same as Task 4).
- Produces: Save As confirmed working or independently fixed.

- [ ] **Step 1: If Task 3 found a shared root cause, verify only**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_save_as_test --output-on-failure
```

- [ ] **Step 2: If independent, repeat Task 3's Steps 1-2**

The failure message already carries `Save As stage=0` — same approach as
Task 3 Step 1: find exactly what `stage` means in the `save_as_mode` block
of `opus_word1_ui_test.cpp` before hypothesizing.

- [ ] **Step 3: Apply and verify the fix**

Same discipline as Task 3.

- [ ] **Step 4: Document**

Append `## 3. Save As: "File Save As dialog did not appear"` to
`docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "fix(port): opus_sdm_runtime.cpp -- corrige 'File Save As dialog did not appear': <causa raíz o referencia a Task 3>"
```

---

### Task 6: Ribbon font-name/point-size dropdowns (`--font-typing`) — today's original bug report

**Files:**
- `src/port/original/opus_sdm_runtime.cpp` — `installed_windows_fonts()`
  (line 1188), `populate_windows_font_control()` (line 1234),
  `materialize_icon_bar_template()` (line 1662), the notification-dispatch
  block around lines 1940-2000 (focus-routing suspect).
- `src/Opus/rulerdrw.c` `UpdateRibbon`/`UpdateLHProp` (read-only reference —
  confirmed correct and unguarded/original this session, unlikely to be the
  bug).
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

**Interfaces:**
- Consumes: nothing from Tasks 3-5 (independent subsystem — combo boxes,
  not modal dialogs).
- Produces: resolution of the user's original bug report from this
  session.

**Head start already done this session (read before re-investigating):**
Confirmed via a standalone `winegcc`-compiled probe that
`EnumFontFamiliesExA` returns 67 real font entries (~17 unique names:
Liberation Sans/Serif/Mono, DejaVu families, Tahoma, MS Sans Serif, Symbol,
Wingdings...) in the `debian13` container — font enumeration itself is not
broken. Confirmed the *displayed* current font name (e.g. "Arial") comes
from the document's own font table (`vhsttbFont`, `Opus/rulerdrw.c`
`UpdateLHProp` `IDLKSFONT` case) while the *dropdown list* of choices comes
from `installed_windows_fonts()`'s Linux-system enumeration — two
disconnected name universes; the current font essentially never appears as
a selectable option in its own dropdown. Visually reproduced (screenshots,
not kept) that both dropdown popups render as solid unpainted black
rectangles under Xvfb — inconclusive whether this is an Xvfb-specific
rendering artifact or a real Wine/app bug, because this investigation had
no access to a real (non-headless) display session. Keyboard Down-arrow
navigation gave inconsistent signals (once landed on "9", once no change) —
not reliable evidence, don't treat it as confirmed.

- [ ] **Step 1: Get access to a real (non-Xvfb) display for this task**

This is the one task in this plan that could not be fully closed
headlessly. Before investing further time, either: (a) run the repro on
the user's actual Hyprland session (ask them to run the commands below and
report back what they see — the black-popup finding may not reproduce
there at all), or (b) install a minimal non-Xvfb-but-still-headless
compositor in the container (e.g. `weston` with `--backend=headless` or a
nested `Xephyr`) that exercises real compositing/theming instead of bare
Xvfb, to rule out "Xvfb has no window manager or theme" as the cause of
the black popup before assuming it's an app bug.

- [ ] **Step 2: Reproduce with the automated test for a baseline**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_font_typing_test --output-on-failure
```
Expected: `"font typing test could not find the ribbon controls"` (searches
for `CB_FINDSTRINGEXACT` "Courier New" and "24" — note "Courier New" will
never be found given the current design, since `installed_windows_fonts()`
returns Linux family names, not Windows aliases; consider whether the test
itself needs updating to search for a name that's actually enumerable,
e.g. "Liberation Sans", as part of this task's fix — that's a legitimate
outcome, not a cop-out, if the dropdown turns out to work correctly with
real (non-alias) names).

- [ ] **Step 3: Invoke `systematic-debugging` Phase 1 with the real display from Step 1**

Specifically resolve: is the popup genuinely empty (data bug in
`populate_windows_font_control`/`replace_list_entries`) or painted-black-
with-real-data (rendering bug, likely outside this project's code —
document as a Wine limitation per the same standard CLAUDE.md already
applies to the `CreateProcessW` zero-PID case, if so confirmed)? Use
keyboard Down-arrow navigation as corroborating evidence only, not as the
primary signal (it was unreliable this session).

- [ ] **Step 4: If a real code-level root cause is found, fix it minimally**

If the finding is "Wine/environment limitation, not this project's bug"
(parallel to the `CreateProcessW` zero-PID precedent in
`docs/port-linux/01-diagnostico-heap-corruption-arranque.md` §25), the
correct outcome is a documented workaround or an explicit "not a project
bug" writeup — do not force a code change where none is warranted.

- [ ] **Step 5: Verify**

Re-run Step 2's `ctest` command. If the fix included updating the test's
expected font names, confirm the new expectation is itself real evidence
(a name actually present in `installed_windows_fonts()`'s output on the
target environment) — not just changed to make the test pass.

- [ ] **Step 6: Document**

Append `## 4. --font-typing: selector de fuente vacío, tamaño en "9"` to
`docs/port-linux/03-comportamiento-word1-startup-blocked.md` — include the
two-font-name-universe finding, the Step 1 display-access resolution, and
the final verdict (real bug + fix, or environment limitation + workaround).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "fix(port): opus_sdm_runtime.cpp -- corrige selector de fuente/tamaño del ribbon: <causa raíz real, una frase>"
```

---

### Task 7: Ctrl+A / Select All (`opus_word1_clipboard_shortcut_test`)

**Files:**
- `src/port/original/opus_sdm_runtime.cpp` and/or `src/Opus/` command
  dispatch for `bcmSelectAll`/equivalent (locate with `grep -rn
  "SelectAll\|bcmSelectAll" src/Opus/*.c src/port/original/*.cpp`).
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

**Interfaces:**
- Consumes: nothing from prior tasks (independent — keyboard shortcut
  dispatch, not dialogs or ribbon).
- Produces: confirmed Select All behavior, or its root cause if broken.

- [ ] **Step 1: Reproduce**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_clipboard_shortcut_test --output-on-failure
```
Expected current failure: `"Ctrl+A did not execute Select All"`.

- [ ] **Step 2: Invoke `systematic-debugging` Phase 1**

Trace: does the Ctrl+A keystroke reach WORD1 at all (check with
`OpusX64TraceRibbon`-style instrumentation or a direct `WM_KEYDOWN` log)?
Does it map to the right command ID? Does that command actually change
`selCur`? Use `kWmOpusX64QuerySelection` (already used by the test itself
to read back selection state — see `opus_word1_ui_test.cpp`'s
`selected_first`/`selected_lim` reads) to bisect where the chain breaks.

- [ ] **Step 3: Apply the minimal fix for the confirmed root cause**

- [ ] **Step 4: Verify**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_clipboard_shortcut_test --output-on-failure
```

- [ ] **Step 5: Document**

Append `## 5. --clipboard: "Ctrl+A did not execute Select All"` to
`docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "fix(port): <archivo tocado> -- corrige 'Ctrl+A did not execute Select All': <causa raíz real, una frase>"
```

---

### Task 8: Canonical insertion selection (`opus_word1_selection_test`)

**Files:**
- Likely shares root cause territory with Task 7 (both are selection-state
  bugs) — check Task 7's finding first before starting a fresh
  investigation.
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

**Interfaces:**
- Consumes: Task 7's finding (selection-subsystem root cause, if any
  overlap exists).
- Produces: confirmed canonical-selection behavior after typing, or its
  root cause.

- [ ] **Step 1: Reproduce**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_selection_test --output-on-failure
```
Expected current failure: `"typing did not leave a canonical insertion
selection"`. Note this test also exercises the `sentence` variable fixed in
Task 2 Step 3 — confirm the failure message is unchanged from before Task
2 (i.e. this is a real behavior bug, not a side effect of that fix) before
proceeding.

- [ ] **Step 2: Check for overlap with Task 7 first**

Read Task 7's documented root cause. If it's in shared selection-state code
(`selCur`, `cpFirst`/`cpLim` handling), test whether Task 7's fix already
resolved this one too by re-running Step 1 before writing any new code.

- [ ] **Step 3: If not resolved, invoke `systematic-debugging` Phase 1 independently**

Use `kWmOpusX64QuerySelection` reads (same technique as Task 7 Step 2) to
compare expected vs. actual `cpFirst`/`cpLim` after typing.

- [ ] **Step 4: Apply and verify the fix**

- [ ] **Step 5: Document**

Append `## 6. --selection: "typing did not leave a canonical insertion
selection"` to `docs/port-linux/03-comportamiento-word1-startup-blocked.md`
— state explicitly whether this shared Task 7's root cause.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "fix(port): <archivo tocado> -- corrige selección canónica tras escritura: <causa raíz o referencia a Task 7>"
```

---

### Task 9: `--typing` exits 13/15 without crashing (post-crash-fix root cause)

**Files:**
- `src/port/original/opus_word1_ui_test.cpp` — the `typing_mode` block
  (exit codes 13 = "active document pane has no focus", 15 = "could not
  post a character to the document" — `grep -n '"active document pane
  has no focus"\|"could not post a character' opus_word1_ui_test.cpp` to
  find the exact `fail()` call sites).
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

**Interfaces:**
- Consumes: Task 2's crash fix (this bug only became visible once the
  crash stopped masking it).
- Produces: confirmed typing behavior, or root cause of the focus/posting
  failure.

- [ ] **Step 1: Reproduce, multiple runs (the doc notes 13 vs 15 varies by run — capture both)**

```bash
for i in 1 2 3 4; do
  ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_typing_test --output-on-failure
done
```
Record which exit appears on which run — this variability is itself a clue
(per `01-diagnostico-heap-corruption-arranque.md` §27: "podría ser una
condición de carrera genuina del propio test contra el foco de la ventana,
no relacionada" — confirm or refute that guess rather than assuming it).

- [ ] **Step 2: Invoke `systematic-debugging` Phase 1**

If it reproduces as a race (intermittent, timing-sensitive), follow
`condition-based-waiting` technique (available as a supporting technique
under `systematic-debugging`) — replace whatever fixed `Sleep()` precedes
the failing focus/post call with a real condition poll
(`GetForegroundWindow`/`GetFocus` matching the expected `HWND`) instead of
guessing at a longer sleep.

- [ ] **Step 3: Apply and verify the fix**

Re-run Step 1's 4-iteration loop after the fix — flakiness needs repeated
verification, not a single green run.

- [ ] **Step 4: Document**

Append `## 7. --typing: exit 13/15 tras el fix del crash (§27)` to
`docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "fix(port): opus_word1_ui_test.cpp -- corrige exit 13/15 en --typing: <causa raíz real, una frase>"
```

---

### Task 10: Native window move test (`opus_word1_interaction_test`)

**Files:**
- `src/port/original/opus_word1_ui_test.cpp` — the `interaction_mode`
  block, specifically whatever precedes `"could not prepare the native
  window move test"` (`grep -n "could not prepare the native window move"
  opus_word1_ui_test.cpp`).
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`.

**Interfaces:**
- Consumes: Task 2's fix to `physical_text` in this same test mode (verify
  that fix didn't change this failure's message/location before
  investigating further).
- Produces: confirmed window-move interaction, or its root cause. Last
  task in the plan — after this, all 7 behavioral bugs from the original
  gap analysis have a documented outcome (fixed, or confirmed
  environment/Wine limitation).

- [ ] **Step 1: Reproduce**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R opus_word1_interaction_test --output-on-failure
```

- [ ] **Step 2: Invoke `systematic-debugging` Phase 1**

Check whether this is a Wine/window-manager limitation similar in kind to
the `CreateProcessW` zero-PID precedent (§25) — window move/resize via
`SetWindowPos`/`WM_MOVE` under a headless Xvfb with no window manager can
behave differently than under a real compositor. Use the same real-display
access resolved in Task 6 Step 1 if this turns out to be display-dependent
— don't re-solve that problem from scratch here.

- [ ] **Step 3: Apply and verify the fix, or document the environment limitation**

- [ ] **Step 4: Document**

Append `## 8. --interaction: "could not prepare the native window move
test"` to `docs/port-linux/03-comportamiento-word1-startup-blocked.md`,
plus a short closing summary section (`## Resumen`) listing all 8 items
1-8 with their final status (fixed / environment limitation / deferred) —
this closes out the document Task 3 created.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "fix(port): opus_word1_ui_test.cpp -- corrige prueba de movimiento de ventana nativa: <causa raíz real, una frase>"
```

- [ ] **Step 6: Full-suite final verification**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
```
Report the final pass/fail tally plainly — if some remain failing as
confirmed environment limitations (not bugs), that's a legitimate, honest
end state; don't claim more than what's verified.
