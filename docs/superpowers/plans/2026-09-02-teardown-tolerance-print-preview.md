# Tolerancia al teardown de Wine + arranque de Print Preview

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Task 1 -- `opus_word1_ui_test` (base smoke) deja de fallar en la VM `debian13` por el código de salida que Wine reporta durante la descarga de DLLs post-`exit(0)`, sin debilitar la verificación de cierre limpio. Task 2 -- `opus_word1_print_preview_test` (añadido en `a8ed4f3`, hoy en `fail(process, 203, "print preview mode did not activate")`) arranca `lmPreview` de verdad, sembrando una impresora en el `WINEPREFIX` y conduciendo el diálogo real de Print Setup.

**Architecture:**

- Task 1 no toca `src/Opus/` -- es puramente arnés (`src/port/original/opus_word1_ui_test.cpp`). El código actual en la rama por defecto de `wmain` (sin ningún `*_mode` activado -- ésa es la que ejecuta `word1_port_smoke_test`) es la ÚNICA rama del archivo que trata `GetExitCodeProcess(...) != 0` como fallo (`fail(process, 12, ...)`, línea ~4593). Las otras dos ramas que también hacen `File Exit` de un documento ya guardado limpio -- `roundtrip_mode` (~línea 1736) y `rich_format_mode` (~línea 2374) -- **no** comprueban el código de salida: sólo verifican que no aparezca el diálogo "guardar cambios" y que `WaitForSingleObject(process.hProcess, 5000)` señalice (con `TerminateProcess` como red de seguridad si no). Ese es el patrón ya aceptado en este mismo archivo para "cierre limpio" -- Task 1 alinea el bloque de dos documentos con él, añadiendo además la comprobación explícita que pide el plan: la ventana principal debe estar destruida (`IsWindow(main_window) == FALSE`) dentro del plazo.
- Esta máquina (hostname `vps`) **no reproduce** el fallo -- `docs/port-linux/*.md` ya documenta que "two-document File Exit was not clean" es específico de la VM `debian13` dentro de `exia`. El implementador no podrá confirmar aquí que pasa de fail a pass; sólo puede confirmar que sigue en verde localmente y que la lógica nueva es correcta por inspección/precedente. La confirmación real de 20/21 en `debian13` queda para una corrida posterior fuera de esta sesión -- no reclamar "arreglado en debian13" sin haber corrido allí.
- Task 2 SÍ tiene código nuevo, pero acotado al arnés -- nada de editar fuentes en `Opus/` (árbol restringido, requiere autorización explícita que esta plan no tiene). La investigación ya hecha en `docs/port-linux/03-word1-startup-blocked-behavior.md` §14 estableció que `FPrinterOK()` (`Opus/command2.c:1783`, `vpri.hszPrinter != NULL`) sólo se satisface por una vía: `ChgPr()` (`Opus/print2.c:1346`), llamado exclusivamente por el manejo real del diálogo Print Setup/Change Printer (`FDlgChgPr`, `IDDChgPr` = 51, `Opus/print2.c:1077`, descrito en `Opus/dlg/chgpr.des`: listbox `tmcChgPrLb` + `OK_BUTTON`/`CANCEL_BUTTON` + botón opcional `&Setup...` `tmcChgPrSetup`). El flujo real de `CmdChangePrinter` (`Opus/print2.c:897-1039`) es: abrir el diálogo (falla con `eidNoPrinters` si `FFillChgPrLb` no encuentra nada) -> usuario selecciona un ítem del listbox y pulsa OK (no hace falta pulsar "Setup...", ese botón sólo abre la configuración avanzada del driver) -> al cerrar con OK, si `FCheckPrinter()` valida el driver/puerto, llama `ChgPr()` -- eso es lo único que puede poner `vpri.hszPrinter` en no-NULL.
- **Hallazgo clave de esta sesión (no estaba en §14): hay DOS almacenes de configuración distintos en juego, no uno.** `FFillChgPrLb` (`Opus/print2.c:836`) usa `GetProfileString(SzShared("devices"), NULL, ...)` sin guardia -- ese `key=NULL` (enumerar nombres) siempre golpea el `win.ini` real de Wine en ambas plataformas, sin importar el toolchain. Pero la siguiente llamada en el mismo bucle (`Opus/print2.c:851-859`, buscar el spec de cada impresora encontrada) SÍ está guardada `#if defined(__GNUC__) && !defined(_MSC_VER)` y en Linux/Winelib usa `OpusShellProfileString()` en vez de `GetProfileString` -- lo mismo pasa en `FCheckPrinter` (`Opus/print2.c:1422-1424`, la validación que corre al aceptar el diálogo). `OpusShellProfileString`/`OpusShellProfileWrite` (`src/core/src/OpusShellConfig.cpp`) están respaldados por **`QSettings` en `~/.config/OpusShell/Word1.ini`** (confirmado en esta sesión -- ese archivo ya existe y ya tiene contenido real de otros call sites migrados, p. ej. `[extensions]`, `[windows]`; WORD1 ya enlaza `libopus_shell_config.a` de verdad, `src/CMakeLists.txt:243`). Es decir: **sembrar sólo `win.ini [devices]` hace que el nombre de la impresora aparezca listado, pero `FFillChgPrLb` igual devuelve `fFalse` (diálogo nunca abre, `eidNoPrinters`) porque el spec del driver/puerto (línea 852-854) sale vacío de `OpusShellProfileString`, que no tiene nada en esa sección.** Hacen falta las DOS escrituras, en las DOS secciones `[devices]`, con el mismo nombre de impresora como clave:
  1. `win.ini` del `WINEPREFIX` activo, sección `[devices]` -- sólo necesita existir la CLAVE (el nombre de la impresora); el valor no se usa por esta vía. Vía `WriteProfileStringA("devices", "<nombre>", "<lo que sea>")` desde el propio arnés (Win32 real, mismo proceso/WINEPREFIX que WORD1 heredará).
  2. `~/.config/OpusShell/Word1.ini` (o donde resuelva `$XDG_CONFIG_HOME` si está seteado -- confirmar antes de asumir la ruta fija), sección `[devices]`, clave = mismo nombre de impresora, valor = `"<DRIVER>,<PUERTO>"` (formato confirmado en `Opus/SYSCHG.C:555` `ParseDeviceSz` -- driver y puerto separados por coma, p. ej. `"WINEPS.DRV,LPT1:"`). Esto es un archivo INI de texto plano estándar (confirmado leyéndolo) -- el arnés puede escribirlo directo con I/O de archivo normal (abrir en modo append/lectura-modificación, añadir la sección si no existe) sin enlazar Qt ni `opus_shell_config` -- más simple y sin tocar CMake. Si el implementador prefiere enlazar `opus_shell_config` y llamar `OpusShellProfileWrite()` directamente, es una alternativa válida (WORD1 ya prueba que el link cruzado winegcc/nativo funciona), pero no es necesario.
