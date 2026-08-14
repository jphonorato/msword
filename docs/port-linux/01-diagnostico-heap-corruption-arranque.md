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

**MATIZADO — ver §12 (punto 2).** El síntoma central de esta sección
(`info sharedlibrary` vacío) tiene una explicación alternativa suficiente
—leerlo después de que el inferior ya salió— confirmada en un entorno sin
`wine-preloader`. La atribución de causa a `wine-preloader` hecha aquí (en
Fedora 44/wine-staging 11.0, donde ese binario sí existe) no fue puesta a
prueba de nuevo ni reprobada en esta sesión: sigue siendo la explicación
más probable *en ese entorno específico*, pero ya no es la única
explicación posible para el síntoma observado.

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

**MATIZADO — ver §12.** No verificado ni reproducido de nuevo en esta
sesión: el entorno usado aquí (Debian 13/wine-10.0/GCC 14.2.0) no es el de
esta sección (Fedora 44/wine-staging 11.0/GCC 16.1.1), y `winedbg` no se
probó. Se deja tal cual, sin refutar ni confirmar.

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

---

**MATIZADO — ver §12 (punto 4).** El resultado de ASan en sí (pasos 1-2,
Fedora, con `wine-preloader` real presente) no se puso en duda. Lo que se
matiza es la generalización de cierre ("tercera de tres herramientas
dinámicas bloqueada por la misma fricción"): un cuarto instrumento
dinámico (el checker de heap de glibc) sí se mapea y sí corre bajo Wine en
un entorno sin `wine-preloader` — con el matiz de que su valor
diagnóstico para este bug específico es limitado (ver §12).

## 8. ASan — descartado, no solo propuesto (2026-08-12)

Se probó el candidato de §7 en aislamiento (binario Winelib trivial en
`/tmp`, sin tocar `out/linux-winelib-*` ni ningún archivo de código) antes
de invertir en reconfigurar el build real.

**Paso 1 — `winegcc` no propaga `-fsanitize=address` al link final.** Su
driver arma el comando de link él mismo y descarta la flag: el `.o` se
compila instrumentado, pero el link final (`gcc -m64 -shared ... -ldl -lm
-lc`, sin `-fsanitize=address` ni `-lasan`) deja `__asan_init`/
`__asan_version_mismatch_check_v8` sin resolver (`ld: undefined
reference`, `winegcc: /usr/bin/gcc failed`, código de salida 2).
Confirmado con `winegcc -v` mostrando el comando de link real emitido.
Se resuelve pasando `-lasan` explícito (`-Wl,--no-as-needed -lasan
-lpthread -ldl -lrt -lm`): el binario linkea y produce `t.exe`/`t.exe.so`.

**Paso 2 — corre bajo `wine` con `LD_PRELOAD=libasan.so`, y falla en la
inicialización del propio ASan, antes de llegar a `main`:**

```
==PID==ERROR: AddressSanitizer failed to deallocate 0xc800 (51200) bytes
at address 0x00007ffb3800 (error code: 22)
```

Se probaron cuatro combinaciones de `ASAN_OPTIONS`
(`allocator_may_return_null=1`, `protect_shadow_gap=0`,
`verify_asan_link_order=0`, y las cuatro juntas) más
`WINEPRELOADRESERVE=0x0-0x0` (intento de desactivar la reserva de
bajo-espacio-de-direcciones de `wine-preloader`) — mismo error en los
cinco casos, sin cambio en el mensaje ni en el código de retorno.

**Lectura:** es la misma clase de incompatibilidad que ya bloqueaba
`valgrind --trace-children` en §4 — un gestor de memoria externo que
reserva/desmapea su propio espacio de direcciones choca con la reserva
que `wine-preloader` hace por adelantado para el layout Win32, antes de
que corra ningún constructor de biblioteca. No es un bug de ASan ni de
este build: es la tercera herramienta dinámica de las tres intentadas
(`gdb`/`winedbg` en §2-3, `valgrind` en §4, ahora ASan) bloqueada por el
mismo tipo de fricción de toolchain Winelib/x86-64, y no por el bug en
sí. **Descartado como vía — no se reconfiguró el build real, no queda
como "pendiente de intentar".**

## 9. Revisión manual de `C_FormatLineDxa` — arrancada, sin causa raíz aislada aún

Primer tramo del último punto de §7 ("revisar `C_FormatLineDxa` y su
vecindario a mano"). Alcance cubierto en esta sesión, con evidencia:

- **Los macros `CwFromCch`/`FChngSizeHCw`/`HAllocateCw` que gobiernan el
  buffer `vhgrpchr` (la tabla de runs `CHR`/`CHRV`/`CHRT`/... que
  `C_FormatLineDxa` llena) se verificaron consistentes, no son el bug.**
  `CwFromCch(cch)` (`wordtech/word.h:196`) usa `sizeof(int)` — 4 bytes
  tanto en Win16 original (`int` de 2 bytes → la unidad "cw" real ahí es
  de 2 bytes) como en este build GCC x64 (`int` de 4 bytes → unidad "cw"
  de 4 bytes). El macro original `heap.h:84`
  (`FChngSizeHCw(h,cw,f) = FChngSizeHCb(h,(cw)<<1,f)`, asume unidad de 2
  bytes) **no se usa en este build**: `heap.h:3-5` toma la rama
  `#ifdef OPUS_X64` y solo incluye `opus_x64_heap.h`, cuya versión
  (`FChngSizeHCw(h,cw,shrink) = OpusFChngSizeHCb(h, cw*sizeof(int),
  shrink)`) multiplica por 4, no por 2 — coherente con la nueva unidad de
  `CwFromCch` bajo `int` de 4 bytes. Sin mezcla de las dos definiciones
  confirmada por lectura directa de `heap.h`. El patrón de crecimiento en
  `FExpandGrpchr` (`fetch1.c:838-853`, +25% con reserva de `cbCHRE`) y su
  único call site relevante también se leyeron completos — sin señal de
  desajuste de tamaño.
- `cbCHR`/`cbCHRT`/`cbCHRV`/`cbCHRDF`/`cbCHRFG` (`wordtech/format.h`) son
  todos `sizeof(struct ...)` calculados por el propio compilador — a
  diferencia del bug ya confirmado de `bitapp.h:29` (`DWORD` fijo
  serializado contra un formato de archivo externo), esta tabla es
  interna y autoconsistente: no hay una constante de formato fija de por
  medio que pueda desalinearse bajo LP64. Se descarta como clase de bug
  para este buffer específico.
- Los ~15 puntos donde `vbchrMac`/`bchrPrev`/`ffs.bchr` se comparan
  contra `vbchrMax` antes de indexar `(**vhgrpchr)[...]` (grep completo
  de `format.c`) están todos guardados (`if ((vbchrMac += cbCHR...) <=
  vbchrMax ...)` o equivalente) — no se encontró un guard faltante en
  esta pasada, pero **no se verificó cada rama a mano línea por línea**,
  solo el patrón general del guard.

**No cubierto todavía — sigue pendiente:** el cuerpo completo de
`C_FormatLineDxa` (`wordtech/format.c:454-1206+`, la función tiene más de
600 líneas) no se leyó entero; el tramo revisado fue el de inicialización
(454-750) y el subsistema de asignación del buffer de runs. Falta el
cuerpo principal del bucle de formateo de línea (medición de ancho de
carácter, tabs, campos/fórmulas, breaks) donde vive la llamada real a
GDI/medición de texto que el crash de §5 alcanza fuera de `WORD1`.

**Pista nueva, no perseguida aún, que cambia el ángulo de búsqueda:**
esta rama del proyecto viene wireando progresivamente `WORD1` a los
contratos `OpusShell*` (commits recientes: `00de60f2` conecta
`C_LoadFcid` — medición de ancho de carácter en pantalla — a
`OpusShellCharWidths`; `b803cc0` enlaza `opus_shell_font_metrics` en
`WORD1`). Si el bucle de `C_FormatLineDxa` todavía llama a GDI real
(`vpri.hdc`/`vfti`, HDC de pantalla) para algo que `C_LoadFcid` ya
resuelve por el contrato Qt en otro punto del mismo flujo de formateo,
un HDC nulo o un estado de fuente inconsistente entre las dos vías
encajaría con "write a 0x0 en código fuera de `WORD1`" (§5) sin requerir
corrupción de heap previa como explicación única. **No confirmado — es
la siguiente hipótesis a probar, no un hallazgo.** `vpri.hdc` en sí
resultó ser solo el HDC de impresora (`disp1.c`, `print.c`,
`initwin.c`) por lectura de sus asignaciones — no es el HDC de pantalla
que usaría el bucle de formateo, así que la hipótesis necesita
identificar primero cuál variable sí lo es antes de poder probarse.

## 10. Hipótesis del HDC en el bucle de formateo — descartada para la ruta de ancho de carácter; corrección de alcance de la función (2026-08-12)

Continuación de §9. Lectura directa (no gdb, no ejecución — solo lectura de
código fuente) de `wordtech/format.c` y de `LOADFONT.C` completo.

**Corrección de alcance importante para la siguiente sesión:** §9 estimaba
`C_FormatLineDxa` en "600+ líneas" (454-1206+). Es mucho más grande: la
función real corre `format.c:454` hasta el `return; }` en `format.c:2604`
(cierre de `#ifdef DEBUGORNOTWIN` en 2606) — **~2150 líneas**, no ~750. Los
targets `LEndJ:` (línea 2378) y `LEnd:` (línea 2481) están mucho más abajo
de lo que el rango citado en §9 sugería.

**Hallazgo 1 — la ruta de medición de ancho de carácter del bucle principal
NO llama a GDI ni toca ningún HDC:**

`grep` de `GetTextExtent*|GetCharWidth|SelectObject|hdc|HDC|GetDC` sobre
todo `wordtech/format.c` no tiene coincidencias. El cálculo de ancho en el
bucle caliente (`format.c:900-907`, y repetido en `~15` puntos más
mapeados por grep de `DxpFromCh` en el archivo) es:

```c
dxt = dxp = DxpFromCh(ch, &vfti);
```

`C_DxpFromCh` (`format.c:2912-2917`) es una función pura:

```c
HANDNATIVE int C_DxpFromCh( ch, pfti )
    int ch;
    struct FTI *pfti;
    {
    return pfti->rgdxp [ch] + pfti->dxpExpanded;
    }
```

Solo indexa el array `rgdxp[256]` de `struct FTI` (`Opus/fontwin.h:157`),
ya poblado antes de entrar al bucle. **No hay ningún HDC vivo en el
tramo 900-1259 de `format.c`** (rango efectivamente revisado línea por
línea en esta sesión) — la hipótesis de §9 ("el bucle sigue llamando a GDI
real con un HDC inconsistente") queda **descartada para esta ruta
específica**: no hay llamada GDI que hacer inconsistente, es una lectura de
tabla.

**Hallazgo 2 — el HDC de pantalla real vive en `LOADFONT.C`, no en
`format.c`, y sigue siendo necesario incluso en la ruta ya wireada a Qt:**

`vfti`/`vftiDxt` (los `struct FTI` que `format.c` lee) se llenan en
`C_LoadFcid` (`Opus/LOADFONT.C:198-608`), no en `format.c`. El HDC de
pantalla real es `vsci.hdcScratch` (o el `hdc` de cada ventana,
`(*hwwd)->hdc`), asignado dentro de `FSelectFont`
(`LOADFONT.C:663-813`, rama pantalla en 709-808) y consumido por
`C_LoadFcid` en `GetTextMetrics(hdc, ...)` (`LOADFONT.C:349`, justo después
de la etiqueta `LNewMetrics:`).

Punto clave: el guard `OPUS_X64`/`__GNUC__` que desvía la tabla de anchos
variable-pitch a `OpusShellCharWidths` (`LOADFONT.C:428-467`, comentado
como wireado por `00de60f2`) es **posterior** a `FSelectFont` (línea 345) y
a `GetTextMetrics` (línea 349) — es decir, **incluso en la ruta ya
conectada al contrato Qt, `C_LoadFcid` sigue llamando a GDI real
(`FSelectFont` + `GetTextMetrics`) sobre `vsci.hdcScratch` para
`dypAscent`/`dypDescent`/`dxpOverhang` antes de decidir si la tabla de
anchos viene de GDI o de `OpusShellCharWidths`.** Esto ya estaba anotado
como pregunta abierta #3 en `docs/port-qt/01-frontera-nucleo-shell.md`
(referenciada en el prompt de esta tarea); esta sesión no la resuelve,
solo confirma por lectura directa que el mecanismo es exactamente ese
(`GetTextMetrics` incondicional en `LNewMetrics`, línea 349) y que no hay
una ruta paralela en `format.c` que la esquive.

**Hipótesis del HDC — estado revisado:** descartada para el bucle de
`C_FormatLineDxa` en sí (no hay HDC ahí); **sigue abierta, sin
confirmar**, para `vsci.hdcScratch` dentro de `C_LoadFcid`/`FSelectFont` —
si ese HDC queda nulo o stale frente al arranque Qt, el crash ocurriría
dentro de `LOADFONT.C`/`disp*.c` (donde se inicializa `vsci.hdcScratch`),
no dentro de `format.c`. No se leyó en esta sesión el código de
inicialización de `vsci.hdcScratch` (candidato: `disp1.c`/`initwin.c`, sin
confirmar) — es el siguiente paso natural, no `format.c`.

**Tramo de `C_FormatLineDxa` cubierto en esta sesión (lectura completa,
línea por línea):** `format.c:454-1259` (inicialización + bucle principal
hasta el manejo de `chSpace`/`LBreakOppR`) y `format.c:2560-2605` (cola de
la función: cálculo de `dypAfter`, ajuste `fPageView`, cierre de
`grpchr`). También se leyó completo `Opus/LOADFONT.C` (1064 líneas).

**No cubierto — pendiente para la siguiente sesión:**
`format.c:1259-2560` (~1300 líneas sin leer): manejo de tabs
(`case chTab` y vecindario, no localizado aún), campos/fórmulas
(`chrmFormula`/`chrmDisplayField`/`chrmFormatGroup`, mencionados en
`format.h` pero su tratamiento en el bucle no revisado), `LEndJ:` (2378,
lógica de justificación) y `LEnd:` (2481, cierre de línea antes de la cola
ya revisada en 2560-2605). Este es el tramo con mayor probabilidad
restante de contener la causa, dado que §9 ya descartó el buffer de runs y
esta sesión descartó la ruta de ancho de carácter simple.

**No se ejecutó gdb en esta sesión** — todo lo anterior es lectura de
código fuente, no verificación en vivo. Cualquier confirmación de "HDC
nulo" en `vsci.hdcScratch` requiere breakpoint en `FSelectFont`
(`LOADFONT.C:709`) o `GetTextMetrics` (`LOADFONT.C:349`) durante el
arranque real de `WORD1`, no hecho todavía.

**REFUTADO — ver §12 (puntos 1-2).** La premisa de §11.3 ("`gdb` vainilla
no puede resolver breakpoints por archivo:línea en `WORD1.exe.so`") es
falsa. Causa real: los 18 archivos case-shimmed (`LOADFONT.C` entre ellos)
registran su compilation unit DWARF con el path del symlink en
minúsculas; `break LOADFONT.C:349` nunca podía resolver por eso, no por
ninguna limitación de `gdb`/Wine. `break loadfont.c:349` resuelve y
dispara en el primer intento, con backtrace simbólico completo hasta
`C_FormatLineDxa`. El "resultado neto" de §11.4 y el "camino recomendado"
de 3 pasos quedan obsoletos — ver Promoted Plan en la revisión de esta
sesión y §12.

## 11. Verificación en vivo intentada — bloqueada por dos motivos distintos, ninguno confirma ni descarta la hipótesis (2026-08-12)

Continuación directa de §10. Entorno **distinto** al de §1-§9: sandbox
nuevo (Debian 13 "trixie", GCC 14.2.0, `wine-10.0` Debian repack,
`gdb` 16.3), no Fedora 44/GCC 16.1.1/wine-staging 11.0. Sin binario
`WORD1.exe.so` preexistente — se reconstruyó desde cero. Esta diferencia
de entorno termina siendo el hallazgo principal de la sesión (ver más
abajo), no un detalle incidental.

### 11.1 Bloqueadores de build encontrados y resueltos (entorno, no `src/Opus/`)

Tres problemas de toolchain nuevos, independientes del bug investigado,
bloquearon incluso producir un binario:

1. **`wrc: Error: codepage 1252 not supported`** al compilar
   `port/word1.rc`. Causa: el `wrc` de este sistema (Wine Resource
   Compiler 10.0) no encuentra sus tablas NLS (`/usr/share/wine/nls/`)
   sin `--nls-dir` explícito, aunque el archivo `c_1252.nls` sí existe en
   disco. **Corregido** agregando `--nls-dir=/usr/share/wine/nls` al
   `add_custom_command` de `wrc` en `src/CMakeLists.txt` (línea ~1370).
2. **`GetCurrentThreadStackLimits was not declared in this scope`** al
   compilar `port/original/opus_original_startup_probe.cpp:249`. Causa:
   `processthreadsapi.h` de este paquete Wine 10.0 no declara esa función
   (exportada por `kernel32` desde Vista+; sí presente en el Wine
   11.0/Fedora usado en §1-§9). **Corregido** con una declaración local
   `extern "C" WINBASEAPI` guardada por `#if !defined(_MSC_VER)`,
   comentada in situ explicando la causa — no toca `src/Opus/` ni
   `src/OpusEtAl/`, está en `src/port/`.
3. **`opus_word1_ui_test.cpp:379`: no matching function for call to
   `min(LONG, long int)`** al intentar compilar `opus_word1_ui_test`
   (necesario para el plan original de disparar `C_FormatLineDxa` vía
   `--typing`/`--font-typing` en vez de solo lanzar `WORD1` en vacío).
   Causa: mismatch de tipo `LONG` (`int` en este `windows.h` de Wine
   10.0) contra el literal `20L` (`long`). **No corregido** — fuera del
   camino finalmente usado (ver 11.3); si una sesión futura necesita este
   harness en este mismo tipo de entorno, el fix es un cast explícito en
   `opus_word1_ui_test.cpp:379-380`, no tocado aquí.

Ninguno de los tres es el bug que se investiga — son huecos de
portabilidad entre versiones de Wine/GCC en el propio árbol de soporte
(`src/port/`, `src/CMakeLists.txt`), no en `src/Opus/`. **No se hizo
commit de estos cambios** — quedan en el árbol de trabajo, pendientes de
que el mantenedor decida si conservarlos (necesarios para reproducir en
cualquier entorno Debian/wine-10.0) o revertirlos.

### 11.2 El crash de §1 no se reproduce en este entorno

Con el binario ya compilado (`bin/WORD1.exe.so`, no stripped, con
`debug_info` — confirmado con `file`), dos corridas directas bajo Xvfb
(`DISPLAY=:99 timeout {30,60} /usr/lib/wine/wine64 WORD1.exe.so`, display
real, no el camino `nodrv` que se ve al no exportar `DISPLAY`):

```
$ DISPLAY=:99 timeout 60 /usr/lib/wine/wine64 WORD1.exe.so
04b0:fixme:dwmapi:DwmSetWindowAttribute (0000000000060088, 22, 00007FFFFE1F788C, 4) stub
[fin de la salida -- timeout mata el proceso a los 60s, exit 124, sin crash]
```

Esta es **exactamente la misma línea** (`fixme:dwmapi:DwmSetWindowAttribute`)
que en §1 aparece inmediatamente antes de `malloc(): invalid size
(unsorted)` / `free(): invalid next size (normal)`. Aquí, en cambio, el
proceso queda vivo e inactivo (confirmado con `ps aux`, `WORD1.exe.so`
listado, consumiendo CPU mínima) indefinidamente — probado hasta 60s sin
señal de corrupción, sin más salida.

**Lectura, no confirmada más allá de esto:** el crash de §1 no es
determinista entre entornos Wine 11.0/Fedora44/GCC16.1.1 y
Wine 10.0/Debian13/GCC14.2.0. No se investigó cuál de las tres variables
(versión de Wine, versión de GCC, o alguna diferencia de `wine.conf`/DPI/
tema entre las dos instalaciones) es la causante — está fuera del alcance
de esta tarea puntual. **Consecuencia práctica para la siguiente sesión:**
reproducir el crash real para seguir diagnosticándolo con `gdb` requiere
un entorno que iguale Fedora 44/wine-staging 11.0/GCC 16.1.1, no este
sandbox Debian.

### 11.3 Los breakpoints por archivo:línea nunca se resuelven — mismo mecanismo que §2, ahora confirmado también para breakpoints

Se intentó de todas formas (el objetivo era ver `vsci.hdcScratch` en
tránsito, no solo esperar el crash). Con `set breakpoint pending on` y
`break LOADFONT.C:349` / `break LOADFONT.C:709` antes de `run`:

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

(Esa corrida en particular falló por un error de path relativo del lado
de Wine —`ShellExecuteEx`— nada que ver con el bug; se repitió con cwd
correcto y el proceso corrió normalmente hasta el mismo punto idle de
§11.2.) **Lo que importa de esta salida:** después de que el proceso
corrió (aunque terminara por error ajeno), `info sharedlibrary` solo
lista `ld-linux-x86-64.so.2` — ni `WORD1.exe.so`, ni `ntdll.so`, ni
`libwine`, nada. El breakpoint 1 queda `<PENDING>` para siempre. Repetido
con `DISPLAY=:99` y el proceso corriendo normalmente 25s sin crashear:
mismo resultado, `run` nunca retorna control a gdb porque el breakpoint
nunca se dispara (no puede dispararse: `WORD1.exe.so` nunca se registra
como shared object).

**Esto confirma y extiende el hallazgo de §2:** no es solo que `gdb` no
pueda *simbolizar* una pila ya rota (§2) — tampoco puede *resolver
breakpoints por archivo:línea* dentro de `WORD1.exe.so` en absoluto,
porque nunca lo ve como shared object cargado (el mecanismo manual de
`wine-preloader` sigue sin pasar por `_dl_debug_state`/`r_debug`). Un
breakpoint por dirección absoluta (`break *0xDIRECCION`, con la dirección
sacada de `addr2line` como en §5) probablemente sí funcionaría —no se
probó en esta sesión por falta de tiempo, y de cualquier forma el crash
no se reprodujo aquí (§11.2) para tener una dirección de interés que
perseguir.

### 11.4 Resultado neto

**Ninguno de los dos breakpoints (`LOADFONT.C:349`, `LOADFONT.C:709`) se
disparó nunca** — no por falta de camino de ejecución (el arranque de
`WORD1` sí pasa por la carga de fuentes, es una ruta de arranque normal),
sino porque `gdb` vainilla no puede poner breakpoints por archivo:línea
en este binario, punto ya documentado en §2 y ahora confirmado que
también aplica a breakpoints, no solo a resolución de pilas. **La
hipótesis de `vsci.hdcScratch` nulo/stale sigue exactamente donde estaba
en §10: abierta, ni confirmada ni descartada.** No se obtuvo ningún
`print vsci.hdcScratch` ni `print hdc` real en esta sesión.

**Camino recomendado para la siguiente sesión** (no intentado aquí):
1. Reproducir en un entorno que iguale Fedora 44/wine-staging
   11.0/GCC 16.1.1 (§11.2) — este sandbox Debian/wine-10.0 no sirve para
   esto.
2. Usar `addr2line`/lectura estática (como en §5) para obtener la
   dirección absoluta de `LOADFONT.C:349` y `:709` en el binario
   reconstruido de ese entorno, y poner el breakpoint por dirección
   (`break *0x...`), no por archivo:línea — evita el problema de §11.3.
3. Alternativa sin `gdb`: instrumentar `LOADFONT.C` con un
   `fprintf(stderr, ...)` temporal imprimiendo `vsci.hdcScratch` y `hdc`
   en esos dos puntos — requiere autorización explícita para tocar
   `src/Opus/LOADFONT.C` (issue de GitHub primero, por la restricción del
   árbol), pero rodea tanto el bloqueador de §11.3 como la no-reproducción
   de §11.2 (el print se vería en cualquier corrida, crashee o no).

## 12. Refutaciones en vivo del "camino recomendado" de §11.4 (2026-08-12, misma sesión, entorno Debian 13/wine-10.0/gdb 16.3)

Continuación directa de §11, mismo binario (`bin/WORD1.exe.so`, sin
recompilar). Los tres pasos de §11.4 resultaron innecesarios: el paso 2
(`addr2line` + `break *0x`) resolvía un problema que no existía, y el
paso 3 (parchar `src/Opus/LOADFONT.C`) da un dato que `gdb` ya da gratis.
Lo que sigue es la evidencia, comando por comando, sin resumir.

### 12.1 Breakpoints por archivo:línea sí resuelven — el bloqueador era el case del symlink

`src/CMakeLists.txt` (líneas ~944-964) symlinkea 18 fuentes —incluida
`LOADFONT.C`— a `generated/lowercase-c/*.c` y las compila desde ahí. El
DWARF registra el path en minúsculas. Reproducido dos veces con el mismo
binario, cambiando solo el case del breakpoint:

**Mayúsculas (como en §11.3) — nunca resuelve:**
```
$ gdb -q --batch -ex "set breakpoint pending on" -ex "break LOADFONT.C:349" -ex "run" ...
No symbol table is loaded.  Use the "file" command.
Breakpoint 1 (LOADFONT.C:349) pending.
[...]
0518:fixme:dwmapi:DwmSetWindowAttribute (0000000000080094, 22, 00007FFFFE1F788C, 4) stub
[el proceso queda idle -- run nunca retorna, breakpoint sigue <PENDING>]
```

**Minúsculas — dispara en el primer intento, backtrace completo:**
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

Corregido para futuras sesiones: usar minúsculas para las 18 fuentes
listadas en `src/CMakeLists.txt` (`CLIPBORD.C CLIPBRD2.C CMD3.C CREATE2.C
DDESRVR.C DLBENUM.C EDIT.C FIELDCMD.C FILE2.C GRSPEC.C LOADFONT.C PIC2.C
RTFIN.C RTFOUT.C RTFRARE.C SCREEN2.C SPELL.C SYSCHG.C`); cualquier otro
archivo de `src/Opus/` va con su nombre real.

Efecto colateral útil: la ruta caliente del arranque queda localizada sin
ambigüedad — `C_FormatLineDxa` (`format.c:2269`) → `N_LoadFont` →
`C_LoadFcid` (`loadfont.c:131/345`) → `FSelectFont` (`loadfont.c:711`).
`format.c:2269` cae dentro del tramo `1259-2560` que §10 marcó como de
mayor probabilidad restante y dejó sin leer — pasa de "probable" a
confirmado por ejecución real.

### 12.2 La lectura de `info sharedlibrary` en §11.3 estaba contaminada por timing, no por un bloqueo de `gdb`/Wine

El propio bloque ya citado en §11.3 muestra la contaminación: la lectura
se hizo **después** de `[Inferior 1 (process 134178) exited with code
01]`, y el listado resultante solo tiene `ld-linux-x86-64.so.2` — ni
siquiera `libc.so.6`, imposible en un proceso dinámico vivo. Ese dato por
sí solo ya invalidaba la lectura de §11.3, sin necesidad de ninguna otra
prueba.

Reproducido aquí de dos formas distintas, ambas contrastando con lo de
arriba:

**(a) Mismo síntoma reproducido fresco, con el proceso realmente colgado
en `run` (no hay `wine-preloader` en este entorno — ver 12.5 — así que
"contaminación por timing" y "wine-preloader rompe el protocolo" quedan
como dos explicaciones independientes para el mismo síntoma, no una sola):**
el script con `break LOADFONT.C:349` (mayúsculas) más `run` +
`info sharedlibrary` + `info breakpoints` nunca llega a ejecutar los dos
últimos comandos porque `run` no retorna (proceso queda idle, igual que
§11.2); el log se corta después de la línea `fixme:dwmapi`.

**(b) `info sharedlibrary` leído en el momento correcto (adjuntado a un
proceso ya corriendo, sin esperar a que salga) — WORD1.exe.so aparece con
`Syms Read = Yes`:**
```
$ gdb -q --batch -ex "info sharedlibrary" -ex "info line LOADFONT.C:349" \
    -ex "print vsci" -ex "print vsci.hdcScratch" -p <PID>
[...]
From                To                  Syms Read   Shared Object Library
0x00007f1c296b6400  0x00007f1c298188fd  Yes         /lib/x86_64-linux-gnu/libc.so.6
0x00007f1c29895000  0x00007f1c298bc3d1  Yes         /lib64/ld-linux-x86-64.so.2
0x00007f1c295d2c40  0x00007f1c296321a0  Yes (*)     /usr/lib/wine/../x86_64-linux-gnu/wine/x86_64-unix/ntdll.so
0x00007f1c28a867c0  0x00007f1c28d85346  Yes         /home/pablo/.wine/dosdevices/z:/home/pablo/msword/bin/WORD1.exe.so
[... 60 líneas más, todas Yes o Yes (*), log completo en
     /tmp/claude-*/scratchpad/gdb2.log de esta sesión ...]

===== RESOLVE FILE:LINE =====
.../gdb2.txt:6: Error in sourced command file:
No source file named LOADFONT.C.
```

El último error (`No source file named LOADFONT.C`) es la misma huella
del problema de 12.1, confirmada por segunda vía independiente: el objeto
compartido sí está cargado y sí tiene símbolos (`WORD1.exe.so`, `Yes`);
lo que no resuelve es el nombre en mayúsculas.

### 12.3 Hipótesis `vsci.hdcScratch` — rama "nulo" descartada en vivo; rama "stale" sigue sin cerrar

Con los breakpoints en minúsculas ya disparando (12.1), se imprimió el
estado real en los dos puntos que §10 dejó como pregunta abierta:

**En `FSelectFont`, justo antes de `GetStockObject`/`SelectObject`
(`loadfont.c:711`):**
```
Breakpoint 2, FSelectFont (...) at .../loadfont.c:711
711			HFONT hfontSystem = GetStockObject( SYSTEM_FONT );
$1 = (HDC) 0x7f750df50        # *phdc
$2 = (HDC) 0x3410055          # vsci.hdcScratch
```

**En `C_LoadFcid`, justo antes de `GetTextMetrics` (`loadfont.c:349`):**
```
Breakpoint 1, C_LoadFcid (fcid=..., pfti=0x7ffff7508460 <vfti>, fWidthsOnly=1) at .../loadfont.c:349
349		GetTextMetrics( hdc, (LPTEXTMETRIC) &tm );
$1 = (HDC) 0x120100af
$2 = {fMonochrome = 0, ..., {mdcdScratch = {hdc = 0x3410055, pbmi = ...}, {hdcScratch = 0x3410055, pbmiScratch = ...}}, mdcdBmp = {hdc = 0x1410069, ...}, ..., dxpScreen = 1024, dypScreen = 768, ..., hbrBkgrnd = 0x910005a, ...}
$3 = 1   # fWidthsOnly
```

**Rama "nulo": descartada.** `hdc`, `vsci.hdcScratch` y el resto de
`vsci` están poblados con valores no-nulos y con pinta razonable
(`dxpScreen=1024`, `dypScreen=768` — coincide con la resolución real de
Xvfb `:99` usada en esta corrida) en ambos puntos.

**Rama "stale": NO descartada — sigue abierta.** Un `HDC` liberado (por
`DeleteDC`/`ReleaseDC` sobre el mismo valor, en otro punto del arranque)
puede seguir teniendo un valor de puntero perfectamente no-nulo; la sola
inspección de `print vsci.hdcScratch` no distingue "handle vivo y válido"
de "handle liberado, valor colgante". Cerrar esta rama requiere una
comprobación real de validez del handle — candidatos, ninguno probado en
esta sesión: `GetObjectType(hdc)` (retorna 0 sobre handle inválido),
`GetDeviceCaps` con chequeo de retorno, o el canal `WINEDEBUG=+gdi` (ver
12.6) puesto alrededor de estos dos puntos para ver si Wine ya reportó
un `DeleteDC` sobre `0x3410055` antes de este momento del arranque. §10
queda como: **rama nulo cerrada, rama stale pendiente** — no "hipótesis
descartada" sin más.

### 12.4 El checker de heap de glibc corre bajo Wine — con valor diagnóstico limitado, no "descartado igual que ASan/valgrind"

Confirmado en vivo, proceso `WORD1.exe.so` real (PID 141797, lanzado
desde `bin/`, sin recompilar):

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

154s vivo, `LD_PRELOAD`/`GLIBC_TUNABLES` sobreviven el re-exec de
`wine64`, sin abortar. No choca con la reserva de espacio de direcciones
como ASan/valgrind (§4, §8) porque no reserva ni desmapea nada — solo
instrumenta el `malloc` de glibc.

**Pero — y esto es lo que matiza §8, no lo confirma sin más — casi toda
la memoria de Word 1.1a pasa por `GlobalAlloc`/`LocalAlloc`/`HeapAlloc`
(API Win16/Win32), que Wine resuelve con su propio asignador
(`Rtl*Heap`, sobre `ntdll`), no con el `malloc` de glibc.** Evidencia
directa, canal `WINEDEBUG=+heap` contra el mismo binario:

```
$ DISPLAY=:99 WINEDEBUG=+heap /usr/lib/wine/wine64 WORD1.exe.so
05e4:trace:heap:RtlCreateHeap flags 0x2, addr 0000000000000000, total_size 0, commit_size 0, lock 0000000000000000, params 0000000000000000
05e4:trace:heap:RtlAllocateHeap handle 00007FFFFE220000, flags 0x8, size 0x2000, return 00007FFFFE2208F0, status 0.
05e4:trace:heap:RtlAllocateHeap handle 00007FFFFE220000, flags 0x8, size 0x500, return 00007FFFFE222920, status 0.
05e4:trace:heap:RtlAllocateHeap handle 00007FFFFE220000, flags 0, size 0x10e2, return 00007FFFFE222E50, status 0.
05e4:trace:heap:RtlFreeHeap handle 00007FFFFE220000, flags 0, ptr 00007FFFFE224690, return 1, status 0.
05e4:trace:heap:RtlReAllocateHeap handle 00007FFFFE220000, flags 0x10, ptr 00007FFFFE225860, size 0x1180, return 00007FFFFE225860, status 0.
[... decenas de líneas más, mismo patrón, handle 00007FFFFE220000 -- una
     región de memoria propia de Wine, separada del heap de glibc ...]
```

Toda la actividad de asignación real del arranque pasa por
`RtlAllocateHeap`/`RtlFreeHeap`/`RtlReAllocateHeap`/`RtlSizeHeap` sobre un
`handle` propio (`00007FFFFE220000`), gestionado por `ntdll.so` de Wine
— una capa de metadatos separada de los chunks de `malloc` de glibc que
`glibc.malloc.check` inspecciona. Una corrupción que dañe los metadatos
de *ese* heap (el candidato más probable para un bug de Word 1.1a
original, que usa `GlobalAlloc`/`HeapAlloc` casi exclusivamente) **no la
va a detectar** `glibc.malloc.check`, sin importar cuánto tiempo corra
limpio. La corrida de 154s sin abortar (arriba) **no es evidencia de que
el arranque esté libre de corrupción** — solo de que, si la hay, no está
pasando por el heap de glibc.

### 12.5 Ausencia de `wine-preloader` — específica de esta instalación, no generalizada

```
$ find /usr -iname '*preloader*'
[sin salida]
$ file /usr/lib/wine/wine64
/usr/lib/wine/wine64: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=017a68eb9b041254c9c597213f78aad6f1f32317, for GNU/Linux 3.2.0, stripped
$ wine --version
wine-10.0 (Debian 10.0~repack-6)
```

`wine64` en este paquete (`wine64 10.0~repack-6`, Debian) es un PIE ELF
normal con intérprete estándar `ld-linux`, sin binario `preloader`
separado en ningún lado bajo `/usr`. Esto es específico de este empaquetado
de wine-staging 10.0/Debian y **no se generalizó ni se puso a prueba**
contra Fedora 44/wine-staging 11.0 (el entorno de §1-§9, donde §2 sí
documenta con evidencia directa un proceso `wine-preloader` real
apareciendo en la traza de `gdb`). No asumir que esta ausencia se
sostiene en esa otra instalación.

### 12.6 `WINEDEBUG=+heap` — verificado contra el binario instalado, fuente `heap.c` no disponible localmente

```
$ find / -path '*/wine*/dlls/ntdll/heap.c' 2>/dev/null
[sin salida, exit=1]
$ dpkg -l | grep -i wine
ii  libwine:amd64        10.0~repack-6  amd64  Windows API implementation - library
ii  libwine-dev:amd64     10.0~repack-6  amd64  Windows API implementation - development files
ii  wine                  10.0~repack-6  all    Windows API implementation - standard suite
ii  wine64                10.0~repack-6  amd64  Windows API implementation - 64-bit binary loader
ii  wine64-tools          10.0~repack-6  amd64  Windows API implementation - 64-bit developer tools
```

Ningún paquete de fuentes de Wine está instalado en este sistema —
`dpkg -l` solo lista los binarios/dev-headers. **No se pudo revisar
`heap.c` real; lo que sigue está verificado contra el comportamiento en
vivo del binario instalado, no contra su fuente.**

El canal `heap` existe y produce trazas reales (ver el bloque completo en
12.4). Probado también a los otros dos niveles de clase que documenta
`man wine`:

```
$ DISPLAY=:99 timeout 3 env WINEDEBUG=warn+heap /usr/lib/wine/wine64 WORD1.exe.so
[sin líneas de heap en 3s de arranque idle]
$ DISPLAY=:99 timeout 3 env WINEDEBUG=err+heap /usr/lib/wine/wine64 WORD1.exe.so
[sin líneas de heap en 3s de arranque idle]
```

`warn+heap`/`err+heap` no emitieron nada en un arranque limpio de 3s —
consistente con que solo hablan cuando hay algo que reportar, pero **sin
la fuente no se puede confirmar qué condición exacta dispara un `warn` o
`err` en este canal** (p. ej. si valida la integridad de la arena en cada
llamada, o solo reporta fallos de `NtAllocateVirtualMemory`/parámetros
inválidos). Esto queda como brecha explícita, no como asumido.

**Comando recomendado para el hito 2 del plan v2 (en Fedora, donde sí
reproduce el crash de §1):**
```
WINEDEBUG=+heap wine WORD1.exe.so 2>heap.trace
```
`trace+heap` (`+heap` es alias de `trace+heap` según la sintaxis de
`WINEDEBUG` en `man wine`) da el registro completo de
`RtlAllocateHeap`/`RtlFreeHeap`/`RtlReAllocateHeap`/`RtlSizeHeap` con
tamaño y puntero devuelto en cada llamada — suficiente para acotar la
ventana temporal de la escritura corruptora cruzando ese log contra la
dirección de crash que ya da `addr2line` (§5), sin necesitar la fuente de
`heap.c`. Combinarlo con `warn+heap,err+heap` en la misma corrida
(`WINEDEBUG=+heap,warn+heap,err+heap` es redundante ya que `+heap`
activa todas las clases; alcanza con `WINEDEBUG=+heap`) para no perder
ningún nivel de mensaje si el crash real sí dispara uno.

## 13. Rama "stale" de `vsci.hdcScratch` — cerrada en este entorno, sin usar `GetObjectType` (2026-08-13)

Continuación de §10/§12.3, mismo entorno (Debian 13/wine-10.0/gdb 16.3),
binario reconstruido tras los 7 commits de migración `OpusMem*` de esta
sesión (`bf7a5e1`..`aab06e5`) — **el crash de §1 sigue sin reproducir
acá**, verificado de nuevo antes de este análisis (`wine WORD1.exe.so`
bajo `Xvfb :99`, 10s, sin salida, mismo comportamiento que §11.2). Este
punto no toca el bloqueador principal; cierra el ítem secundario que
§12.3 dejó abierto.

**Intento fallido, documentado para no repetirlo:** llamar
`GetObjectType(hdc)` desde `gdb` (`print GetObjectType(vsci.hdcScratch)`)
falla con `No symbol "GetObjectType" en el contexto actual` en los dos
breakpoints de §12.1 (`loadfont.c:711` y `:349`). Causa, confirmada con
`info sharedlibrary` completo (sin filtro) en el mismo proceso: en esta
build Winelib, `gdi32` no aparece como `.so` nativo separado — a
diferencia de `win32u.so`/`winex11.so`/`winspool.so`
(`/usr/lib/x86_64-linux-gnu/wine/x86_64-unix/`), que sí están en la
lista. Confirmado también por `nm -D bin/WORD1.exe.so | grep -w
GetObjectType` (sin resultado) y `GetTextMetrics` (sin resultado) — sólo
`FSelectFont` (símbolo propio de `Opus/`) aparece exportado. Lectura: en
esta configuración `gdi32` se resuelve puramente vía PE (importado a
través de la maquinaria de carga PE de Wine, sin contraparte ELF que
`gdb` pueda ver), así que `gdb` no tiene con qué resolver el nombre por
symbol lookup nativo — llamar la función real requeriría reconstruir el
thunk de import a mano, no vale el costo frente a la alternativa de
abajo.

**Vía que sí funcionó: `WINEDEBUG=+gdi` sobre una corrida completa,
grep por el valor de handle.** Mismo comando de arranque que el resto de
esta sección (`cwd=bin/`, `DISPLAY=:99`):

```
$ DISPLAY=:99 timeout 8 env WINEDEBUG=+gdi wine WORD1.exe.so >gdi.trace 2>&1
$ wc -l gdi.trace
45265 gdi.trace
```

`vsci.hdcScratch` se crea una sola vez, temprano en el arranque:

```
0024:trace:gdi:alloc_gdi_handle allocated NTGDI_OBJ_MEMDC 0x3410055 73/65536
```

Mismo valor de handle (`0x3410055`) que en la corrida de `gdb` de §12.3
— determinístico en este build/entorno, no una coincidencia de esta
corrida. Se reutiliza 43 veces vía `SelectObject` a lo largo de las
45265 líneas del trace (última aparición en la línea 39537, de 45265),
y **cero** apariciones junto a `NtGdiDeleteObjectApp`/`free_gdi_handle`
en las 3982 líneas que sí contienen esos dos tokens en todo el trace —
verificado con `grep -c "free_gdi_handle\|DeleteDC" gdi.trace` (3982
líneas, ninguna con `3410055`) y `grep -n 3410055 gdi.trace | grep -iv
"SelectObject\|alloc_gdi_handle"` (sin salida). El trace termina con el
proceso todavía activo, dibujando contenido real sobre otro DC (`Polygon`
`Rectangle` `SelectObject` sobre `000000000D0100CD`, un HDC de ventana
distinto) hasta el corte por `timeout` — sin ninguna excepción, `fault`,
`crash` ni mensaje de corrupción en las 45265 líneas (`grep -in
"exception|crash|segv|fault|corrupt" gdi.trace`, sin resultado).

**Conclusión: rama "stale" descartada para esta corrida concreta, no en
general.** `vsci.hdcScratch` está vivo (nunca liberado) durante toda la
ventana observada de 8s, incluyendo el uso real en `FSelectFont`/
`C_LoadFcid` de §12.1/§12.3 — no es un handle colgante en este arranque.
§10 queda ahora: **rama nulo cerrada (§12.3), rama stale cerrada para
este entorno (aquí)** — la hipótesis del HDC en `vsci.hdcScratch` deja
de ser candidata a explicar un crash que, de todos modos, este entorno
nunca reproduce. No se puede generalizar a Fedora (donde sí reproduce
§1) sin repetir esta misma captura allá — queda como parte del hito 2
pendiente, no como algo que este resultado ya cubre.

---

## Sesión hp-15 (EndeavourOS/Arch) — 2026-08-14: reproduce, y primer candidato con nombre y línea

**Entorno:** EndeavourOS (Arch, rolling), GCC 16.2.1, wine-staging 11.15, `gdb`
17.2, `valgrind` 3.25.1. Build desde `HEAD` (`5fed452`), reconfigurado y
reconstruido en esta sesión (`opus_original_engine` 0 errores, `WORD1.exe`/
`WORD1.exe.so` enlazados). `Xvfb :99` dedicado (no el display real de la
sesión de escritorio) — instalado (`xorg-server-xvfb`) para no interferir con
el entorno gráfico en uso.

### 1. Reproduce — cuarta y quinta firma de corrupción

Cuatro corridas con `gdb -q --batch -ex run -ex "bt full" --args wine
WORD1.exe.so`, mismo punto de arranque hasta el fallo
(`DwmSetWindowAttribute` stub, igual que Fedora/Debian):

| Corrida | Mensaje glibc |
|---|---|
| 1 | `free(): invalid pointer` |
| 2 | `free(): invalid next size (normal)` (== firma #2 de Fedora, §1) |
| 3 | `free(): invalid next size (normal)` |
| 4 (con debuginfod) | `double free or corruption (!prev)` |

Dos firmas nuevas (`invalid pointer`, `double free or corruption (!prev)`)
que no aparecían en Fedora ni Debian — refuerza la lectura de §1: es
corrupción de heap real y timing-dependiente, no un bug determinista de
lógica. **hp-15 reproduce de forma consistente (4/4)**, a diferencia del VPS
(Debian/GCC 14.2/wine 10.0, confirmado que no reproduce, `02-pendientes-fedora.md`)
— GCC ≥15 parece ser la variable relevante, no la distro.

### 2. Hito 2 ejecutado por primera vez: `WINEDEBUG=+heap`

Nunca corrido antes en ningún entorno (§2 de `02-pendientes-fedora.md` lo
dejaba como recomendación pendiente). Resultado: **888.073 líneas**,
crash real en la línea 24273 — el resto son hilos que siguieron corriendo
después del `abort()` de este hilo (no es que el proceso siguiera vivo; es
mezcla de buffering entre el canal de trace de Wine y el `fprintf` directo
de glibc a stderr — el orden de líneas post-crash no es cronológicamente
confiable entre sí, pero sí lo es dentro de un mismo hilo).

**Hallazgo:** inmediatamente antes de la línea del crash **no hay ningún
`RtlFreeHeap` logueado** — la corrupción se detecta en un `free()` que no
pasa por el wrapper de heap de Wine que instrumenta `+heap`. Esto es
consistente con lo que sigue (§3): el `free()` que aborta es el destructor
de un `std::wstring` de C++ (glibc `malloc`/`free` directo vía
`operator delete`), no un `HeapFree`/`GlobalFree` de la API Win32 que
`WINEDEBUG=+heap` sí habría capturado.

### 3. Frame #0 simbolizado — y algo más allá de frame #0

`info proc mappings` + `x/3i $pc` en el punto del abort: `$pc` cae dentro de
`/usr/lib/libc.so.6` (rango ejecutable `0x...c24000`-`0x...d9f000`) — es
código interno de glibc (la ruta de `malloc_printerr`/`abort`/`raise`), como
se esperaba. `bt` no desenrolla más allá de frame #0 — probado también con
`debuginfod.archlinux.org` habilitado (`set debuginfod enabled on`, símbolos
sí se descargaron a `~/.cache/debuginfod_client/`, 6 build-ids) y sigue sin
desenrollar. **No es falta de símbolos — es imposibilidad de unwind por CFI/
frame-pointers rotos en el código optimizado de glibc en este punto**, la
misma familia de problema que ya bloqueaba `winedbg`/`dbghelp` en Fedora (§3
original), confirmada ahora también con gdb vanilla + debuginfod en Arch.

**Rodeo que sí funcionó — escaneo manual de stack:** con el mapa de memoria
ya capturado (`info proc mappings`) y un volcado crudo del stack
(`x/400gx $rsp`), clasifiqué cada valor de 8 bytes contra los rangos
ejecutables conocidos (libc, `ntdll.dll`, `user32.dll`, `win32u.dll`,
`WORD1.exe.so`) con un script Python. Encontró una cadena de direcciones
dentro de `WORD1.exe.so` — no es un unwind real (es memoria de stack cruda,
puede incluir basura de llamadas ya retornadas), pero cruzada con
`addr2line -e WORD1.exe.so -f -C` da nombres y líneas de código reales y
consistentes entre sí:

```
new_allocator<wchar_t>::deallocate           new_allocator.h:184
basic_string<wchar_t>::_M_dispose            basic_string.h:299
basic_string<wchar_t>::~basic_string         basic_string.h:920
sync_combo(HWND, HWND, int&)                 opus_win95_chrome.cpp:890  <-- el for de la línea 890
ComboEnumeration::~ComboEnumeration          opus_win95_chrome.cpp:814
locate_source_combos(HWND, ToolbarState&)    opus_win95_chrome.cpp:849
sync_mirrors(HWND, ToolbarState&)            opus_win95_chrome.cpp:924
toolbar_window_proc(HWND, UINT, WPARAM, LPARAM)  opus_win95_chrome.cpp:2557, 2604
Dispatch<2ul>(...)                           opus_asm_wproc.cpp:79
```

Los tres primeros frames (deallocate → `_M_dispose` → `~basic_string`) son
exactamente la ruta interna de "un `std::wstring` se destruye y su buffer se
libera" — y el frame que lo posee es `sync_combo`, línea 890, que es el
`for` de este bloque (`src/port/original/opus_win95_chrome.cpp:884-897`):

```cpp
if (count >= 0 && count != copied_count) {
    std::wstring mirror_text;
    const int mirror_length = GetWindowTextLengthW(mirror);
    mirror_text.resize(static_cast<std::size_t>(mirror_length) + 1);
    GetWindowTextW(mirror, &mirror_text[0], mirror_length + 1);   // línea 888
    SendMessageW(mirror, CB_RESETCONTENT, 0, 0);
    for (int index = 0; index < count; ++index) {                // línea 890
        const std::wstring item = wide_from_ansi(combo_item(source, index));
        SendMessageW(mirror, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(item.c_str()));
    }                                                              // línea 894 — ~item() por iteración
    SetWindowTextW(mirror, mirror_text.c_str());
    ...
```

**Hipótesis concreta, no verificada con instrumentación adicional:**
`mirror_text` (línea 885-888) se dimensiona con `GetWindowTextLengthW` y se
escribe con `GetWindowTextW` sobre el mismo buffer. MSDN documenta
explícitamente que `GetWindowTextLength{A,W}` puede devolver una longitud
que no coincide con lo que la variante opuesta (`W` vs `A`) termina
escribiendo, específicamente en mezclas ANSI/Unicode — exactamente el caso
aquí, ya que el resto de `sync_combo`/`combo_item` usa `SendMessageA` sobre
el mismo control. Si la reimplementación de Wine de este par de APIs
escribe más `wchar_t` de los que `GetWindowTextLengthW` reportó, es un
heap-buffer-overflow de tamaño acotado (unos pocos wchar_t) sobre el buffer
de `mirror_text` — exactamente la clase de corrupción que produce
`free(): invalid next size` / `double free` en el `free()` de una llamada
posterior (no necesariamente la de `mirror_text` mismo — puede manifestarse
en el chunk vecino, como en cualquier heap-overflow), consistente con que la
firma varíe entre corridas (§1: depende de qué chunk linda con el buffer
dañado).

**No confirmado. No aplicado.** No se instrumentó `sync_combo` para verificar
tamaño real escrito vs. reportado (siguiente paso obvio, no intentado en
esta sesión) ni se descartó la otra candidata más débil del mismo bloque
(`combo_item`/`wide_from_ansi`, revisadas y con aritmética de tamaño
correcta a simple lectura — MultiByteToWideChar con `cchMultiByte=-1` ya
incluye el terminador nulo en el conteo devuelto, sin off-by-one visible).

### 4. Hipótesis de §3 (`mirror_text`) — instrumentada y **refutada**

Instrumentación temporal aplicada a `sync_combo` (línea 886-888, revertida
después, no commiteada): en vez de escribir directamente en
`mirror_text[0]`, se escribió en un `std::vector<wchar_t>` sobre-reservado
con 16 celdas canario (`0xCDCD`) más allá del límite `mirror_length + 1`
pasado como `nMaxCount` a `GetWindowTextW`, comparando además su valor de
retorno contra `mirror_length`.

```
[sync_combo DIAG] mirror_length=5 copied=5 guard_cells_clobbered=0/16
```

**5/5 corridas, mismo resultado exacto** (los tres combos —
style/font/size— pasan por este mismo bloque instrumentado en cada
corrida). `GetWindowTextW` devuelve exactamente lo pedido y **no toca
ninguna celda canario** — el par `GetWindowTextLengthW`/`GetWindowTextW`
sobre `mirror` se comporta correctamente en esta reimplementación de Wine.
**Hipótesis refutada, no es la fuente de la corrupción.**

El crash **sigue ocurriendo** con la instrumentación puesta (1/5 corridas,
`free(): invalid pointer`) — el bug es real y sigue vivo, solo que no es
este overflow puntual. El resto de la cadena de stack de §3 (destructor de
`std::wstring` dentro de `sync_combo`/`locate_source_combos`/
`sync_mirrors`) sigue siendo la evidencia más sólida que hay; lo que se
descarta es específicamente el mecanismo propuesto en §3, no la ubicación
general.

### 5. Dónde retomar

1. ~~**Candidata siguiente, no probada:** `combo_item()`/`wide_from_ansi()`
   (línea ~798-857)~~ — **instrumentada y refutada, ver §6.**
2. **También sin probar:** el propio `ComboEnumeration`/
   `collect_original_combos` (línea 814-828) — aparece en la cadena de
   stack de §3 como destructor, pero no se revisó su contenido en detalle
   (`std::vector<HWND> combos`, callback de `EnumChildWindows`).
3. Repetir el mismo escaneo de stack (`info proc mappings` + `x/400gx $rsp`
   + `addr2line`) en el VPS/Debian una vez que se entienda por qué ahí no
   reproduce — podría no ser "no reproduce el bug", sino "reproduce pero no
   se manifiesta como abort visible" bajo GCC 14 (layout de heap distinto).
   No verificado.
4. La técnica de escaneo manual de stack (sin depender de `bt`/unwind roto)
   y la de canario post-buffer (sin depender de ASan/valgrind, ambos ya
   descartados en Fedora por chocar con `wine-preloader`, §7 de
   `02-pendientes-fedora.md`) quedan como métodos reutilizables para
   cualquier crash futuro sin DWARF/CFI confiable en este proyecto — no son
   específicos de este bug.

### 6. `combo_item()`/`wide_from_ansi()` — instrumentados y **refutados** (mismo día, hp-15)

Segunda candidata de §5 (punto 1), el otro tramo de la misma cadena de
stack de §3 (`combo_item` alimenta `wide_from_ansi`, cuyo resultado es el
`item` que `sync_combo` pasa a `CB_ADDSTRING` en el `for` de la línea 890).

Instrumentación temporal aplicada a ambas funciones
(`src/port/original/opus_win95_chrome.cpp`, revertida después, no
commiteada — diff confirmado limpio contra `HEAD` tras revertir):

- `combo_item()`: buffer de `length + 1` bytes sobre-reservado con 16
  celdas canario `0xCD`, comparando el valor de retorno de
  `CB_GETLBTEXT` (`copied`) contra `CB_GETLBTEXTLEN` (`length`).
- `wide_from_ansi()`: buffer de `count` `wchar_t` sobre-reservado con 16
  celdas canario `0xCDCD`, comparando el valor de retorno de la segunda
  llamada a `MultiByteToWideChar` (`written`) contra la primera
  (`count`).

**5/5 corridas** (`gdb -q --batch -ex run -ex "bt full"`, `Xvfb :99`,
build reconstruido desde `HEAD` con la instrumentación): salida de DIAG
**idéntica byte a byte entre las 5 corridas** (mismo `md5sum`) — más
determinista que la instrumentación de `mirror_text` en §4, que ya tuvo
variación de firma en 1/5. Ejemplo (una corrida sincroniza un combo con 5
entradas):

```
[combo_item DIAG] index=0 length=12 copied=12 guard_cells_clobbered=0/16
[wide_from_ansi DIAG] count=13 written=13 guard_cells_clobbered=0/16
[combo_item DIAG] index=1 length=12 copied=12 guard_cells_clobbered=0/16
[wide_from_ansi DIAG] count=13 written=13 guard_cells_clobbered=0/16
...
```

`copied == length` y `written == count` en las 10 líneas DIAG de las 5
corridas, **ninguna celda canario tocada nunca** (`guard_cells_clobbered=0/16`
en el 100% de las invocaciones). El crash **persiste en las 5/5 corridas**,
misma firma exacta en las cinco (`free(): invalid next size (normal)`,
`SIGABRT`) — más consistente incluso que el baseline de §1 (que variaba
entre firmas). **Hipótesis refutada**: ni `combo_item()` ni
`wide_from_ansi()` escriben fuera de lo que reportan sus longitudes; el
par `CB_GETLBTEXTLEN`/`CB_GETLBTEXT` y el patrón de doble llamada a
`MultiByteToWideChar` se comportan correctamente en esta reimplementación
de Wine, igual que ya se había confirmado para `GetWindowTextLengthW`/
`GetWindowTextW` en §4.

**Lectura acumulada (§4 + este punto):** los tres bloques de
lectura/escritura de tamaño explícito dentro de `sync_combo` y sus
llamadas directas (`mirror_text`, `combo_item`, `wide_from_ansi`) quedan
descartados como la fuente. La cadena de stack de §3 sigue siendo la
evidencia más sólida disponible (el crash real es un `free()` de
`std::wstring` en esa vecindad de código), pero el mecanismo concreto
sigue sin aislarse. Punto 2 de §5 (`ComboEnumeration`/
`collect_original_combos`, línea 814-828 — el `std::vector<HWND> combos`
y el callback de `EnumChildWindows`) queda como la única candidata de esa
cadena de stack todavía sin instrumentar.

**Build restaurado:** `opus_original_engine` y `WORD1` reconstruidos
después de revertir la instrumentación — el binario en `bin/` vuelve a
corresponder exactamente al código de `HEAD`, no queda instrumentación
residual.
