# Tabla de sustitución de fuentes (§B2.5) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure and encode the physical-font substitution table Wine applies to
Word 1.1a's 4 default era font names (`Tms Rmn`, `Symbol`, `Helv`, `Courier`),
so Qt-2's future text-measurement contract (B2) has something concrete to
build a `QRawFont` from.

**Architecture:** A one-off Winelib probe measures what each era name resolves
to under the oracle (`GetTextFaceA`); `fc-match`/`fc-scan` resolve that family
name to a physical font file, cross-checked. The result is documented
(§B2.6) and hard-coded into a new `opus_core` static-lookup contract,
`OpusShellFontSubstitution.h`/`.cpp`, following the existing `OpusShell*`
pattern in `src/core/`.

**Tech Stack:** C (Winelib probe, compiled with `winegcc`), C++17 (`opus_core`
contract, no Qt dependency needed — pure data lookup), CMake/CTest
(`opus_core`'s existing `OPUS_CORE_BUILD_TESTS` gate), `fontconfig`
(`fc-match`, `fc-scan`).

## Global Constraints

- Scope is exactly the 4 default era names Word 1.1a loads at startup
  (`Opus/initwin.c`, `vhsttbFont` init block, `ftc` 0-3): `Tms Rmn`, `Symbol`,
  `Helv`, `Courier`. No other name (`Script`, `Modern`, or anything else).
- Only the regular weight/style is resolved to a file. Bold/italic are
  rasterizer-synthesized on the same file (§B2.5: `tmOverhang = 0` measured
  in all 8 style combinations under Wine TrueType) — do not add per-style
  file lookups.
- `src/Opus/` and `src/OpusEtAl/` are not touched by this plan (restricted
  tree per `CONTRIBUTING.md`) — this plan only adds files under
  `docs/port-qt/scripts/fidelity/`, `docs/port-qt/01-frontera-nucleo-shell.md`,
  and `src/core/`.
- `src/core/` code is native-compiler only (not winegcc), C++17,
  `CMAKE_POSITION_INDEPENDENT_CODE` already set at directory level —
  don't add per-target PIC flags.
- Measurement environment for this plan (record verbatim in the doc,
  matches §B2.3's environment): Wine 10.0 (Debian trixie repack), Qt 6.8.2,
  fontconfig resolving `Liberation Sans` to
  `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf`.

---

## File Structure

- `docs/port-qt/scripts/fidelity/font_substitution.c` (new) — Winelib probe,
  one-off, not part of the build. Prints resolved family + charset + overhang
  per era name.
- `docs/port-qt/scripts/fidelity/README.md` (modify) — add a row for the new
  probe, matching the existing table format.
- `docs/port-qt/01-frontera-nucleo-shell.md` (modify) — new `§B2.6` after
  `§B2.5`, with the measured table and verification commands.
- `src/core/include/OpusShellFontSubstitution.h` (new) — contract
  declaration, `extern "C"`, one function.
- `src/core/src/OpusShellFontSubstitution.cpp` (new) — hard-coded lookup
  table implementation.
- `src/core/src/OpusShellFontSubstitution_test.cpp` (new) — plain
  `Check()`-style test, same pattern as `OpusShellConfig_test.cpp`.
- `src/core/CMakeLists.txt` (modify) — add `opus_shell_font_substitution`
  static library target and its gated test, mirroring the existing
  `opus_shell_config`/`opus_shell_config_test` block.

---

### Task 1: Oracle probe — measure family resolution for the 4 era names

**Files:**
- Create: `docs/port-qt/scripts/fidelity/font_substitution.c`
- Modify: `docs/port-qt/scripts/fidelity/README.md`

**Interfaces:**
- Produces: measured text output (era name → resolved family, charset,
  overhang) that Task 3 transcribes into the doc table.

- [ ] **Step 1: Write the probe**

```c
/* docs/port-qt/scripts/fidelity/font_substitution.c
 *
 * Sonda de un solo uso para §B2.6: mide a qué familia resuelve el oráculo
 * Winelib cada uno de los 4 nombres de época que Word 1.1a carga por
 * defecto (Opus/initwin.c, vhsttbFont). No forma parte del build.
 */
#include <windows.h>
#include <stdio.h>

int main(void) {
    HDC hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    const char *names[] = { "Tms Rmn", "Symbol", "Helv", "Courier" };
    for (unsigned k = 0; k < sizeof(names) / sizeof(*names); k++) {
        LOGFONTA lf;
        ZeroMemory(&lf, sizeof lf);
        lf.lfHeight = -MulDiv(14, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfItalic = 0;
        lf.lfCharSet = ANSI_CHARSET;
        lstrcpynA(lf.lfFaceName, names[k], LF_FACESIZE);

        HFONT hf = CreateFontIndirectA(&lf);
        HFONT old = SelectObject(hdc, hf);

        TEXTMETRICA tm;
        GetTextMetricsA(hdc, &tm);
        char actual[LF_FACESIZE];
        GetTextFaceA(hdc, LF_FACESIZE, actual);

        printf("%-10s -> %-24s charset=%u overhang=%ld\n",
               names[k], actual, (unsigned)tm.tmCharSet, tm.tmOverhang);

        SelectObject(hdc, old);
        DeleteObject(hf);
    }
    DeleteDC(hdc);
    return 0;
}
```

- [ ] **Step 2: Compile and run it**

```bash
cd docs/port-qt/scripts/fidelity
winegcc -o font_substitution.exe font_substitution.c -lgdi32 -luser32
WINEDEBUG=-all ./font_substitution.exe
```

Expected output (measured against Wine 10.0, this environment):
```
Tms Rmn    -> Liberation Sans          charset=0 overhang=0
Symbol     -> Liberation Sans          charset=0 overhang=0
Helv       -> Liberation Sans          charset=0 overhang=0
Courier    -> Liberation Sans          charset=0 overhang=0
```

If the output differs on the machine actually running this (different Wine
version, different installed fonts), use the *actual* output for Task 3's
doc table, not the one above — the above is what this plan's author measured
and is the expected baseline, not a hard requirement.

- [ ] **Step 3: Clean up build artifacts, add README row**

```bash
rm -f font_substitution.exe font_substitution.exe.so
```

Add a row to the table in `docs/port-qt/scripts/fidelity/README.md`,
matching its existing format:

```
| `font_substitution.c` | oráculo | Resuelve los 4 nombres de época por defecto (`Tms Rmn`, `Symbol`, `Helv`, `Courier`) a familia real vía `GetTextFaceA`, para §B2.6 |
```

- [ ] **Step 4: Commit**

```bash
git add docs/port-qt/scripts/fidelity/font_substitution.c docs/port-qt/scripts/fidelity/README.md
git commit -m "docs(port-qt): sonda de sustitución de fuentes para §B2.6"
```

---

### Task 2: Resolve family names to physical files, cross-verified

**Files:**
- None created — this task's output is input to Task 3's doc write.

**Interfaces:**
- Consumes: the family name(s) printed by Task 1 (e.g. `Liberation Sans`).
- Produces: a verified `family -> absolute file path` mapping for Task 3 and
  Task 4.

- [ ] **Step 1: Resolve each distinct family from Task 1 to a file**

For each distinct family name Task 1 printed (in the baseline measurement,
only one: `Liberation Sans` — all 4 era names resolved to it):

```bash
fc-match -f '%{file}\n' "Liberation Sans"
```

Expected (this environment): `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf`

- [ ] **Step 2: Cross-check the file actually claims that family**

Don't trust `fc-match`'s answer without checking the file's own name table:

```bash
fc-scan --format '%{family}\n' /usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf
```

Expected: `Liberation Sans`. If step 1's path and step 2's family disagree,
stop and re-investigate before Task 3 — do not write an unverified path into
the doc or the contract.

- [ ] **Step 3: Record the verified command output**

Keep the four command outputs (Task 1's probe output, `fc-match`, `fc-scan`)
at hand — Task 3 transcribes them verbatim into the doc. No commit for this
task; it produces no files.

---

### Task 3: Document §B2.6 in the frontier design doc

**Files:**
- Modify: `docs/port-qt/01-frontera-nucleo-shell.md` (insert after §B2.5,
  before the `---` that precedes `## B3`)

**Interfaces:**
- Consumes: Task 1's probe output, Task 2's `fc-match`/`fc-scan` output.
- Produces: the measured table Task 4's implementation is transcribed from.

- [ ] **Step 1: Insert the new subsection**

Insert immediately after the end of §B2.5's last bullet (`"Compilación
condicional..."` paragraph) and before the `---` separator that precedes
`## B3 — Contrato de memoria Win16`:

```markdown
### B2.6 Tabla de sustitución de fuentes

Medido con la sonda `docs/port-qt/scripts/fidelity/font_substitution.c`
(mismo entorno que §B2.3: Wine 10.0, Debian trixie) para los 4 nombres de
época que `Opus/initwin.c` carga en la tabla maestra de arranque
(`vhsttbFont`, `ftc` 0-3):

```
Tms Rmn    -> Liberation Sans          charset=0 overhang=0
Symbol     -> Liberation Sans          charset=0 overhang=0
Helv       -> Liberation Sans          charset=0 overhang=0
Courier    -> Liberation Sans          charset=0 overhang=0
```

Los cuatro resuelven a la misma familia, incluido `Symbol`: contra lo que el
`ANSI_CHARSET` pedido en el `LOGFONTA` sugeriría, el oráculo devuelve
`tmCharSet=0` (ANSI) para los cuatro, no un charset simbólico para `Symbol`.
No hay tratamiento especial que preservar del lado shell — `Symbol` se
sustituye exactamente igual que los otros tres, no hace falta una fuente de
símbolos ni una tabla de glifos aparte.

Resolución de familia a archivo físico, verificada cruzado (no se acepta la
respuesta de `fc-match` sin confirmar contra la tabla `name` del propio
archivo):

```
$ fc-match -f '%{file}\n' "Liberation Sans"
/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf

$ fc-scan --format '%{family}\n' /usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf
Liberation Sans
```

Coincide: la familia que reporta la propia tabla `name` del archivo es la
misma que `fc-match` resolvió. Tabla final:

| Nombre de época | Familia resuelta | Archivo físico |
|---|---|---|
| `Tms Rmn` | Liberation Sans | `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf` |
| `Symbol` | Liberation Sans | `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf` |
| `Helv` | Liberation Sans | `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf` |
| `Courier` | Liberation Sans | `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf` |

Implementado como tabla estática en
`src/core/include/OpusShellFontSubstitution.h` /
`src/core/src/OpusShellFontSubstitution.cpp` — ver ese header para el
contrato. Solo cubre estos 4 nombres; `Script` y `Modern` quedan fuera de
alcance de esta medición (no están en la tabla maestra de arranque, ver
`01-frontera-nucleo-shell.md`, "Secuencia recomendada para Qt-2", paso 2).
```

If Task 1/2's actual measured output differs from the baseline above (see
Task 1 Step 2's note), use the actual output here instead — this section
must reflect what was really measured on the machine that ran it, not the
plan's baseline.

- [ ] **Step 2: Commit**

```bash
git add docs/port-qt/01-frontera-nucleo-shell.md
git commit -m "docs(port-qt): §B2.6 -- tabla de sustitución de fuentes medida"
```

---

### Task 4: `OpusShellFontSubstitution` contract — failing test first

**Files:**
- Create: `src/core/include/OpusShellFontSubstitution.h`
- Create: `src/core/src/OpusShellFontSubstitution_test.cpp`
- Test: same file as above (this contract's test is a standalone `main`,
  no separate test framework — matches `OpusShellConfig_test.cpp`)

**Interfaces:**
- Produces: `const char *OpusShellSubstituteFontFile(const char *eraName)`
  — declared here, implemented in Task 5. Returns an absolute path (`const
  char *` owned by the library, never freed by the caller) for one of the 4
  known era names, or `NULL` for anything else, including `NULL` input.

- [ ] **Step 1: Write the header (declaration only, like `OpusShellFontMetrics.h`)**

```c
/* src/core/include/OpusShellFontSubstitution.h
 *
 * Contrato de sustitución de fuentes de época entre el núcleo Qt y el
 * shell. Diseño: docs/port-qt/01-frontera-nucleo-shell.md, §B2.6.
 *
 * Cubre únicamente los 4 nombres que Opus/initwin.c carga en la tabla
 * maestra de fuentes al arrancar (Tms Rmn, Symbol, Helv, Courier). La ruta
 * devuelta es fija, medida contra el oráculo Winelib -- no se recalcula en
 * tiempo de ejecución ni depende de qué fuentes estén instaladas en la
 * máquina que corre el shell. Solo resuelve el peso regular: negrita y
 * cursiva se sintetizan sobre el mismo archivo (§B2.5: tmOverhang = 0
 * medido en los 8 casos de estilo bajo TrueType/Wine).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Devuelve la ruta absoluta al archivo de fuente física que sustituye al
 * nombre de época dado, o NULL si eraName es NULL o no es uno de los 4
 * nombres cubiertos (Tms Rmn, Symbol, Helv, Courier). El puntero devuelto
 * es propiedad de la biblioteca (cadena estática) -- el caller no lo libera
 * ni lo modifica.
 */
const char *OpusShellSubstituteFontFile(const char *eraName);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write the failing test**

```cpp
/* src/core/src/OpusShellFontSubstitution_test.cpp
 *
 * Prueba propia de OpusShellFontSubstitution.h
 * (docs/port-qt/01-frontera-nucleo-shell.md §B2.6). Sin Qt: es una tabla
 * de datos estática, mismo patrón de Check() que OpusShellConfig_test.cpp.
 */
#include "OpusShellFontSubstitution.h"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FALLÓ: %s\n", what);
        ++g_failures;
    }
}

}  // namespace

