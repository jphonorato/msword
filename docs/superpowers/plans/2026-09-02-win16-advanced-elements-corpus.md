# Corpus Win16 por generación interna: marcadores, campos, notas al pie, saltos de sección/página

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Un nuevo modo `--advanced-elements` en `opus_word1_ui_test.cpp` que, vía la UI real de WORD1 (no manipulación directa de bytes), inserta un marcador, un campo, una nota al pie y un salto forzado que produzca más de una entrada en `plcfpgd`/`plcfsed`, guarda con Save As real, y dispara `doc_inspector` sobre el `.doc` resultante -- reutilizando la maquinaria ya existente (`opus_doc_inspector_test`, `RunDocInspector.cmake`, `keep_doc_artifact`) sin tocarla.

**Por qué "generación interna" y no un banco de archivos vintage descargados:** WORD1.exe.so bajo Winelib **es** el motor Word 1.1a real -- no una reimplementación. Cualquier `.doc` que él mismo escriba es, por construcción, un binario Win16 genuino con el mismo códec que un archivo de un floppy de 1990. Buscar archivos externos añade un problema de procedencia/licencia sin resolver (se planteó en una sesión anterior y quedó sin respuesta) y no compra nada que este enfoque no dé ya. Si en el futuro aparecen archivos vintage reales de fuente confiable, son un complemento, no un requisito.

**Architecture:**

- Patrón a replicar: `rich_format_mode` (~línea 2374 en adelante de `opus_word1_ui_test.cpp` a fecha de este plan) ya hace exactamente esta forma -- aplica varias operaciones de edición vía `WM_COMMAND`/menú real sobre un documento, guarda con el diálogo Save As real, deja una copia en `OPUS_X64_DOC_ARTIFACT_DIR` vía `keep_doc_artifact(ansi_path, "rich_format")`, y `RunDocInspector.cmake` la recoge automáticamente (hace `file(GLOB "${ARTIFACT_DIR}/*.doc")`, sin lista fija de nombres). El nuevo modo es ese mismo patrón con otro conjunto de ediciones y otro tag (`"advanced_elements"` o similar) -- **cero cambios necesarios en `RunDocInspector.cmake` ni en `doc_inspector.cpp`**, ambos ya son genéricos y ya validan `plcfbkf/plcfbkl` (marcadores, ver `PrintBookmarks` en `doc_inspector.cpp:1672-1689`), `plcffldMom/Hdr/Ftn/Atn/Mcr` (campos), `plcffndRef/Txt` (notas al pie) y `plcfpgd`/`plcfsed` (páginas/secciones) -- confirmado leyendo `doc_inspector.cpp` en esta sesión.
- IDs de `WM_COMMAND` reales, confirmados en `out/linux-winelib-debug/generated/original/opuscmd_native.inc` de esta sesión (no adivinar, no re-derivar):
  - **Marcador:** `CmdInsBookmark`, id **3360**, símbolo `InsertBookmark`. Clase 3 en la tabla generada (mismo valor de clase que `CmdChangePrinter`, que sí abre un diálogo real -- tratar como "probablemente abre diálogo" hasta confirmarlo en runtime, no como certeza).
  - **Campo:** dos comandos distintos, no confundirlos -- `CmdInsertField` (id **711**, símbolo `InsertFieldChars`, clase 1 -- probablemente inserción directa de los delimitadores de campo sin diálogo, equivalente a Ctrl+F9) vs `CmdInsField` (id **3326**, símbolo `InsertField`, clase 3 -- probablemente abre un diálogo para elegir tipo/nombre de campo). Cualquiera de los dos sirve para poblar `plcffld*`; el de diálogo (3326) es más representativo de un campo "real" con contenido, pero requiere descubrir los controles del diálogo en runtime (mismo patrón que Task 2 de la sesión anterior con `chgpr.hs` -- aquí no hay un `.hs`/`.des` ya localizado, buscarlo primero).
  - **Nota al pie:** `CmdInsFootnote`, id **3189**, símbolo `InsertFootnote`, clase 3.
  - **Salto de página forzado:** `CmdInsPageBreak`, id **6217**, símbolo `InsertPageBreak`, clase 1 (probablemente inserción directa, sin diálogo).
  - **NO existe** un comando "Insert Section Break" separado encontrado en esta sesión (`CmdSection`, id 3590, símbolo `FormatSection`, es el diálogo Formato>Sección para propiedades de la sección actual, no necesariamente el que crea una sección nueva). `Opus/ch.h:12` define `chSect = 12` -- un carácter literal que, insertado en el flujo de texto (como ya se insertan otros caracteres en `typing_mode`/`font_typing_mode` de este mismo archivo), es la forma en que Word 1.x representa un salto de sección en el documento. **No asumir cuál es el mecanismo real de inserción de UI** -- confirmarlo leyendo `Opus/dlglook2.c`'s `CmdSection` y buscando dónde se inserta `chSect` (probablemente en `Opus/*.c` vía `FReplace` o similar) antes de escribir código. Es lectura de `Opus/`, no edición -- no requiere autorización, sólo edición del árbol restringido la requiere.
