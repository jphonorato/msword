# Round-trip File Save As / Open Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A new `opus_word1_roundtrip_test` types a known ASCII sentence into WORD1, saves it as a real `.doc` via File > Save As, starts a second WORD1 on that file, and checks that characters, `ftc`/`hps`, and formatted line height match the snapshot taken before save.

**Architecture:** Stay in the existing UI harness (`opus_word1_ui_test.cpp` + CTest). Drive the **real** Wine common dialog (`#32770`, "Save As") the same way Task 5 already proved: the SDM decoy `OpusSdmDialog` is not the dialog that owns the path. After the file exists on disk, tear down process 1 and `CreateProcessW` process 2 with that path on the command line (`initwin.c` already tokenizes `lpszCmdLine` into `rgpchArg`). Compare via `kWmOpusX64QuerySelection` already implemented in `Opus/wproc.c` (codes 41, 51, 52, 55, 69). Do not add a save-to-path backdoor.

**Tech Stack:** C++20 Winelib harness, CTest, Wine `GetSaveFileNameA` (already hooked in `opus_sdm_runtime.cpp`).

**Spec:** this plan. Context: `docs/port-linux/03-comportamiento-word1-startup-blocked.md` §7 (Save As decoy vs `#32770`); `src/Opus/wproc.c` query codes; `out/linux-winelib-debug/generated/original/opuscmd.h` (`imiOpen` = 1843, `imiSaveAs` = 1897).

## Global Constraints

- Debian 13, preset `linux-winelib-debug`, DISPLAY/Xvfb as the other `word1_startup_blocked` tests.
- `src/Opus/` is forbidden unless a save/load bug is proven in original code; then stop and report, do not patch without a new authorization.
- No `std::wstring` / `std::wcsstr` / `std::wcerr` on real `WCHAR` (wide-char rule in `CLAUDE.md` / §23).
- Do not change `--save-as` (cancel-only). This is a new mode `--roundtrip`.
- Do not touch `--interaction`, `EraNameFromFtc`, `res.c`, or `disp.c`.
- `OFN_OVERWRITEPROMPT` is on: **delete the target file first** so Wine does not show Yes/No.
- After a successful save, Word writes a short 8.3 staging file and copies to the path the user picked (`OpusFinishWin95SaveAlias`). The test asserts the **picked** path, not the staging name.
- Label `word1_startup_blocked`, TIMEOUT 45. Non-gating like the siblings.

---

## File Structure

- Modify: `src/port/original/opus_word1_ui_test.cpp` — new `--roundtrip` mode
- Modify: `src/CMakeLists.txt` — register `opus_word1_roundtrip_test`
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md` — new section
- Test: `opus_word1_roundtrip_test`, then `-L word1_startup_blocked`

Helpers already in the harness to reuse: `send_physical_text`, `make_foreground_and_focus`, `wait_for_window`, `find_descendant_by_class`, `fail`, `PostMessageW` File commands, query 41/51/52/55/69.

Constants already in the file:

```cpp
constexpr WPARAM kFileNew = 1813;
constexpr WPARAM kFileSaveAs = 1897;
constexpr WPARAM kFileExit = 2095;
```

Add:

```cpp
constexpr WPARAM kFileOpen = 1843; /* opuscmd.h imiOpen */
```

---

### Task 1: Save As writes a `.doc` the harness can see

**Files:**
- Modify: `src/port/original/opus_word1_ui_test.cpp`
- Modify: `src/CMakeLists.txt`
- Test: `opus_word1_roundtrip_test` (will still fail on reopen until Task 2; Task 1 may leave the mode incomplete — prefer landing both tasks in one executable, two commits if you split)

**Interfaces:**
- Consumes: File > Save As = 1897, decoy forward of IDOK from Task 5, `GetSaveFileNameA`.
- Produces: on-disk `.doc` at a path the test chose; in-memory snapshot `{cp_mac, bytes[0..cp_mac), ftc0, hps0, dypLine0}`.

- [ ] **Step 1: Register the test**

In `src/CMakeLists.txt`, next to `opus_word1_save_as_test`:

```cmake
add_test(NAME opus_word1_roundtrip_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --roundtrip
)
set_tests_properties(opus_word1_roundtrip_test PROPERTIES TIMEOUT 45 LABELS "word1_startup_blocked")
```

Wire `--roundtrip` in the mode parser the same way as `--save-as` (raw `lstrcmpW`, not `std::wstring`).

- [ ] **Step 2: Type and snapshot (process 1)**

After the main window `"Microsoft Word - Document1"` is up, focus `OpusWwd` with `make_foreground_and_focus` (same as `--typing`). Type with `send_physical_text` only the allowed alphabet (a–z, 0–9, space, `\r`):

```text
roundtrip line one
```

Sleep 500 ms. Snapshot via `SendMessageW(pane, kWmOpusX64QuerySelection, …)`:

| Code | Meaning (`wproc.c`) | Store |
|---|---|---|
| 41 | `CpMacDocEdit` | `cp_mac` |
| 69, `lParam=cp` | raw byte at `cp` | `bytes[cp]` for `cp in [0, cp_mac)` |
| 51, `lParam=0` | `vchpFetch.ftc` at cp 0 | `ftc0` |
| 52, `lParam=0` | `vchpFetch.hps` at cp 0 | `hps0` |
| 55, `lParam=0` | `vfli.dypLine` at cp 0 | `dyp0` |

Fail if `cp_mac < 10` or any query 69 returns -1. Do not assert pixel bands.

- [ ] **Step 3: Choose a target path and delete it**

Winelib `GetTempPathA` + `opus_rt_<pid>.doc` (pid of WORD1 or of the harness; 8.3-safe: `oprtXXXX.doc` is enough). `DeleteFileA` if it exists. Keep both the ANSI path (for the dialog) and a wchar copy (for process 2's command line). No `std::wstring` concatenation: `lstrcpyW`/`lstrcatW` or a fixed `wchar_t[MAX_PATH]`.

- [ ] **Step 4: File > Save As and drive `#32770`**