int main() {
    const char *kExpected =
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf";

    const char *names[] = { "Tms Rmn", "Symbol", "Helv", "Courier" };
    for (const char *name : names) {
        const char *path = OpusShellSubstituteFontFile(name);
        Check(path != nullptr, name);
        if (path != nullptr) {
            Check(std::strcmp(path, kExpected) == 0, name);
        }
    }

    Check(OpusShellSubstituteFontFile("Script") == nullptr,
          "Script no debe resolver -- fuera de alcance de esta tabla");
    Check(OpusShellSubstituteFontFile("Modern") == nullptr,
          "Modern no debe resolver -- fuera de alcance de esta tabla");
    Check(OpusShellSubstituteFontFile("nombre-inexistente") == nullptr,
          "nombre desconocido debe devolver NULL");
    Check(OpusShellSubstituteFontFile(nullptr) == nullptr,
          "NULL de entrada debe devolver NULL, no crashear");

    if (g_failures == 0) {
        std::printf("OpusShellFontSubstitution_test: %d verificaciones, todas bien.\n",
                    8);
        return 0;
    }
    std::fprintf(stderr, "OpusShellFontSubstitution_test: %d fallo(s).\n",
                 g_failures);
    return 1;
}
```

Note: `kExpected` is Task 3's measured path. If Task 3 recorded a different
path on the machine that actually ran Task 1/2, use that path here instead
— this test and the implementation in Task 5 must agree with the doc.

- [ ] **Step 3: Wire the test target into `src/core/CMakeLists.txt` (test only, no impl target yet)**

This step alone won't compile — the library target lands in Task 5. Skip
straight to Task 5; the header from Step 1 above and the test from Step 2
are inputs to it, not yet buildable on their own since `opus_core`'s
`CMakeLists.txt` needs the library target added in the same edit as the
test target (CMake `add_test` on a not-yet-defined executable would break
configure). Do not commit after Step 2 — Task 4 and Task 5 land in one
commit, at the end of Task 5.

---

### Task 5: Implement `OpusShellFontSubstitution` and wire the build

**Files:**
- Create: `src/core/src/OpusShellFontSubstitution.cpp`
- Modify: `src/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `OpusShellFontSubstitution.h` (Task 4), the test file (Task 4).
- Produces: `opus_shell_font_substitution` static library target,
  `opus_shell_font_substitution_test` gated test target/executable.