- Encadenar `OPUS_X64_DOC_ARTIFACT_DIR` y el fixture `opus_saved_doc_artifacts`: en `src/CMakeLists.txt`, las líneas `set_property(TEST opus_word1_roundtrip_test opus_word1_formatting_test APPEND PROPERTY ENVIRONMENT ...)` y la siguiente con `FIXTURES_SETUP` (~líneas 1719-1722) listan los nombres de test que dejan artefacto -- añadir el nuevo nombre de test a esas DOS líneas es lo único que hace falta para que `opus_doc_inspector_test` (cuyo `FIXTURES_REQUIRED opus_saved_doc_artifacts`, sin cambios) también inspeccione el nuevo `.doc`. No hace falta tocar `RunDocInspector.cmake`.
- `keep_doc_artifact`/`discard_doc_artifact` (ya definidos en el archivo, usados por `roundtrip_mode`/`rich_format_mode`) son las funciones a reusar -- no reinventar el guardado del artefacto.

**Tech Stack:** C++20 test harness (`src/port/original/opus_word1_ui_test.cpp`), Win32 vía Winelib, CMake/CTest (`src/CMakeLists.txt`). Lectura de `Opus/*.c`/`*.h` permitida y necesaria para investigación; **edición de `src/Opus/` y `src/OpusEtAl/` prohibida** (árbol restringido, sin autorización para este plan). `doc_inspector.cpp`/`RunDocInspector.cmake` no deberían necesitar cambios -- si la investigación revela que sí hacen falta, es un hallazgo a reportar, no algo a implementar sin más en este plan (ver Global Constraints).

**Spec:** Este documento; `src/port/tools/doc_inspector/doc_inspector.cpp` (ya cubre las estructuras objetivo, confirmado en esta sesión); `out/linux-winelib-debug/generated/original/opuscmd_native.inc` (IDs de comando); `Opus/ch.h` (constantes de caracteres); `src/CMakeLists.txt` (patrón de registro de tests y fixture de artefactos).

## Global Constraints

- No editar `src/Opus/` ni `src/OpusEtAl/`. Leerlos para investigar el mecanismo real de cada inserción está permitido y es parte explícita del trabajo.
- Si la investigación concluye que `doc_inspector.cpp` o `RunDocInspector.cmake` necesitan cambios reales (no sólo el artefacto nuevo) para validar algo que hoy no cubren, **parar y reportar** en vez de extender su alcance sin más -- ambos están fuera del árbol restringido así que técnicamente se podrían tocar, pero este plan asume que no hace falta (confirmado por lectura de `doc_inspector.cpp` en la sesión de planificación) y un cambio ahí es una decisión de diseño más grande que amerita su propio ciclo de revisión, no un efecto colateral de esta task.
- El nuevo modo debe seguir el patrón de fallo explícito de `roundtrip_mode`/`rich_format_mode`: si aparece un diálogo inesperado (`#32770`) o cualquier paso no completa, `fail()` con un código y mensaje claros -- no intentar recuperarse a ciegas.
- No mergear a `main`; el mantenedor decide el merge.
- Si un mecanismo de inserción concreto (ej. el diálogo de campo, o `CmdInsBookmark`) resulta bloqueado por algo genuinamente no resuelto en un tiempo razonable de investigación, es aceptable entregar el modo con un subconjunto de las cuatro inserciones y reportarlo como tal -- no es necesario resolver las cuatro para que la task tenga valor real (cada PLC que doc_inspector valide con contenido real es progreso). Documentar claramente cuáles quedaron fuera y por qué.

---

## File Structure