`PostMessageW(main, WM_COMMAND, kFileSaveAs, 0)`. Wait up to 5 s for a top-level window **class `#32770`**, caption containing `Save As`, same process (the Task 5 real dialog). Do not consider the hidden `OpusSdmDialog` sufficient.

Set the file name on that dialog:

```cpp
/* CDM_SETCONTROLTEXT = WM_USER+100+4, edt1 = 0x0480 */
SendMessageA(save_dialog, WM_USER + 104, 0x0480,
             reinterpret_cast<LPARAM>(ansi_path));
```

If that does not stick (GetWindowText of the filename edit still empty), find a child `Edit` and `SetWindowTextA`. Then `PostMessageW(save_dialog, WM_COMMAND, IDOK, 0)` (or decoy `OpusSdmDialog` + `kTmcOk=1`, which Task 5 already forwards). Do **not** click the decoy Cancel path.

Wait until `GetFileAttributesA(ansi_path)` succeeds and `GetFileSize` > 128 bytes, timeout 8 s. If a second `#32770` "Confirm Save As" appears, click Yes once, then keep waiting. If the file never appears, dump window list (`log_process_windows`) and fail with the exact dialog class/caption found. **Do not add an internal Save-to-path API.**

- [ ] **Step 5: Tear down process 1**

`PostMessageW(main, WM_COMMAND, kFileExit, 0)`. If a "save changes" `#32770` appears, the save did not mark the document clean — fail with that caption, do not Yes-and-hope. Then `WaitForSingleObject` up to 5 s; if still alive, `TerminateProcess`. Close handles.

- [ ] **Step 6: Commit if you split here**

Only if Task 2 is a second commit. Otherwise continue.

```
test(port): opus_word1_ui_test --roundtrip -- File Save As deja un .doc en disco
```

---

### Task 2: Second process opens the file and matches the snapshot

**Files:**
- Modify: same `opus_word1_ui_test.cpp` (continue the mode)
- Append: `docs/port-linux/03-comportamiento-word1-startup-blocked.md`
- Test: `opus_word1_roundtrip_test`, `-L word1_startup_blocked`

**Interfaces:**
- Consumes: on-disk `.doc` from Task 1; `initwin.c` command-line file args.
- Produces: green `opus_word1_roundtrip_test`; label **9/10** (the tenth is `--interaction`, documented Wine/Xvfb).

- [ ] **Step 1: Launch process 2 with the path**

`CreateProcessW` on the same `WORD1` binary, command line:

```text
"…\WORD1.exe" "C:\users\…\oprtNNNN.doc"
```

(or whichever ANSI path Step 3 produced, widened with `MultiByteToWideChar`). Recover PID from the window if `PROCESS_INFORMATION` is zeroed (existing workaround).

Wait for a main window whose title contains `Microsoft Word` **and** the base name (`oprtNNNN`), timeout 8 s. If the title is still `Document1`, File > Open (`kFileOpen=1843`) and drive `#32770` "Open" the same way as Save As. If that also fails, stop and report titles/classes.

- [ ] **Step 2: Compare**

Focus `OpusWwd`. Re-read codes 41, 69 for each cp, 51/52/55 at cp 0.

Pass only if:

- `cp_mac` equal
- every query-69 byte equal
- `ftc0` and `hps0` equal
- `dyp0` equal (formatted line height, not client pixels)

Fail messages must say which field differed (`cpMac`, `byte@N`, `ftc`, `hps`, `dypLine`).

- [ ] **Step 3: Cleanup**

`TerminateProcess` process 2. `DeleteFileA` the `.doc` on every exit path (RAII or a `goto` cleanup). Leave no files in Temp.

- [ ] **Step 4: Verify**

```bash
cmake --build --preset linux-winelib-debug --target opus_word1_ui_test WORD1
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -R '^opus_word1_roundtrip_test$' --output-on-failure
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -R '^opus_word1_save_as_test$' --output-on-failure
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug \
  -L word1_startup_blocked --output-on-failure
```

Expected: roundtrip Passed (run twice); `--save-as` still Passed; label 9/10 with only `--interaction` Failed.

- [ ] **Step 5: Document and commit**

New `## N. --roundtrip` in `03-comportamiento`: what path was used, whether command-line open worked or File > Open was needed, file size, the four numbers compared. Do not claim byte-identical pagination against Windows MSVC; this oracle is the same WORD1 twice.

```
test(port): opus_word1_roundtrip_test -- save .doc, reopen, texto/ftc/hps/dypLine coinciden
```

Do not merge to `main`.

---

## Self-Review Notes

- Spec coverage: persist + reload + four fields. Dialog is the real `#32770` (Task 5 lesson). Staging copy is acknowledged.
- Stop condition: if the common dialog cannot be driven, report class/caption/file attributes; no `OpusMem`-style product patch and no secret save API.
- Type consistency: `kFileOpen=1843` matches `opuscmd.h`; query codes match `wproc.c`.
)