- [ ] **Step 1: Implement the lookup**

```cpp
/* src/core/src/OpusShellFontSubstitution.cpp
 *
 * Implementación del contrato de sustitución de fuentes de época
 * (src/core/include/OpusShellFontSubstitution.h,
 * docs/port-qt/01-frontera-nucleo-shell.md §B2.6).
 *
 * Tabla estática, medida una vez contra el oráculo Winelib -- no vuelve a
 * medir en tiempo de ejecución. Ver §B2.6 para el método de medición
 * (GetTextFaceA para la familia, fc-match/fc-scan cruzados para el
 * archivo).
 */
#include "OpusShellFontSubstitution.h"

#include <cstring>

namespace {

struct SubstitutionEntry {
    const char *eraName;
    const char *filePath;
};

/* Los 4 nombres de época de la tabla maestra de arranque
   (Opus/initwin.c, vhsttbFont, ftc 0-3). Los cuatro resuelven a la misma
   familia bajo el oráculo medido -- ver §B2.6, incluido Symbol, que no
   recibe tratamiento de charset simbólico especial. */
const SubstitutionEntry kSubstitutionTable[] = {
    { "Tms Rmn", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
    { "Symbol",  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
    { "Helv",    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
    { "Courier", "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
};

}  // namespace

const char *OpusShellSubstituteFontFile(const char *eraName) {
    if (eraName == nullptr) {
        return nullptr;
    }
    for (const auto &entry : kSubstitutionTable) {
        if (std::strcmp(eraName, entry.eraName) == 0) {
            return entry.filePath;
        }
    }
    return nullptr;
}
```

