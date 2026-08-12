# Diagnóstico: corrupción de heap en el arranque de WORD1 (Fedora 44)

**Fecha:** 2026-08-12 · Fedora 44, GCC 16.1.1, wine-staging 11.0, `gdb` 17.2 y
`valgrind` 3.27.1 (instalados en esta sesión, con autorización explícita,
para esta tarea).

**Estado real: reproducido de forma consistente, con dos firmas de
corrupción distintas según la corrida; punto de crash simbolizado con
`addr2line` contra el propio binario; origen exacto de la corrupción NO
aislado — bloqueado por dos problemas de toolchain independientes del bug
en sí (ver más abajo). Diagnóstico puro: no se tocó ningún archivo de
código de `src/Opus/`, `src/OpusEtAl/` ni `src/port/` en esta tarea.**

Prerrequisito resuelto antes de empezar: el `HEAD` de esta sesión no
enlazaba `WORD1.exe.so` (faltaba `opus_shell_spine` en el link de `WORD1`,
hueco dejado por el commit `ea5f908` — ver
`01-frontera-nucleo-shell.md`, sección "Verificación cruzada en Fedora 44").
Se corrigió con una línea en `src/CMakeLists.txt` (agregar
`target_link_libraries(WORD1 PRIVATE opus_shell_spine)` más el bloque
`IMPORTED` correspondiente, mismo patrón que las otras 3 libs de núcleo) y
se reconstruyó. El binario diagnosticado aquí (`bin/WORD1.exe.so`,
mtime 2026-08-11 23:57 CLT) corresponde al `HEAD` real de esta sesión, no a
una copia vieja.

---

## 1. Reproducción — dos firmas de corrupción, no una

**Corrida bajo `gdb` (repetida 5 veces, mismo resultado en las 5):**

```
$ gdb -q --batch -ex "run" -ex "bt full" --args wine WORD1.exe.so
[...]
0024:fixme:dwmapi:DwmSetWindowAttribute (0000000000010086, 22, 000000000010789C, 4) stub
malloc(): invalid size (unsorted)

Program received signal SIGABRT, Aborted.
0x00007ffff7e0bccc in ?? ()
```

**Corrida directa (`wine WORD1.exe.so`, sin depurador, repetida 2 veces):**

```
$ wine WORD1.exe.so
[...]
0024:fixme:dwmapi:DwmSetWindowAttribute (0000000000010086, 22, 000000000010789C, 4) stub
free(): invalid next size (normal)
0024:fixme:dbghelp:sparse_array_add re-adding an existing key
0024:err:msvcrt:_wassert (L"NULL != abbrev_entry",L"dlls/dbghelp/dwarf.c",465)
Assertion failed: NULL != abbrev_entry, file dlls/dbghelp/dwarf.c, line 465
```

**Lectura:** dos mensajes distintos de glibc (`malloc(): invalid size
(unsorted)` vs. `free(): invalid next size (normal)`) para la misma
secuencia de arranque, sin cambiar una sola línea entre corridas. Es la
firma característica de una corrupción de heap real (un `write`
fuera de rango daña metadatos de un chunk de `malloc`), no de un bug
determinista de lógica: el punto exacto donde el allocator *detecta* el
daño depende del estado interno del heap en ese momento (qué chunk
coalesce/split ocurre primero), que a su vez depende de timing y del
mecanismo de tracing activo (gdb ralentiza y serializa la ejecución,
valgrind more aún). Ambos son sí-o-sí sobre el mismo hecho: **hay memoria
de heap escrita fuera de los límites de su bloque antes de que `malloc`/
`free` lo note.**

En todos los casos el punto de arranque hasta el fallo es idéntico:
`loader_init` de wine-staging, un stub de `DwmSetWindowAttribute`, y
entonces la corrupción. Consistente y reproducible, no intermitente en el
sentido de "a veces no pasa" — siempre pasa, solo cambia *cómo* se anuncia.

---

## 2. `gdb`: la traza no es utilizable — causa aislada, no es el bug

`bt full` después del `SIGABRT` no da una pila real:

```
#0  0x00007ffff7e0bccc in ?? ()
#1  0x2074616820646572 in ?? ()
#2  0x6d65732074786574 in ?? ()
#3  0x0000000000000000 in ?? ()
```

Los frames `#1`/`#2` son basura (se leen como texto ASCII arbitrario si se
decodifican los bytes — no son direcciones de retorno reales). Investigado
a fondo antes de descartarlo como ruido:

- `info symbol $pc` → `No symbol matches $pc`, **a pesar de** que
  `info proc mappings` confirma que `$pc` (`0x7ffff7e0bccc`) cae dentro del
  rango cargado de `/usr/lib64/libc.so.6` (`0x7ffff7d97000`-`0x7ffff7f88000`).