- Riesgo documentado en §14 que el implementador debe vigilar y reportar si aparece: tras seleccionar la impresora, `FCheckPageAndMargins`/`FPageOK` (`Opus/print1.c:218`) puede comparar el tamaño de página real del driver sintético contra el documento y disparar un `IdMessageBoxMstRgwMb` de desajuste que el arnés tendría que descartar también. Si aparece, documentarlo y decidir el tamaño de página del driver sintético para que coincida con el documento de prueba en vez de añadir un dismiss genérico que enmascare desajustes reales.

**Tech Stack:** C++20 test harness (`src/port/original/opus_word1_ui_test.cpp`), Win32 vía Winelib, `win.ini` del `WINEPREFIX` activo, `~/.config/OpusShell/Word1.ini` (QSettings INI plano). Sin editar fuentes en `src/Opus/` ni `src/core/` -- sólo se lee/escribe el archivo de configuración en runtime que `src/core/` ya produce, igual que cualquier otro dato de usuario.

**Spec:** `docs/port-linux/03-word1-startup-blocked-behavior.md` §14 (investigación de `FPrinterOK()`/`ChgPr`, con la corrección del hallazgo de los dos almacenes arriba); `Opus/print2.c` (`CmdChangePrinter`, `FFillChgPrLb`, `FCheckPrinter`, `ParseDeviceSz` en `Opus/SYSCHG.C`); `Opus/dlg/chgpr.des` (controles del diálogo); `src/core/src/OpusShellConfig.cpp`; comentario en línea del propio harness sobre el exit-code de Wine (línea ~1326); CLAUDE.md (árbol `Opus/` restringido).

## Global Constraints