- [ ] **Step 2: Add the library and gated test to `src/core/CMakeLists.txt`**

Insert after the `install(TARGETS opus_shell_config ...)` line and before
the `# Contrato de memoria Win16 (B3)` comment block:

```cmake
# Contrato de sustitución de fuentes de época (§B2.6). Tabla de datos
# estática, medida contra el oráculo Winelib -- ver
# docs/port-qt/01-frontera-nucleo-shell.md §B2.6. No depende de Qt.
add_library(opus_shell_font_substitution STATIC
    src/OpusShellFontSubstitution.cpp
)
target_include_directories(opus_shell_font_substitution PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
install(TARGETS opus_shell_font_substitution ARCHIVE DESTINATION lib)

if(OPUS_CORE_BUILD_TESTS)
    add_executable(opus_shell_font_substitution_test
        src/OpusShellFontSubstitution_test.cpp
    )
    target_link_libraries(opus_shell_font_substitution_test PRIVATE
        opus_shell_font_substitution
    )
    add_test(NAME opus_shell_font_substitution_test
             COMMAND $<TARGET_FILE:opus_shell_font_substitution_test>)
    install(TARGETS opus_shell_font_substitution_test RUNTIME DESTINATION bin)
endif()
```

- [ ] **Step 3: Configure and build**