- `info sharedlibrary` devuelve **vacío** — ninguna biblioteca compartida
  registrada, ni siquiera `libc.so.6` o `ntdll.so`, pese a estar mapeadas.
- Se descargaron símbolos de depuración reales vía `debuginfod` (`set
  debuginfod enabled on`): 6.75 MB de `libc.so.6`, 2.76 MB de `ntdll.so`,
  más `libunwind`. No cambió nada — el problema no es falta de símbolos,
  es que gdb nunca asoció ningún objeto de biblioteca a esas direcciones.
- Se forzó un rescaneo con el comando `sharedlibrary` después de la señal:
  mismo resultado, vacío.

**Causa identificada, no solo sospechada:** `wine-preloader` (el binario
real que `wine` ejecuta primero — visible en la traza como `process NNNNN
is executing new program: /usr/lib64/wine-wow64/wine/x86_64-unix/
wine-preloader`) es un cargador ELF propio de Wine que mapea manualmente
`ntdll.so`, las bibliotecas del sistema y a sí mismo **sin pasar por el
protocolo estándar de `_dl_debug_state`/`r_debug` de glibc** — reserva a
mano el espacio de direcciones bajo para compatibilidad de layout Win32
antes de que corra ningún cargador dinámico normal. `gdb` (a través de
`solib-svr4.c`) depende exactamente de ese protocolo para poblar `info
sharedlibrary`. Como nunca se activa de la forma que `gdb` espera, la
resolución de símbolos para *todo el proceso* (no solo `WORD1.exe.so`)
queda rota, independientemente de si hay símbolos de depuración
disponibles. **Es una limitación conocida de depurar binarios Winelib con
`gdb` vainilla, no un defecto de este build ni de esta tarea** — la vía
recomendada por el propio proyecto Wine es `winedbg --gdb`, probada en la
sección siguiente.

---

## 3. `winedbg --gdb`: bloqueado por un bug de Wine, no del build

`winedbg` es el puente diseñado para este caso exacto (entiende el layout
PE + ELF de Wine, y expone un proxy compatible con `gdb`). Falla antes de
llegar a ejecutar nada:

```
$ printf 'c\nbt 20\n...' | winedbg --gdb "$(pwd)/WORD1.exe.so"
WineDbg starting on pid 0130
0130:0134: create process 'Z:\home\exia\word1\msword\bin\WORD1.exe'/0000000000000000 @00007F4E81713670 (0<0>)
012c:fixme:dbghelp:sparse_array_add re-adding an existing key
012c:err:msvcrt:_wassert (L"NULL != abbrev_entry",L"dlls/dbghelp/dwarf.c",465)
Assertion failed: NULL != abbrev_entry, file dlls/dbghelp/dwarf.c, line 465
```

**El propio `dbghelp` de Wine se cae** intentando leer el DWARF de
`WORD1.exe.so` — antes incluso de llegar al `run`/`continue`. Mismo
`assert` que ya aparece en la corrida directa sin depurador (§1): no es un
efecto secundario de `winedbg`, es `dbghelp.dll` (la misma biblioteca que
usa **el propio probe de arranque del proyecto**,
`port/original/opus_original_startup_probe.cpp`, vía `SymInitialize`/
`SymFromAddr`/`StackWalk64`) fallando al parsear el DWARF que GCC 16
genera para este binario.

**Esto explica, con evidencia y no por sospecha, un hallazgo colateral
importante:** por qué `WORD1-crash.txt` (el log que el propio probe del
proyecto escribe en cada arranque, ver `build/WORD1-crash.txt`) **nunca
tiene nombres de función ni líneas — solo `WORD1+0xoffset` crudo.** No es
una limitación del probe ni de `dbghelp` en general: es que `dbghelp.dll`
de Wine no puede parsear el formato de DWARF que emite GCC 16 para este
binario, así que cualquier intento de simbolizar en tiempo de ejecución
(el probe, `winedbg`) queda ciego. `addr2line` (offline, fuera de Wine,
§5) sí puede leerlo — es un problema del parser de Wine, no del contenido
del DWARF.

No investigado más a fondo (fuera de alcance de esta tarea: es un bug de
Wine, no del port). Reportable aguas arriba si en algún momento se
necesita que el probe simbolice en vivo.

---

## 4. `valgrind`: dos intentos, ninguno da señal utilizable

**Sin `--trace-children`:** valgrind solo instrumenta el proceso `wine`
supervisor, que hace `exec` hacia el binario real sin que valgrind lo
siga — cero errores reportados porque nunca llegó a ver el proceso que
realmente corrompe memoria:

```
$ valgrind --error-exitcode=99 --track-origins=yes wine WORD1.exe.so
==47068== Memcheck, a memory error detector
==47068== Command: wine WORD1.exe.so
==47068== Parent PID: 47067
[proceso termina, exit=0, sin errores -- porque no rastreó el proceso hijo]
```

**Con `--trace-children=yes`:** rompe la reserva de espacio de
direcciones que `wine-preloader` necesita hacer para el layout Win32,
antes de llegar siquiera a cargar `WORD1.exe.so`:

```
$ valgrind --trace-children=yes --track-origins=yes wine WORD1.exe.so
preloader: Warning: failed to reserve range 0000000000110000-0000000068000000
wine: dlls/ntdll/unix/virtual.c:3704: virtual_init: Assertion `view_block_start != MAP_FAILED' failed.
```

Es una incompatibilidad conocida entre el gestor de memoria propio de
Valgrind y la reserva de bajo-espacio-de-direcciones de `wine-preloader`
para procesos de 64 bits — no específica de este build. No se probó más
flags de Valgrind (`--soname-synonyms`, reservas manuales) por estar fuera
del alcance de "diagnóstico, sin arreglar nada" de esta tarea: son ajustes
de entorno de Wine/Valgrind, no del bug.

---

## 5. Lo que sí funcionó: `addr2line` directo contra el DWARF del binario

Sin pasar por el loader de Wine ni por `dbghelp`, leyendo el ELF
`WORD1.exe.so` (no stripped, con `debug_info`) directamente:

```
$ addr2line -e WORD1.exe.so -f -C -i 0x1FD57C
N_FormatLineDxa
/home/exia/word1/msword/src/port/original/opus_asm_resn2_adapters.cpp:185
```

El log del propio probe (`build/WORD1-crash.txt`, escrito en esta sesión,
arranque real bajo `wine` sin depurador) da:

```
Exception 0xC0000005 at 0x00006FFFFFC1B75F
Access violation: write at 0x0000000000000000
unwind #0 0x00006FFFFFC1B75F rsp=0x105DE0
unwind #1 WORD1+0x1FD57C rsp=0x105DE8
unwind #2 0x00006FFFFFC0EE53 rsp=0x105DF0
stack+0x0: WORD1+0x1FD57C
[...]
```

Los tres `unwind #N` son los únicos frames que `StackWalk64` (usado por el
probe) pudo verificar de verdad; el resto (`stack+0xNN`) es un barrido
heurístico de la pila en busca de valores que caigan dentro del rango de
`WORD1` — útil como pista, no como prueba de una secuencia de llamadas
real.

**Frame verificado #1, simbolizado — el hallazgo concreto de esta
tarea:**

```
WORD1+0x1FD57C → N_FormatLineDxa
  src/port/original/opus_asm_resn2_adapters.cpp:185-188:
      int N_FormatLineDxa(int ww, int doc, long cp, int dxa) {
          C_FormatLineDxa(ww, doc, cp, dxa);
          return 0;
      }
```

Es un adaptador nativo de una sola línea que llama directo a
`C_FormatLineDxa` — el motor real de formateo de línea (paginación, el
mismo subsistema que documenta B1.3/B2 de `01-frontera-nucleo-shell.md`).

**Frame #0** (el punto exacto del fallo, `write` a `0x0`) es la dirección
cruda `0x00006FFFFFC1B75F` — **no cae dentro del rango de `WORD1`** (no
tiene prefijo `WORD1+`), así que es código de algún otro módulo cargado
(muy probablemente una DLL de Wine — `user32`/`gdi32`/`ntdll`, dado el
rango de dirección). No se pudo simbolizar en esta tarea: `addr2line`
solo tiene el DWARF de `WORD1.exe.so`, no el de las `.so` de Wine, y
`winedbg`/`dbghelp` (que sí sabría resolver eso) está roto (§3).

**Lectura de conjunto, con cautela explícita sobre qué es evidencia dura y
qué es lectura razonable:** `N_FormatLineDxa` llama a `C_FormatLineDxa`, y
el fallo ocurre *dentro de* esa llamada, en código fuera de `WORD1` —
consistente con (no probado como) una llamada a través de un puntero de
función dañado, o una escritura a través de un puntero ya liberado/movido
que apunta a memoria fuera del proceso o a una página no mapeada en `0x0`.
Encaja con el patrón de la arquitectura documentada en
`docs/port-linux/00-reconocimiento.md` §1.7: el motor resuelve comandos
dinámicamente vía `GetProcAddress` sobre tablas generadas por MKCMD, así
que un puntero de función corrupto en esa zona (tabla de despacho, no la
pila) produciría exactamente esta forma de fallo. **Esto es una lectura
razonada, no una prueba** — no se aisló el `write` que corrompe el heap en
sí, solo su consecuencia unos pasos después.