- No tocar `src/Opus/` ni `src/OpusEtAl/` en ninguna task de este plan -- ambas tienen ruta sin pasar por ahí.
- No debilitar ninguna aserción existente que no sea la exacta discutida en cada task (Task 1: sólo el `exit_code != 0` de la rama de dos documentos; Task 2: nada existente que tocar, es código nuevo).
- Task 1: la nueva comprobación debe seguir fallando si el proceso NO sale a tiempo o si la ventana principal sigue viva -- "tolerante al exit code" no es "tolerante a que WORD1 se quede colgado".
- Task 2: si `FPageOK` u otro diálogo de desajuste aparece y bloquea, reportar como `BLOCKED` con el diagnóstico en vez de forzar un dismiss ciego -- ese caso no está resuelto por este plan.
- No mergear a `main`; el mantenedor decide el merge.

---

## File Structure

- Modify: `src/port/original/opus_word1_ui_test.cpp`
  - Task 1: bloque de `File Exit` de dos documentos (~línea 4586-4594, rama por defecto de `wmain`)
  - Task 2: bloque `print_preview_mode` (~línea 1381 en adelante), antes de `PostMessageW(main_window, kWmCommand, kFilePrintPreview, 0)`
- Test: `opus_word1_ui_test` (`ctest -R '^opus_word1_port_smoke_test$'` -- confirmar nombre exacto de target vía `ctest -N` si difiere), `opus_word1_print_preview_test`

---

### Task 1: Tolerar el exit code post-`exit(0)` de Wine en el cierre de dos documentos

**Files:**
- Modify: `src/port/original/opus_word1_ui_test.cpp`
- Test: el target de `word1_port_smoke_test` (confirmar nombre exacto con `ctest --test-dir out/linux-winelib-debug -N | grep -i smoke`)

**Interfaces:**
- Consumes: `main_window` (HWND ya obtenido), `process.hProcess` (handle ya recuperado del PID real, ver comentario en línea ~1326-1338).
- Produce: el mismo `return 0` de éxito; sólo cambia el criterio de fallo.

- [ ] **Step 1: Confirmar el nombre exacto del target CTest y que hoy pasa aquí (VPS)**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -N | grep -i smoke
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R '<nombre confirmado>' --output-on-failure
```

Esperado: Passed (esta máquina no reproduce el fallo de debian13 -- eso es lo esperado, no un problema).

- [ ] **Step 2: Reemplazar la comprobación de exit code por verificación de ventana destruida**

Hoy (~línea 4590-4594):

```cpp
    if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) {
        return fail(process, 11, "two-document File Exit timed out");
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.hProcess, &exit_code) || exit_code != 0) {
        return fail(process, 12, "two-document File Exit was not clean");
    }
```

Cambiar a: mantener el `fail(11)` de timeout sin tocar (ya verifica que el proceso señalizó a tiempo). Sustituir el bloque de `exit_code` por una comprobación de que la ventana principal esté destruida, logueando el exit code como diagnóstico en vez de usarlo como criterio de fallo -- exactamente el patrón que ya usan `roundtrip_mode` (~línea 1736) y `rich_format_mode` (~línea 2374) para el mismo `File Exit` de un documento limpio, más la verificación explícita de ventana que pide este plan:

```cpp
    if (WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0) {
        return fail(process, 11, "two-document File Exit timed out");
    }
    // Wine's DLL-unload sequencing after a clean exit(0) can report exit
    // code 1 from GetExitCodeProcess despite the engine having exited
    // cleanly -- see the long comment above wait_for_window() near the top
    // of main() (~line 1326) and docs/port-linux/03-word1-startup-blocked-
    // behavior.md. The exit code is not a reliable clean-teardown witness
    // under Wine; the destroyed main window is. roundtrip_mode and
    // rich_format_mode already treat "File Exit after a clean save" this
    // way (no exit-code assertion) -- this brings the two-document path in
    // line with that precedent.
    DWORD exit_code = 0;
    GetExitCodeProcess(process.hProcess, &exit_code);
    std::cerr << "two-document File Exit: exit_code=" << exit_code
              << " mainWindowDestroyed=" << !IsWindow(main_window) << '\n';
    if (IsWindow(main_window)) {
        return fail(process, 12,
                    "two-document File Exit main window still exists");
    }