```bash
cmake -S src/core -B /tmp/opus_core_build -DOPUS_CORE_BUILD_TESTS=ON
cmake --build /tmp/opus_core_build
```

Expected: clean build, both `opus_shell_font_substitution` and
`opus_shell_font_substitution_test` targets succeed.

- [ ] **Step 4: Run the test, verify it passes**

```bash
ctest --test-dir /tmp/opus_core_build -R opus_shell_font_substitution_test -V
```

Expected: `OpusShellFontSubstitution_test: 8 verificaciones, todas bien.`,
exit code 0.

- [ ] **Step 5: Also confirm it builds with tests off**

```bash
cmake -S src/core -B /tmp/opus_core_build_notest -DOPUS_CORE_BUILD_TESTS=OFF
cmake --build /tmp/opus_core_build_notest
```

Expected: clean build, only `opus_shell_font_substitution` target exists,
no test target configured.

- [ ] **Step 6: Clean up local build dirs**

```bash
rm -rf /tmp/opus_core_build /tmp/opus_core_build_notest
```

- [ ] **Step 7: Commit**

```bash
git add src/core/include/OpusShellFontSubstitution.h \
        src/core/src/OpusShellFontSubstitution.cpp \
        src/core/src/OpusShellFontSubstitution_test.cpp \
        src/core/CMakeLists.txt
git commit -m "core(port-qt): Qt-2 -- contrato de sustitución de fuentes (§B2.6), implementado y verificado"
```

---

## Self-Review Notes

- **Spec coverage:** probe + README row (spec deliverable 1) → Task 1. Doc
  §B2.6 with measured table (deliverable 2) → Task 3. Header + impl + CMake
  wiring (deliverable 3) → Tasks 4-5. Symbol open question → resolved
  empirically in Task 1/3 (no special treatment needed, documented as such,
  not left open). `Script`/`Modern` out-of-scope note → carried into §B2.6
  text and the header comment.
- **Placeholder scan:** all code blocks are complete and copy-pasteable; no
  TODO/TBD. The two "if actual output differs, use the real one" notes in
  Tasks 1 and 3 are not placeholders — they're honest handling of a
  measurement step whose numeric result depends on the executing machine's
  installed fonts/Wine version, same as §B2.3's original experiment. The
  baseline given was measured live during planning (Wine 10.0, this
  environment) and is expected to reproduce.
- **Type consistency:** `OpusShellSubstituteFontFile(const char *)` is the
  same signature in the header (Task 4), the test (Task 4), and the
  implementation (Task 5). Target name `opus_shell_font_substitution`
  matches across CMakeLists additions and the commit message.