- Modify: `src/port/original/opus_word1_ui_test.cpp` -- nuevo modo `--advanced-elements` (bloque `advanced_elements_mode`, mismo patrón que los demás `*_mode`)
- Modify: `src/CMakeLists.txt` -- registrar `opus_word1_advanced_elements_test`, añadirlo a las dos líneas de `OPUS_X64_DOC_ARTIFACT_DIR`/`FIXTURES_SETUP`
- Modify (si hace falta, documentar por qué): `docs/port-linux/03-word1-startup-blocked-behavior.md` -- nueva entrada con lo aprendido sobre los mecanismos de inserción reales
- Test: `opus_word1_advanced_elements_test` (nuevo), `opus_doc_inspector_test` (debe seguir pasando, ahora sobre 3 artefactos en vez de 2)

---

### Task 1: Modo `--advanced-elements` -- marcador, campo, nota al pie, salto forzado

**Files:**
- Modify: `src/port/original/opus_word1_ui_test.cpp`
- Modify: `src/CMakeLists.txt`
- Test: `opus_word1_advanced_elements_test`, `opus_doc_inspector_test`

**Interfaces:**
- Consumes: `keep_doc_artifact`/`discard_doc_artifact`, `main_window`, el patrón `PostMessageW(main_window, kWmCommand, <id>, 0)` + `wait_for_window`/`control_has_class` ya usado en todo el archivo, `OPUS_X64_DOC_ARTIFACT_DIR` (env var ya wireado).
- Produce: un `.doc` con al menos una entrada real (no vacía/trivial) en cada una de `plcfbkf`/`plcfbkl`, `plcffldMom` (o el PLC de campo que corresponda al mecanismo elegido), `plcffndRef`/`plcffndTxt`, y `plcfpgd`/`plcfsed` con más de una entrada.

- [ ] **Step 1: Investigación de mecanismos reales (sin escribir código todavía)**

Para cada uno de los cuatro elementos, confirmar por lectura de `Opus/` (no por suposición) el flujo real de UI:

1. `CmdInsBookmark` (3360): leer `Opus/print2.c`-equivalente para bookmarks (buscar `FDlgChgPr`-style handler, probablemente en `dlglook2.c`, `dialog2.c` o similar -- grep `InsBookmark`/`CmdInsBookmark` en `Opus/*.c`) -- ¿abre diálogo? ¿qué controla pide (nombre)? ¿requiere selección previa de texto o funciona con el cursor solo?
2. Campo -- decidir entre `CmdInsertField` (711, directo) y `CmdInsField` (3326, diálogo) leyendo ambos handlers; el directo es más simple y suficiente para poblar el PLC si el diálogo resulta complejo.
3. `CmdInsFootnote` (3189): ¿diálogo, o inserción directa que cambia el foco al panel de notas? Si cambia de panel, cómo escribir el texto de la nota y volver al panel principal (buscar precedente de multi-panel en este mismo archivo si existe, o en `Opus/` cómo referencia el panel de notas).
4. Salto forzado: confirmar en `Opus/dlglook2.c`'s `CmdSection` y en el código que maneja `chSect`/`chPage`-equivalente si un salto de página/sección se inserta como carácter literal vía `FReplace` (en cuyo caso enviar ese carácter con el mismo mecanismo que `typing_mode` ya usa para escribir texto) o si `CmdInsPageBreak` (6217) hace algo distinto. El objetivo es lograr **más de una** entrada en `plcfpgd` y, si es alcanzable sin un mecanismo de sección aparte no documentado, también en `plcfsed` -- si resulta que sólo se puede forzar `plcfpgd` (page break) sin tocar `plcfsed` (section break) en el tiempo razonable de esta investigación, es aceptable entregar sólo eso y documentarlo (ver Global Constraints).

Documentar los hallazgos de este step (aunque sea en el propio mensaje de reporte) antes de pasar al Step 2 -- si algo no está claro, es preferible reportar `NEEDS_CONTEXT`/`BLOCKED` en un mecanismo puntual y seguir con los demás, que adivinar y romper todo el modo.

- [ ] **Step 2: Implementar `advanced_elements_mode`**

Seguir el esqueleto de `rich_format_mode` (~línea 2374): crear documento (o reusar el patrón de `roundtrip_mode`/`rich_format_mode` para partir de un `Document1` con contenido base vía `typing_mode`-style `WM_CHAR`), luego para cada elemento confirmado en el Step 1: enviar el `WM_COMMAND`, conducir cualquier diálogo con `wait_for_window`/`control_has_class`/`PostMessageW` (mismo patrón que el resto del archivo), verificar con las queries de `WM_OPUS_X64_QUERY_SELECTION` que ya expone `Opus/wproc.c` si hay una query relevante para confirmar el estado tras cada inserción (opcional pero recomendable, igual que `--formatting` hace con las queries 82/83/84).