```

No cambiar el código `12` de `fail()` (mismo identificador, nueva causa) salvo que el reviewer señale colisión con otro uso -- verificar con `grep -n "fail(process, 12" src/port/original/opus_word1_ui_test.cpp` que sigue siendo el único sitio.

- [ ] **Step 3: Rebuild y correr el target dos veces**

```bash
cmake --build --preset linux-winelib-debug --target opus_word1_ui_test
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -R '<nombre confirmado>' --output-on-failure
# repetir
```

Esperado: Passed ambas veces (aquí ya pasaba; el objetivo es no regresionar mientras se cambia el criterio).

- [ ] **Step 4: Label completo, sin regresiones**

```bash
ctest --test-dir /home/pablo/msword/out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
```

Comparar contra la línea base documentada en CLAUDE.md (9 gating + label 10/12 en esta VPS). No debe empeorar ningún test que hoy pasa.

- [ ] **Step 5: Documentar y commitear**

Añadir una entrada breve a `docs/port-linux/03-word1-startup-blocked-behavior.md` (nueva subsección o al final de §14/adyacente) notando: criterio de cierre limpio para dos documentos cambiado de exit-code a ventana destruida, por qué (exit code no fiable post-`exit(0)` bajo Wine, mismo patrón que roundtrip/rich-format), y que la confirmación real del 20/21 en `debian13` queda pendiente de una corrida en esa VM (esta sesión corrió en `vps`, que no reproduce el fallo original).

```
fix(port): tolerar exit code de Wine tras exit(0) limpio, verificar ventana destruida
```

---

### Task 2: Sembrar impresora sintética + conducir Print Setup para activar `lmPreview`

**Files:**
- Modify: `src/port/original/opus_word1_ui_test.cpp`
- Test: `opus_word1_print_preview_test`

**Interfaces:**
- Consumes: `win.ini` del `WINEPREFIX` activo (vía `WriteProfileStringA`/`GetProfileStringA`, Win32 estándar, ya vinculado); `main_window`; el patrón `control_has_class` + `PostMessageW(..., kWmCommand, ...)` ya usado para `OpusSdmDialog` en el bloque de `File New` (~línea 4562-4576) y en otras dialog-driving sections del mismo archivo (buscar `IDDChgPr` no existe aún en el harness -- el diálogo se identifica por clase de ventana en runtime, igual que `OpusSdmDialog`; confirmar la clase real de la ventana Print Setup con `log_process_windows`/`dump_dialog_tree_diagnostic`, ya presentes en el archivo, antes de asumir el nombre).
- Produce: `vpri.hszPrinter` no-NULL dentro del proceso WORD1 (efecto secundario interno, no observable directo desde el arnés salvo porque `FPrinterOK()` deja de bloquear `CmdPrintPreview`), y por tanto que `query 87` (ya usado en el test, línea ~1396) se active tras `kFilePrintPreview`.

**Contexto que el brief no puede saber por sí solo (cárguelo el despachador):**
- No hay `cupsd` corriendo en esta máquina, sólo `libcups2` como librería -- no asumir un demonio CUPS disponible. La entrada de `win.ini [devices]` debe apuntar a un driver que Wine resuelva sin CUPS (p. ej. `WINEPS.DRV` sobre un puerto ficticio tipo `LPT1:`, o el driver "Generic / Text Only" que Wine trae embebido -- confirmar cuál está realmente disponible en este prefix con `wine wineboot`/`winecfg -v` o inspeccionando `/home/pablo/.wine/drive_c/windows/inf/` antes de fijar el nombre exacto).
- El comando de menú que abre el diálogo de cambiar impresora es DIRECTO, no anidado dentro de `File > Print`: `WM_COMMAND` id **2056** (símbolo `FilePrinterSetup` -> `CmdChangePrinter`, confirmado en `out/linux-winelib-debug/generated/original/opuscmd.h:78` y `opuscmd_native.inc:531` de esta sesión). Añadir `constexpr WPARAM kFilePrinterSetup = 2056;` junto a los demás `kFile*` del archivo (junto a `kFileExit`, `kFilePrintPreview`) y `PostMessageW(main_window, kWmCommand, kFilePrinterSetup, 0)` -- no hace falta pasar por el diálogo de `File > Print` ni pulsar ningún botón "Setup...".
- Los IDs numéricos reales de los controles del diálogo `IDDChgPr` (listbox `tmcChgPrLb`, OK, Cancel, botón `tmcChgPrSetup`) NO están en ningún header generado de este build (se comprobó -- no aparecen en `out/linux-winelib-debug/generated/`); se asignan dentro del blob binario que compila `Opus/dlg/chgpr.des` y no hay atajo textual para leerlos. No adivinar los números -- lanzar el diálogo una vez con `FFillChgPrLb` ya satisfecho (Step 2) y usar `dump_dialog_tree_diagnostic`/`log_process_windows` (ya existen en el archivo, usados en otros modos) para enumerarlos en runtime antes de escribir el `PostMessageW` que selecciona el ítem y acepta. El patrón OK=1/Cancel=2 que usa el diálogo `OpusSdmDialog` de `File New` (línea ~4562) es un candidato razonable a probar primero, no una certeza.
- **Sembrar la impresora requiere escribir en DOS almacenes, no uno** (ver el hallazgo de esta sesión en la sección Architecture arriba) -- omitir cualquiera de los dos deja `FFillChgPrLb` devolviendo `fFalse` y el diálogo nunca abre:
  1. `win.ini` del `WINEPREFIX` activo, sección `[devices]`, sólo la clave (nombre de impresora) importa aquí.
  2. `~/.config/OpusShell/Word1.ini` (confirmar la ruta real resuelta en este entorno -- puede variar si `$XDG_CONFIG_HOME` está seteado), sección `[devices]`, clave = mismo nombre, valor = `"<DRIVER>,<PUERTO>"`.
- `FPrinterOK()` no se cachea entre re-ejecuciones del test -- cada proceso WORD1 nuevo empieza con `vpri.hszPrinter == NULL` y necesita la conducción del diálogo en cada corrida del test, no sólo una vez en el prefix. Los dos archivos de configuración SÍ persisten entre corridas (son archivos en disco) -- sembrarlos una vez por ejecución del test alcanza, pero conducir el diálogo (Step 3) hay que hacerlo en cada proceso WORD1 nuevo.

- [ ] **Step 1: Confirmar el bloqueo actual**

```bash
export DISPLAY=:91   # Xvfb ya corriendo en este display -- no levantar otro
ctest --test-dir /home/pablo/msword-teardown/out/linux-winelib-debug -R '^opus_word1_print_preview_test$' --output-on-failure
```

Esperado (línea base, confirmada en esta sesión): `print preview mode did not activate (query 87)` (fail code 203). Si ya no es así, parar y reportar el nuevo estado en vez de seguir con este plan.

- [ ] **Step 2: Sembrar los DOS almacenes de configuración desde el propio arnés**

Antes de `CreateProcessW` en `print_preview_mode` (el arnés y WORD1 comparten el mismo `WINEPREFIX` y el mismo `$HOME`):
1. `WriteProfileStringA("devices", "<nombre elegido>", "<DRIVER>,<PUERTO>")` -- puebla `win.ini` para que `FFillChgPrLb` (línea 836, sin guardia) encuentre el nombre.
2. Escribir/añadir en `~/.config/OpusShell/Word1.ini` la sección `[devices]` con `<nombre elegido>=<DRIVER>,<PUERTO>` -- I/O de archivo de texto plano normal (leer el archivo si existe, añadir la sección si falta, no pisar el resto del contenido -- ya tiene datos reales de otros call sites). Confirmar la ruta exacta resuelta en este entorno antes de hardcodearla.

Verificar ambas escrituras releyendo con `GetProfileStringA`/lectura de archivo antes de lanzar WORD1.

- [ ] **Step 3: Conducir el diálogo real de cambiar impresora (`FDlgChgPr`, `IDDChgPr`=51)**

Añadir `constexpr WPARAM kFilePrinterSetup = 2056;` junto a los demás `kFile*` (junto a `kFileExit`, `kFilePrintPreview`) -- es un comando de menú directo (`FilePrinterSetup` -> `CmdChangePrinter`), no pasa por el diálogo de `File > Print`. Enviar `PostMessageW(main_window, kWmCommand, kFilePrinterSetup, 0)`, esperar la ventana del diálogo con `wait_for_window` (clase real a determinar en runtime -- puede ser `OpusSdmDialog` como los demás diálogos SDM del archivo, o distinta; comprobarlo con `log_process_windows`/`dump_dialog_tree_diagnostic` si `wait_for_window` con `L"OpusSdmDialog"` no la encuentra).

Antes de intentar `PostMessageW` sobre controles específicos, volcar el árbol del diálogo con `dump_dialog_tree_diagnostic` para obtener los IDs numéricos reales del listbox (`tmcChgPrLb`) y del botón OK -- no están en ningún header generado de este build (confirmado, no aparecen bajo `out/linux-winelib-debug/generated/`), así que no se adivinan, se leen del volcado. El patrón OK=1 que usa `OpusSdmDialog` en `File New` (línea ~4562) es el primer candidato a probar. Seleccionar en el listbox la entrada sembrada en Step 2 (por nombre, vía `LB_FINDSTRINGEXACT`/`LB_SETCURSEL` + texto, o el patrón que ya use el archivo para listboxes -- revisar cómo el modo `find_replace_mode` u otro maneja listboxes si existe precedente) y aceptar con OK. **No hace falta pulsar el botón "Setup..." (`tmcChgPrSetup`)** -- ver Architecture: la llamada a `ChgPr()` ocurre igual al cerrar con OK simple, `tmcChgPrSetup` sólo abre configuración avanzada del driver que este plan no necesita.

Si en cualquier punto aparece un diálogo de desajuste de página (`IdMessageBoxMstRgwMb`, ver riesgo documentado arriba) o el diálogo se cierra con `eidBadPrinter`/`eidNoPrinters` (`FCheckPrinter`/`FFillChgPrLb` fallando pese al Step 2), no descartarlo a ciegas -- capturar el diagnóstico y reportar `BLOCKED` con ese detalle en vez de forzar un dismiss o adivinar otro formato de valor.

- [ ] **Step 4: Enviar `kFilePrintPreview` y verificar activación**

El resto del bloque `print_preview_mode` (query 87, paginación, vuelta a modo edición) ya existe sin cambios -- sólo debe empezar a pasar ahora que `FPrinterOK()` es verdadero.

```bash
export DISPLAY=:91
cmake --build --preset linux-winelib-debug --target opus_word1_ui_test
ctest --test-dir out/linux-winelib-debug -R '^opus_word1_print_preview_test$' --output-on-failure
# repetir
```

(Ejecutar desde `/home/pablo/msword-teardown` -- worktree de esta plan -- o con `--test-dir` apuntando a su `out/linux-winelib-debug`, no al del checkout principal.)

- [ ] **Step 5: Label completo, sin regresiones, documentar**

```bash
ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
```

Actualizar `docs/port-linux/03-word1-startup-blocked-behavior.md` §14: de "Print Preview/printing has never been exercised end to end" a el resultado real obtenido (verde, o el punto exacto de bloqueo si terminó en `BLOCKED`), incluyendo la corrección del hallazgo de los dos almacenes de configuración (`win.ini` + `~/.config/OpusShell/Word1.ini`) para que quede registrado y no se repita la investigación.

```
feat(port): sembrar impresora sintetica y conducir Print Setup para activar lmPreview
```

---

## Self-Review Notes

- Task 1 tiene precedente exacto ya en el archivo (roundtrip_mode, rich_format_mode) -- no es una hipótesis, es alinear con un patrón que ya existe y ya pasa.
- Task 1 no puede verificarse "arreglado en debian13" desde esta máquina (hostname `vps`) -- el brief lo dice explícitamente para que el reporte final no sobre-reclame.
- Task 2 corrige el plan original del usuario: "declarar impresora en win.ini" por sí solo NO satisface `FPrinterOK()` (confirmado por la investigación ya existente en §14, `Opus/command2.c:1783` + `Opus/print2.c:1346`). El brief exige conducir el diálogo real, no sólo la config de Wine -- si un implementador intentara la vía "sólo win.ini", el spec la marca como incompleta.
- Task 2 explícitamente prohíbe tocar `Opus/print2.c`/`print1.c` -- la vía que sí lo requeriría (extender `OpusShellConfig` + migrar la enumeración `key=NULL`) queda fuera de este plan por la restricción del árbol; no se ofrece como alternativa dentro del loop de fixes.
- Ambas tasks acotadas a un archivo (`opus_word1_ui_test.cpp`) + un doc -- sin tocar CMakeLists.txt (el target de Task 2 ya está registrado en `a8ed4f3`).