**Rastro heurístico** (mismo método, offsets de `stack+0xNN`, no
verificados por `StackWalk64` — tratar como pista de la zona del código,
no como secuencia de llamadas confirmada):

| Offset | Símbolo | Ubicación |
|---|---|---|
| `0x4C727B`, `0x457EFD`, `0x3AC0D8` | *(sin símbolo — código externo a `WORD1`, no resuelto)* | — |
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
| `0x2DE5DA`, `0x2DE881` | `InvokeValue<...>` (plantilla) | `opus_elx_dispatch.cpp:34`, `:27` |

Leído de abajo hacia arriba (offsets de pila más altos = llamadas más
externas si el barrido heurístico reflejara la pila real, que no está
garantizado): el patrón visible —despacho de macro/ELX vía plantillas
`InvokeValue`, pasando por `ExecEndProc`, `CmdUndo`, rutinas de
diccionario ortográfico, configuración de menús Win95, y terminando en
formateo de línea— es compatible con una secuencia de arranque real
(inicialización de menús → diccionario → deshacer → macro nativa →
formateo), pero **de nuevo: es un barrido de coincidencias de texto en la
pila, no una pila confirmada.** Se reporta como pista para acotar dónde
mirar, no como el camino de ejecución probado.

---

## 6. Origen de la corrupción: no aislado en esta tarea

**No se identificó la instrucción exacta que escribe fuera de rango.** Lo
que sí queda establecido con evidencia real:

1. Hay corrupción de heap real y reproducible (glibc lo detecta, con dos
   mensajes distintos según la corrida — §1).
2. El punto donde el daño se manifiesta como violación de acceso (no el
   origen) está en código externo a `WORD1`, alcanzado desde
   `N_FormatLineDxa`/`C_FormatLineDxa` (§5) — un `write` a `0x0`.
3. Las dos vías estándar para ir más allá (`gdb`, `valgrind`) están
   bloqueadas por incompatibilidades de entorno/toolchain **independientes
   del bug**: el cargador propio de Wine rompe el rastreo de símbolos de
   `gdb` (§2), y `dbghelp`/`winedbg` no pueden parsear el DWARF de GCC 16
   en este binario (§3), y `valgrind --trace-children` choca con la
   reserva de memoria de `wine-preloader` (§4).

## 7. Próximos pasos sugeridos — no implementados, a la espera de decisión

- **ASan.** Es el candidato más directo para aislar el `write` real: un
  build con `-fsanitize=address` detectaría la escritura fuera de rango
  en el momento exacto, no varios pasos después como ahora. Requiere
  reconfigurar el build (`CMAKE_C_FLAGS`/`CMAKE_CXX_FLAGS` para el target
  `WORD1`/`opus_original_engine`, o un preset nuevo) — **no se aplicó en
  esta tarea**, según lo pactado; se propone y se espera confirmación.
  Riesgo conocido de antemano: ASan y el propio manejo de excepciones/SEH
  de Wine no siempre conviven bien (el manejador de señales de ASan puede
  chocar con el de Wine) — habría que probarlo y estar preparado para que
  dé una señal distinta a la esperada, no asumir que "simplemente
  funciona".
- **Symbolizar frame #0.** Encontrar qué módulo de Wine cubre el rango de
  `0x00006FFFFFC1B75F` (vía `addr2line`/`nm` contra las `.so` de Wine
  instaladas, ya que `winedbg` no sirve) acotaría si el salto es a
  `user32`/`gdi32`/`ntdll`, lo que ayudaría a decidir si el puntero
  corrupto es una tabla de comandos (§1.7 de `00-reconocimiento.md`), un
  callback de ventana, o algo distinto.
- **Revisar `C_FormatLineDxa` y su vecindario en `Opus/wordtech/`** a mano
  (sin herramientas dinámicas) buscando escrituras con índice/tamaño no
  acotado alrededor de estructuras que dependan de `long`/`DWORD` de 8
  bytes bajo LP64 — el mismo patrón de bug que ya causó daño real y
  confirmado en BITAPP (`00-reconocimiento.md`, Fase 1, `bitapp.h:29`).
  No se hizo en esta tarea por ser ya análisis de causa, no diagnóstico de
  síntoma.
- **Reportar el assert de `dbghelp.dll` aguas arriba a Wine**, si el
  proyecto llega a depender de que el probe propio simbolice en vivo —
  hoy es un bloqueador de tooling, no del port.