Tras las cuatro inserciones (o el subconjunto que Step 1 confirmó viable), Save As real a un `.doc` con `keep_doc_artifact(ansi_path, "advanced_elements")`, cerrar limpio.

- [ ] **Step 3: Registrar el test en CMake**

En `src/CMakeLists.txt`, junto a los demás `add_test(NAME opus_word1_*_test ...)` (~línea 1683 en adelante a fecha de este plan): añadir

```cmake
add_test(NAME opus_word1_advanced_elements_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --advanced-elements
)
set_tests_properties(opus_word1_advanced_elements_test PROPERTIES TIMEOUT 45 LABELS "word1_startup_blocked")
```

Y añadir `opus_word1_advanced_elements_test` a las DOS listas de nombres en las llamadas `set_property(TEST opus_word1_roundtrip_test opus_word1_formatting_test ...)` (una para `ENVIRONMENT`, una para `FIXTURES_SETUP`) unas líneas más abajo -- no crear un fixture nuevo, sumarse al existente `opus_saved_doc_artifacts`.

- [ ] **Step 4: Build y verificación**

```bash
export DISPLAY=:91   # Xvfb ya corriendo en este display -- no levantar otro, confirmar con `ps aux | grep Xvfb` antes si hay duda
cmake --build --preset linux-winelib-debug --target opus_word1_ui_test
ctest --test-dir out/linux-winelib-debug -R '^opus_word1_advanced_elements_test$' --output-on-failure
# repetir una vez más
ctest --test-dir out/linux-winelib-debug -R '^opus_doc_inspector_test$' --output-on-failure
```

`opus_doc_inspector_test` debe seguir pasando (ahora inspecciona 3 artefactos, no 2) y su salida (`--verbose` ya está en `RunDocInspector.cmake`) debe mostrar entradas no triviales para `plcfbkf`/`plcfbkl`, el/los PLC de campo elegido, `plcffndRef`/`plcffndTxt`, y `plcfpgd` (más de 1 entrada) -- revisar la salida a mano, no sólo el exit code, para confirmar que de verdad hay contenido y no sólo tablas vacías que igual "pasan" por no tener nada que objetar.

Luego label completo:

```bash
ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
```

Comparar contra la línea base post-merge de esta VPS (12/14 antes de este cambio, con `interaction_test` y `print_preview_test` como únicos fallos conocidos) -- ahora debería ser 13/15 (un test nuevo, sin regresiones en los demás).

- [ ] **Step 5: Documentar y commitear**

Añadir una entrada a `docs/port-linux/03-word1-startup-blocked-behavior.md` con: qué mecanismo real resultó ser cada inserción (útil para quien lo retome), qué PLCs quedaron con contenido real vs. cuáles (si alguno) no se logró poblar y por qué.

```
feat(port): --advanced-elements -- marcadores, campos, notas al pie y saltos vía UI real para doc_inspector
```

No mergear a `main` en este task.

---

## Self-Review Notes

- La justificación de "generación interna en vez de corpus externo" está en el Architecture, no sólo en la conversación -- para que quien lea el plan sin el historial de chat entienda por qué no hay una task de "buscar archivos .doc vintage".
- `doc_inspector.cpp` y `RunDocInspector.cmake` fueron leídos en la sesión de planificación y confirmados genéricos/suficientes -- el plan lo dice explícito para que no se re-investigue desde cero, y pone una guarda (Global Constraints) contra extender su alcance sin necesidad real.
- Los IDs de `WM_COMMAND` son reales (de `opuscmd_native.inc`), no inventados -- pero los mecanismos de diálogo/interacción exacta NO están confirmados (a diferencia del plan de teardown-tolerance, donde `chgpr.hs` dio los IDs de controles exactos) -- Step 1 existe precisamente para no repetir el error de asumir un flujo de UI sin haberlo leído primero.
- Alcance parcial es un resultado aceptable y está dicho explícitamente (Global Constraints) -- no se necesita bloquear todo el task si un solo mecanismo (ej. el diálogo de campo) resulta más complejo de lo esperado.
