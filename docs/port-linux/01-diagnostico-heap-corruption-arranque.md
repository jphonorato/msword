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
2. ~~**También sin probar:** el propio `ComboEnumeration`/
   `collect_original_combos` (línea 814-828)~~ — **instrumentado y
   refutado, ver §7 (buffer local) y §11 (destrucción del vector +
   viaje por `LPARAM`, agotado a fondo).**
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
`collect_original_combos`, línea 814-828) sigue como pendiente — ver §7,
también refutado.

**Build restaurado:** `opus_original_engine` y `WORD1` reconstruidos
después de revertir la instrumentación — el binario en `bin/` vuelve a
corresponder exactamente al código de `HEAD`, no queda instrumentación
residual.

### 7. `ComboEnumeration`/`collect_original_combos` — instrumentado y **refutado** (mismo día, hp-15)

Punto 2 de §5, el último candidato pendiente de la cadena de stack de §3.
De las dos piezas de este bloque (`opus_win95_chrome.cpp:814-828`), solo
una tiene la forma "API de Win32 llena un buffer de tamaño explícito" que
el método de canario puede probar directamente: el `wchar_t
class_name[64]` de `collect_original_combos`, llenado por
`GetClassNameW(candidate, class_name, 64)`. La otra —
`std::vector<HWND> combos` creciendo vía `push_back` en cada callback de
`EnumChildWindows` — es gestión de memoria de C++ estándar, no una
escritura de la API sobre un buffer nuestro con tamaño pasado
explícitamente; no hay un límite ajeno que instrumentar del mismo modo
(su propia gestión interna de capacidad no es de las que este proyecto
haya visto fallar). Se instrumentó solo `class_name`.

Instrumentación temporal (revertida después, no commiteada — diff
confirmado limpio contra `HEAD`): buffer `wchar_t[64 + 16]`, 16 celdas
canario `0xCDCD` más allá del límite de 64 pasado como `nMaxCount`,
comparando el valor de retorno de `GetClassNameW` (`written`) y
verificando las celdas canario tras la llamada. (Nota aparte: al quitar
la cero-inicialización original del buffer para poder llenarlo con el
patrón canario, hubo que añadir un `class_name[0] = L'\0'` explícito para
el caso `written == 0` — `GetClassNameW` no garantiza terminar el buffer
si falla, y el código original dependía de la cero-inicialización para
ese caso. No afecta la detección de overflow, que ya se evalúa antes de
esa rama.)

**5/5 corridas**, mismo método (`gdb -q --batch -ex run -ex "bt full"`,
`Xvfb :99`): salida de DIAG **idéntica byte a byte entre las 5 corridas**
(mismo `md5sum`, igual que §6) — 20 líneas DIAG por corrida (cada llamada
a `EnumChildWindows` recorre más ventanas candidatas que las que
`combo_item` procesaba, de ahí el doble de invocaciones que en §6).
Valores de `written` observados: `4, 6, 7, 8, 10, 13` — siempre muy por
debajo del límite de 64. **`guard_cells_clobbered=0/16` en el 100% de las
20×5=100 invocaciones.** El crash persiste en las 5/5 corridas, misma
firma exacta (`free(): invalid next size (normal)`, `SIGABRT`).
**Hipótesis refutada:** `GetClassNameW` respeta su `nMaxCount` en esta
reimplementación de Wine, igual que ya se confirmó para los otros tres
pares API de §4 y §6.

**Lectura acumulada final (§4 + §6 + §7):** con este punto se agota la
cadena de stack completa de §3 (`sync_combo` → `locate_source_combos` →
`collect_original_combos`) en lo que respecta a escrituras de tamaño
explícito sobre buffers propios — las cuatro API de Win32 involucradas
(`GetWindowTextW`, `CB_GETLBTEXT`, `MultiByteToWideChar`,
`GetClassNameW`) se comportan correctamente. Ninguna de las cuatro es la
fuente. Quedan dos lecturas posibles, ninguna instrumentada aún:

1. La corrupción real no está en este bloque de código en absoluto — la
   cadena de stack de §3 es memoria de stack cruda (no un unwind real, ver
   §3), así que podría estar mostrando llamadas ya retornadas, no el
   punto de origen. Sería necesario repetir el escaneo manual de stack en
   una corrida fresca y verificar si la misma cadena aparece de forma
   consistente, o si varía entre corridas (no verificado todavía).
2. La corrupción está en la gestión propia de `std::vector<HWND> combos`
   (crecimiento/realloc) o en algo fuera de este archivo que corrompe un
   chunk vecino que luego se libera aquí — ninguna de las dos vías tiene
   un método de canario directo aplicable; requeriría una herramienta
   distinta (el checklist de `02-pendientes-fedora.md` §3, symbolizar
   frame #0, sigue siendo la vía más prometedora sin explorar).

**Build restaurado:** `opus_original_engine` y `WORD1` reconstruidos de
nuevo después de revertir esta instrumentación también.

### 8. Comparación contra el entorno que no reproduce (`/vps`, Debian 13) — reabre y vuelve a cerrar `valgrind`, por un motivo distinto y más amplio que `wine-preloader`

Con la cadena de stack de §3 agotada (§4, §6, §7, las cuatro sin
resultado), se probó el ángulo complementario: en vez de seguir buscando
en Arch (donde el crash reproduce), comparar contra el entorno que
**no** reproduce (Debian 13/GCC 14.2/wine-10.0, el VPS de
`~/.ssh/config` alias `vps`) para acotar la variable de entorno, tal
como quedaba pendiente en `02-pendientes-fedora.md` ("No se sabe
todavía cuál de estas variables... es la que hace la diferencia").

**Reconstrucción y reconfirmación del baseline.** Repo del VPS ya estaba
al día con `ae8b0cb` (los commits de esta sesión, `d260424`/`ddd28dc`,
son solo docs — sin diff de código, el build es equivalente).
`opus_original_engine`/`WORD1` reconstruidos limpios en el VPS.

**Nota de tooling nueva, específica del VPS:** `gdb -q --batch -ex run
--args wine WORD1.exe.so` falla ahí con `"/usr/bin/wine": not in
executable format` — en este paquete Debian, `/usr/bin/wine` resuelve
(vía `update-alternatives`) a `/usr/bin/wine-stable`, un **script de
shell POSIX** que decide en tiempo de ejecución si usar `wine32` o
`wine64` (`wine32` falta, cae a `wine64`). `gdb --args` necesita poder
cargar el ejecutable como objeto BFD para el comando `run` implícito, y
un script no es un objeto ELF — de ahí el error. **Fix:** invocar
directamente `/usr/lib/wine/wine64` con `WINELOADER=/usr/lib/wine/wine64`
exportado (que es lo que el script termina haciendo igual). No aplica en
Arch, donde `/usr/bin/wine` es el binario real.

**Baseline reconfirmado, 5/5 corridas** (`gdb -q --batch -ex run -ex "bt
full" --args /usr/lib/wine/wine64 WORD1.exe.so`, mismo `Xvfb :99` ya
corriendo de una sesión anterior en el VPS): las cinco llegan al mismo
punto de arranque (`fixme:dwmapi:DwmSetWindowAttribute ... stub`) y
después **no crashean** — timeout a los 60s (`exit 124`), sin más
salida. Coincide exactamente con lo ya documentado en §11.2, ahora
reconfirmado sobre el `HEAD` actual (7 commits de migración de memoria
después del build de §11.2).

**Aclaración importante sobre qué significa "no reproduce" aquí:** el
proceso no queda trabado ni bloqueado en ese punto — llega a un estado
de reposo genuino (bucle de mensajes de Windows esperando input de
usuario, que bajo `Xvfb` sin interacción nunca llega). Es el
comportamiento normal de una app GUI idle, no un fallo de arranque
distinto. Confirma, de forma independiente, la lectura de §11.2 (no
solo la repite): el código de arranque —incluyendo el bloque
`sync_combo`/`locate_source_combos` que este documento lleva tres
secciones instrumentando— se ejecuta completo y sin abortar en este
entorno.

**Hallazgo colateral: no hay `wine-preloader` en este paquete Debian.**
`02-pendientes-fedora.md` §7 atribuye el bloqueo de ASan/valgrind
específicamente a la reserva de espacio de direcciones de
`wine-preloader` en Fedora. Buscar ese binario en el VPS
(`/usr/lib/wine/`) no encuentra nada — y tampoco existe en Arch/hp-15
(verificado también, mismo resultado). Esto abre la posibilidad de que
el bloqueo de valgrind no sea universal, solo específico del empaquetado
de Fedora — vale la pena reabrir la pregunta.

**Valgrind en el VPS — corre limpio, 240s, cero errores.** Sin
`wine-preloader` de por medio, `valgrind --error-exitcode=99` arranca
sin problema, llega al mismo punto de arranque, y corre 240s en reposo
(mismo estado idle de arriba) sin loguear ni un solo error de memoria.
Inicialmente prometedor — pero ver el punto siguiente antes de leerlo
como "el código está limpio ahí".

**Control directo en Arch (donde el crash sí ocurre) — valgrind no lo ve.**
Para no sobre-interpretar el resultado limpio del VPS (¿es limpio porque
no hay bug, o porque valgrind no está mirando lo que hay que mirar?), se
corrió el mismo `valgrind --error-exitcode=99` directo contra el binario
en Arch, donde el crash **sí** reproduce. Resultado: el crash **ocurre
igual bajo valgrind** — el mismo `free(): invalid pointer` de glibc se
imprime en la salida estándar, seguido del mismo intento roto de
backtrace de `dbghelp` que ya se documentó en hp-15 §3 (`elf_search_auxv
can't find symbol`, `dwarf2_get_cie wrong CIE pointer`) — pero **el log
de valgrind no registra ningún error**, ni antes ni durante. Verificado
con `-v` que la intercepción de `malloc`/`free` sí está activa (`REDIR:
... libc.so.6:malloc redirected...`, `REDIR: ... libc.so.6:free
redirected...`, ambos logueados *antes* de que se cargue `ntdll.so`, así
que cubren todo el código que se ejecuta después, incluyendo
`WORD1.exe.so`). **No se investigó la causa exacta** de por qué
memcheck no ve esta corrupción con la intercepción confirmadamente
activa — hipótesis no verificadas: podría ser un `free()` que no pasa
por el símbolo redirigido de `libc.so.6` por algún camino interno de
Wine/Winelib, o el bug podría depender del layout/timing exacto del
heap de glibc de un modo que la instrumentación de valgrind (mucho más
lenta, con su propio allocator) simplemente no dispara.

**Consecuencia: se retracta la lectura optimista del punto anterior.**
El resultado limpio de 240s en el VPS **no es evidencia** de que el
bloque `sync_combo`/`locate_source_combos` esté libre de bugs ahí —
es evidencia de que valgrind no es una herramienta útil para esta
corrupción específica, en ningún entorno probado hasta ahora, por un
motivo más amplio que el bloqueo por `wine-preloader` ya documentado
para Fedora. `valgrind` se re-cierra como vía, con esta razón nueva y
más general — actualizado en `02-pendientes-fedora.md` §7.

**Lo único que queda como resultado sólido de esta sección:** la
reconfirmación independiente de que Debian/GCC 14.2/wine-10.0 no
reproduce (§11.2 seguía vigente sobre el `HEAD` actual) y que llega
limpiamente al mismo punto de arranque que Fedora/Arch antes de quedar
en reposo normal. La pregunta de fondo —¿versión de Wine, de GCC, o
alguna otra diferencia de entorno es la variable causante?— sigue
abierta. No se intentó aislarla instalando un Wine más nuevo o un GCC
≥15 en el VPS: ambos requieren cambios a nivel de sistema (repo de
WineHQ + habilitar multiarch i386, o backports/sid para GCC) en una
máquina compartida con otros servicios — se deja pendiente de decisión
explícita antes de tocar paquetes del sistema ahí, no intentado en esta
sesión.

**Procesos limpiados:** ninguno quedó residual ni en el VPS (`timeout`
mató todo, verificado con `pgrep`) ni en local (verificado igual).

### 9. Symbolizar frame #0 — hecho, con precisión de línea; refuta la hipótesis "DLL de Wine" de `02-pendientes-fedora.md` §3

Pendiente arrastrado desde Fedora (§5/§7 de este documento, y §3 de
`02-pendientes-fedora.md`), donde la dirección de frame #0
(`0x00006FFFFFC1B75F`) no caía dentro de `WORD1` y se hipotetizaba sin
confirmar que fuera `user32`/`gdi32`/`ntdll`. hp-15 §3 ya había acotado
la región a `libc.so.6` por inspección de `$pc` a ojo; esta sesión lo
lleva a símbolo y línea exactos.

**Método:** `gdb -q --batch -ex "set debuginfod enabled on" -ex run -ex
"print/x \$pc" -ex "info proc mappings" --args wine WORD1.exe.so`,
identificar la fila de `info proc mappings` que contiene `$pc` (base de
`libc.so.6` = inicio del mapeo `r-xp` menos su offset de archivo),
restar la base a `$pc` para obtener el offset dentro del archivo, y
`addr2line -e <debuginfo cacheado por debuginfod> -f -C -p <offset>` —
el `debuginfo` completo (no solo símbolos dinámicos) ya estaba en
`~/.cache/debuginfod_client/` de la verificación de hp-15 §3, indexado
por build-id (`503200d7fda94a5dc6058d7e0694e5d1dcb2e372`, confirmado con
`readelf -n`). `nm -D` sobre el `.so` del sistema (sin debug info) da un
resultado engañoso (`pthread_key_delete`, el símbolo dinámico exportado
más cercano) — no usar esa vía sin el `debuginfo` real.

**Resultado, 3/3 corridas, dos firmas distintas** (`free(): invalid
pointer`, `free(): invalid next size (normal)`): **`$pc` idéntico en las
tres** (`0x00007ffff7c9a17c`), resuelto a
`__pthread_kill_implementation` en `nptl/pthread_kill.c:44`. Es la cola
genérica de `abort() → raise() → pthread_kill()` — la misma para
cualquier abort de glibc sin importar qué chequeo de heap lo disparó,
consistente con que las cuatro firmas de §1/hp-15-§1 compartan este
mismo frame #0.

**Conclusión:** frame #0 **no es una DLL de Wine** — la hipótesis de
`02-pendientes-fedora.md` §3 (basada en una dirección de un build viejo
de Fedora que ya no aplica, `WORD1+0x1FD57C`) queda refutada. Es, como
ya sugería hp-15 §3 de forma más gruesa, código interno de glibc — y al
ser la maquinaria genérica de entrega de señal, **no aporta información
sobre el origen de la corrupción por sí solo**: cualquier `free()`/`
malloc()` corrupto termina exactamente aquí. El punto útil de la cadena
sigue siendo más arriba (`malloc_printerr`/`_int_free`, y de ahí al
llamador real en código de `WORD1`) — ahí es donde el escaneo manual de
stack de hp-15 §3 (no un unwind real, pero cruzado con `addr2line` sobre
`WORD1.exe.so`) ya había encontrado la cadena `sync_combo`/
`locate_source_combos`, agotada en §4/§6/§7 de este documento sin
resultado.

**Ítem de `02-pendientes-fedora.md` §3 cerrado** con este resultado —
no queda pendiente de reintentar en Fedora, la dirección/símbolo es
consistente entre entornos (misma clase de dirección genérica de
glibc), solo cambia numéricamente por ASLR/versión de glibc, no en
naturaleza.

### 10. `C_FormatLineDxa` instrumentado — resultado negativo limpio: **no se llega a ejecutar** antes del crash de este startup

Retoma el candidato original de §7 ("revisar `C_FormatLineDxa` y su
vecindario a mano"), que quedó leído pero nunca instrumentado en §9/§10
(sesión de Fedora, antes de que hp-15 desviara el foco a
`sync_combo`/toolbar). `src/Opus/wordtech/format.c` es árbol restringido
(`CLAUDE.md`) — autorización explícita confirmada con el usuario antes
de editar, sin pasar por el trámite de issue dado que la instrucción fue
directa.

**Instrumentación temporal (revertida después, no commiteada — diff
confirmado limpio contra `HEAD`):**

1. Log de entrada en cada llamada (contador + `ww`/`doc`/`cp`/`dxa`),
   antes de cualquier `return` temprano.
2. Log en cada uno de los dos `return` tempranos de la función (guard de
   reentrancia `vrf.fInFormatLine`, línea ~540; cache-hit "Just did this
   one", línea ~591) y en el punto donde el código sigue de largo hacia
   la ejecución completa.
3. Canario de verdad (no de celdas, sino contra la asignación real del
   heap) en el único punto de la función que escribe en el buffer
   compartido `vhgrpchr` **sin chequeo de rango** — el comentario del
   propio código lo dice explícitamente: `/* Note: no need to check for
   sufficient space */` (línea ~2601, el terminador `chrmEnd` de fin de
   línea). Se comparó `bchrBreak + cbCHR` (lo que el código está por
   escribir) contra `CbOfH(vhgrpchr)` (tamaño real asignado al handle,
   vía `OpusCbOfH` — no `vbchrMax`, que es solo la cuenta que lleva el
   propio código y podría estar desincronizada de la realidad bajo un
   bug de tamaño LP64 como el ya confirmado en `bitapp.h:29`).

**Resultado, 5/5 corridas (dos tandas, la primera solo con el canario
del punto 3, la segunda añadiendo también el log de entrada/salida del
punto 1-2): cero líneas DIAG en las diez corridas combinadas.** El
crash ocurre con la firma y el punto de arranque de siempre
(`DwmSetWindowAttribute` stub → `free()`/`SIGABRT`), confirmando que el
pipeline de captura funciona (se ve todo el resto de la salida de Wine
normalmente) — la ausencia de DIAG no es un problema de instrumentación.

**Conclusión: `C_FormatLineDxa` nunca se llama** durante este arranque
concreto (documento en blanco recién abierto, sin tipeo ni interacción)
— ni siquiera entra a evaluar el guard de reentrancia, que es la primera
línea ejecutable de la función. Coherente con que no hay texto que
paginar todavía: el crash ocurre enteramente durante la construcción de
la ventana/toolbar (la cadena `sync_combo`/`locate_source_combos` de §3,
agotada en §4/§6/§7), antes de que el documento en blanco necesite su
primer pase de formateo de línea.

**Esto no descarta bugs reales dentro de `C_FormatLineDxa`** — solo
descarta que sea la causa de *este* crash de arranque específico. Sigue
siendo candidata legítima para cualquier crash que involucre paginación
real (con texto tipeado), camino que `opus_word1_ui_test --typing`/
`--font-typing` (`02-pendientes-fedora.md` §5, nunca ejecutado) sí
alcanzaría — pero es un bug distinto, con un repro distinto, no el que
este documento viene rastreando desde §1.

**Build restaurado** después de revertir ambas tandas de instrumentación.

### 11. `~ComboEnumeration` revisitado — destrucción del `std::vector<HWND>` confirmada limpia; también refutado el viaje por `LPARAM`

Punto 2 de §5 (`ComboEnumeration`/`collect_original_combos`), retomado
más a fondo que en §7: ahí solo se había instrumentado el buffer de
`GetClassNameW` dentro de `collect_original_combos` (refutado). Esta
vez el objetivo es la destrucción del propio `std::vector<HWND> combos`
— el frame `ComboEnumeration::~ComboEnumeration` aparece literal en la
cadena de stack de §3 — y, por separado, el viaje del puntero
`&enumeration` a través de `LPARAM` en `EnumChildWindows` (precedente de
bug de clase LP64 nunca antes probado para este puntero específico,
`bitapp.h:29`).

**Instrumentación temporal** (`opus_win95_chrome.cpp`, revertida
después, diff confirmado limpio):

1. Log de `&enumeration` (como puntero y como `LPARAM`) justo antes de
   `EnumChildWindows`, y log de `parameter` recibido en
   `collect_original_combos` — para comparar ambos valores.
2. Reemplazo de la destrucción implícita del vector (al salir de
   `locate_source_combos`) por `std::vector<HWND>().swap(enumeration.combos)`
   explícito e instrumentado — el idiom swap-with-empty fuerza el
   `delete[]` real en un punto que controlamos, con log inmediatamente
   antes y después.

**Resultado, 5/5 corridas:**

- **`locate_source_combos` se llama dos veces por corrida** (nunca antes
  documentado): la primera con `enumeration.combos` vacío (0 combos
  encontrados — la enumeración de hijos de ventana corre antes de que el
  toolbar esté completamente poblado), la segunda con 3 (`style`/`font`/
  `size`, coherente con lo ya visto en §4/§6). Ambas veces, el `parameter`
  recibido en `collect_original_combos` **coincide exactamente** con el
  `&enumeration` original logueado antes de la llamada — sin truncar ni
  corromperse, en las 38 invocaciones por corrida combinadas de las dos
  rondas. **Hipótesis del viaje por `LPARAM` refutada.**
- **La destrucción forzada del vector completa sin abortar las dos veces,
  en las 5/5 corridas**, sin importar la firma de crash de esa corrida
  (`free(): invalid pointer` / `free(): invalid next size`) — el log
  `after combos dtor, ok` imprime siempre. **`~ComboEnumeration` no es
  donde ocurre el abort.**
- **Ninguna línea DIAG aparece después del mensaje de crash** en ninguna
  corrida — confirma que el abort ocurre *después* de que ambas llamadas
  a `locate_source_combos` (con sus destrucciones) ya terminaron
  limpiamente, en código más adelante de la cadena de stack de §3
  (`sync_mirrors`/`toolbar_window_proc`) que **ninguna de las sesiones
  hp-15 ha instrumentado todavía**.

**Conclusión:** con esto se cierra por completo el punto 2 de §5, esta
vez a fondo (no solo el buffer local de §7, también la destrucción del
recurso compartido y el mecanismo de paso de puntero). La cadena
completa de §3 dentro de `sync_combo`/`locate_source_combos`
(`mirror_text`, `combo_item`, `wide_from_ansi`, `GetClassNameW`,
`LPARAM`, `~ComboEnumeration`) queda agotada sin encontrar la causa. El
dato nuevo más útil de esta sesión es el **acotamiento temporal**: el
abort ocurre estrictamente después de la segunda llamada a
`locate_source_combos`, no dentro de ella — la siguiente candidata
natural es instrumentar `sync_mirrors`/`toolbar_window_proc` en sí (el
código que llama a `locate_source_combos` y que sigue ejecutándose
después), no volver a los mismos cinco puntos ya refutados.

**Build restaurado** después de revertir esta instrumentación también.

### 12. `sync_combo`/`sync_mirrors` retomado — hallazgo mayor: el crash ocurre *dentro* del loop `CB_ADDSTRING`, en un índice distinto cada corrida, sobre datos válidos

Sigue la pista concreta que dejó §11 (el abort ocurre después de que
`locate_source_combos` termina limpio dos veces). El punto 1 de §5
(`combo_item()`/`wide_from_ansi()`) se había dado por agotado en §6,
pero ahí solo se probó el par dentro del `for` de población (línea
~890-892). Esta sesión encontró un **tercer** par longitud/texto dentro
de `sync_combo`, nunca antes tocado: `GetWindowTextLengthA(source)`/
`GetWindowTextA(source, ...)` en la rama `else` de
`!combo_or_child_has_focus(mirror)` (línea ~903-905) — instrumentado
igual que los anteriores (canario de 16 celdas). **Cero líneas DIAG en
5/5 corridas** — esa rama nunca se alcanza antes del crash.

**Instrumentación de seguimiento** (traza de ramas en `sync_combo`, sin
canario) reveló por qué, y de paso el hallazgo real de esta sesión:

- `sync_combo` se llama **repetidas veces** antes del crash: primero
  varias veces con `source == nullptr` (los tres combos, antes de que
  `locate_source_combos` los resuelva — coherente con §11), luego una
  vez más con `source` ya resuelto (`0x100c0`/`0x200e4`/`0x300cc` en las
  tres corridas). En ese punto, **`SendMessageA(source, CB_GETCOUNT, 0,
  0)` devuelve `count=1395`**, idéntico en 3/3 corridas — un número muy
  por encima de lo esperado para un combo de fuente/estilo/tamaño de un
  procesador de texto de 1989.
- Con `count=1395` el `for` de población (línea 890-894, ya recorrido
  en §6 pero solo hasta `index=4`) se instrumentó de nuevo con traza por
  iteración. **Los primeros ~16-20 valores de longitud de `combo_item`
  son idénticos a los ya vistos en §4/§6** (12, 12, 5, 19, 24, 20, 4, 9,
  7, 11, 8, 20, 11, 21, 17, 16, ...) — son nombres de fuente/estilo
  reales, no basura ni memoria sin inicializar. **El crash ocurre
  *dentro* de este loop, en un índice distinto cada corrida: 5, 14, y
  15** en tres corridas consecutivas con el mismo build — mientras se
  procesan datos válidos, no cerca del límite de 1395 ni en zona de
  índices fuera de rango.

**Lectura — cambia el diagnóstico de fondo:** cada escritura que este
proyecto controla directamente y puede instrumentar (`mirror_text`,
`combo_item`, `wide_from_ansi`, `GetClassNameW`, el tercer par de
`sync_combo`, el viaje por `LPARAM`, la destrucción de
`ComboEnumeration`) ha salido limpia, repetidas veces, en sesiones
distintas. Y sin embargo el crash persiste, **en un punto variable
dentro de un loop que hace la misma llamada Win32 (`CB_ADDSTRING`)
repetidamente sobre datos que en sí mismos son válidos.** Esa
combinación — mismo dato, mismo código, punto de fallo que se mueve
entre corridas, mientras cada verificación local de bordes sale limpia
— es la firma característica de **corrupción de heap acumulada dentro
de la implementación de Wine del control ComboBox** (la ruta interna
que `CB_ADDSTRING`/`CB_RESETCONTENT` ejercitan en `user32`/`comctl32`
bajo wine-staging 11.15), no un bug en el código de `Opus`/`port` que
esta investigación pueda seguir instrumentando con canarios locales.
**No confirmado con una herramienta que mire directamente el heap de
Wine** (los intentos con `+heap` y `valgrind` de §2/§8 no vieron nada,
pero por razones ya documentadas como no concluyentes para esta clase
de bug) — es una inferencia por eliminación, no una prueba directa.

**Pregunta abierta, no perseguida esta sesión:** ¿de dónde sale
`count=1395`? Podría ser un reflejo real (aunque inusualmente alto) del
número de fuentes que Wine enumera en este sistema vía fontconfig, o
podría ser en sí mismo un síntoma de que el combo `source` original ya
estaba con su lista interna dañada por un ciclo anterior de
`CB_RESETCONTENT`/`CB_ADDSTRING` sobre el *mismo* handle en otro punto
del arranque — no se comparó contra el conteo real de fuentes instaladas
en este sistema, ni se revisó si `source` (el combo original de Word,
no el mirror) recibe su propia carga de `CB_ADDSTRING` en otro lugar
del código antes de este punto.

**Consecuencia práctica para la siguiente sesión:** seguir
instrumentando código de `Opus`/`port` con canarios locales ya no es la
vía más prometedora — los ocho candidatos de esa clase (§4, §6, §7, §11,
y el tercer par de esta sección) salieron limpios. Las vías que quedan,
en orden de lo más al menos directo:

1. Acotar si `count=1395` es plausible (contar fuentes reales del
   sistema, `fc-list | wc -l` o equivalente) — barato, no intentado.
2. Repetir el mismo experimento con un `source` distinto o forzando
   `count` a un valor pequeño (parche temporal de diagnóstico, revertir
   después) para ver si el crash desaparece — confirmaría o refutaría
   que el volumen del loop es la variable relevante, no el contenido.
3. Instrumentar directamente alrededor de la llamada a `CB_ADDSTRING`
   con una verificación de heap acotada (no todo el proceso, solo el
   entorno inmediato de esa llamada) — algo que ni `+heap` ni `valgrind`
   lograron dar en esta investigación al mirar el proceso completo.
4. Considerar la hipótesis de un bug real de Wine (`wine-staging
   11.15`/GCC 16 en la ruta de `COMBOBOX_InsertString` o similar) y
   probar con otra versión de Wine en este mismo entorno Arch — no
   intentado, requeriría instalar un Wine distinto (cambio de sistema, a
   confirmar con el usuario antes de tocarlo, como ya se dejó pendiente
   para el VPS en §8).

**Build restaurado** después de revertir toda la instrumentación de esta
sección (tres tandas: canario del tercer par, traza de ramas, traza de
loop).

**Seguimiento — punto 1 de la lista de arriba, respondido:** en este
mismo entorno (hp-15, EndeavourOS), `fc-list | wc -l` da **2553** caras
de fuente instaladas, `fc-list : family | sort -u | wc -l` da **1965**
nombres de familia únicos. `count=1395` cae **dentro** de ese rango
(menor que ambos totales) — no es descabellado como reflejo de una
enumeración real de fuentes, a diferencia de lo que sería un valor
claramente imposible (negativo, `INT_MAX`, etc.). No cierra la pregunta
del todo: `fc-match "Courier New"` resuelve por sustitución
(`Liberation Mono`) pero `fc-list | grep -ic courier` da **0** — "Courier
New" (el nombre que `combo_contains` busca para clasificar el combo como
`source_font`, y que efectivamente aparece como dato real en el `for` de
§12) no es un nombre de familia instalado literal en este sistema, así
que el combo no está enumerando `fc-list` en crudo 1:1 — es la
enumeración GDI de Wine (`EnumFontFamilies` o equivalente), que puede
generar una entrada por cada combinación fuente×charset/script y por
tanto un total mayor o distinto al de `fc-list`. **Orden de magnitud
plausible, procedencia exacta sin confirmar.**

**Seguimiento — punto 2 de la lista de §12, respondido: refutado.**
Experimento de control: `count` interceptado justo después de
`SendMessageA(source, CB_GETCOUNT, 0, 0)` y forzado a `20` cuando supera
ese valor (`opus_win95_chrome.cpp`, instrumentación temporal revertida
después, diff confirmado limpio), dejando el resto de `sync_combo`
intacto — el loop de población corre de verdad, solo que 20 veces en
vez de 1395.

**5/5 corridas, el cap se aplicó** (log `capping count from 1395 to 20`
presente en las cinco) **y el crash ocurrió exactamente igual las cinco
veces**, mismas firmas de siempre (`free(): invalid next size`,
`free(): invalid pointer`, `malloc(): unaligned tcache chunk detected`).
**El volumen del loop no es la variable relevante** — con 20 iteraciones
en vez de 1395 el resultado es idéntico. Esto **descarta directamente**
la lectura de §12 de "corrupción acumulada dentro de la implementación
de Wine del ComboBox por volumen de llamadas repetidas a
`CB_ADDSTRING`" — si fuera una cuestión de volumen/desgaste acumulado,
capar a 20 debería haber cambiado algo (aunque sea la firma o la
frecuencia), y no cambió nada en absoluto.

**Lectura corregida:** la corrupción no depende de *cuántas* veces se
llama a `CB_ADDSTRING` en este loop — es consistente con que ya esté
presente **antes** de llegar aquí (el `free()` que aborta se dispara en
la primera oportunidad que tiene de encontrar la corrupción, sea esa
oportunidad la iteración 1 o la 1395 da igual), no con que este loop
específico la *cause* por acumulación. Esto reabre con más fuerza la
lectura alternativa que §5 (punto 3) y §12 ya dejaban abierta sin
perseguir: el origen real está en otro lado, y todo lo que esta
investigación viene instrumentando dentro de `sync_combo`/
`locate_source_combos`/`collect_original_combos` (ocho candidatos
refutados en total) puede ser enteramente inocente — el `free()` que
vemos abortar ahí es solo el primer punto de contacto con un heap ya
dañado por algo que corre **antes** en el arranque, no dentro de esta
cadena de funciones en absoluto.

**Consecuencia práctica:** parar de instrumentar código dentro de la
cadena `sync_combo`/`locate_source_combos` — con este resultado, seguir
ahí ya no tiene sustento. La vía que queda con más apoyo es acotar hacia
atrás: qué corre *antes* de la primera llamada a `sync_mirrors` en el
arranque (creación de ventanas/controles del toolbar, `WM_CREATE` de
`toolbar_window_proc`, o más atrás aún) — no perseguido todavía.

**Build restaurado** después de revertir esta instrumentación.

### 13. Qué corre antes de `sync_mirrors` — bisección de `WM_CREATE`: **el propio `WM_CREATE`, incluida su primera llamada a `sync_mirrors`, sale limpio**

Retoma directamente el punto pendiente de §12 ("acotar hacia atrás qué
corre antes de la primera llamada a `sync_mirrors`"). `sync_mirrors` se
llama desde dos sitios en `toolbar_window_proc`: una vez dentro de
`WM_CREATE` (línea 2590, incondicional, al crear el toolbar), y otra vez
dentro de `WM_TIMER` (línea 2602, cada 350 ms vía `SetTimer(window,
kSyncTimer, 350, nullptr)`, puesto al final del propio `WM_CREATE`).
Hasta esta sesión no estaba claro cuál de las dos llamadas es la que
llega a la iteración con `count=1395` de §12 — ambas rutas pasan por el
mismo `sync_mirrors`.

**Instrumentación temporal** (`opus_win95_chrome.cpp`, revertida
después, diff confirmado limpio): un checkpoint `fprintf` después de
cada paso significativo del cuerpo de `WM_CREATE` — asignación de
`ToolbarState`, `LoadImageW` del sprite, `CreateFontIndirectW`, cada uno
de los cuatro `create_combo`/`create_zoom_combo`, los `SetWindowTextW`
iniciales, los `WM_SETFONT`, `position_combos`, y por último la llamada
a `sync_mirrors` en sí (antes y después).

**Resultado, 5/5 corridas: la secuencia completa de checkpoints
imprime siempre, incluido `sync_mirrors returned ok` como última línea
antes del crash.** Es decir: **todo el cuerpo de `WM_CREATE` —
asignación de estado, carga de sprite, creación de fuente, creación de
los cuatro combos, textos iniciales, aplicación de fuente,
posicionamiento, y la primera llamada completa a `sync_mirrors` —
termina sin ningún problema en las cinco corridas.** El crash ocurre
**después** de que `WM_CREATE` retorna `0`.

**Reconciliación con §12 — no es una contradicción, es una
localización más precisa:** en la primera llamada a `sync_mirrors`
(dentro de `WM_CREATE`), `locate_source_combos` todavía no encuentra
ningún combo real (§11: "primera llamada... vacía") — las tres llamadas
a `sync_combo` dentro de esa primera pasada hacen `early-return` de
inmediato (`source == nullptr`) y no llegan ni cerca del loop de
`CB_ADDSTRING`. La iteración con `count=1395` que crashea en §12
**tiene que ser la de la segunda llamada a `sync_mirrors`, disparada por
`WM_TIMER` 350 ms después** — es la única otra ruta que existe hacia
`sync_combo`, y es coherente con que `locate_source_combos` sí encuentre
los tres combos reales en su segunda invocación (también documentado en
§11).

**Consecuencia:** la ventana de interés ya no es "todo lo que corre
antes de `sync_mirrors`" en abstracto — es específicamente **lo que
ocurre entre que `WM_CREATE` retorna (toolbar recién creado, ventana
visible) y que el temporizador de 350 ms dispara la segunda llamada a
`sync_mirrors`**. En ese intervalo el bucle de mensajes de Wine sigue
bombeando: pueden estar creándose otras ventanas (el pane del documento,
otros controles), procesándose `WM_PAINT`/`WM_SIZE`, o corriendo
cualquier otro código de arranque de `WORD1` en paralelo — nada de eso
se ha instrumentado todavía. Combinado con el resultado de §12 (el
volumen del loop de `sync_combo` no es la variable relevante), la
lectura más consistente con toda la evidencia acumulada sigue siendo:
la corrupción no se origina dentro de la cadena `toolbar_window_proc`→
`sync_mirrors`→`sync_combo`→`locate_source_combos`→
`collect_original_combos` en absoluto (nueve candidatos refutados ahí
entre todas las sesiones) — se origina en otro código que corre en
paralelo durante ese intervalo de 350 ms, y el primer `free()` de
`sync_combo` es solo el primer punto de contacto con el daño.

**No perseguido esta sesión:** instrumentar el propio `WM_TIMER` (log
antes/después de su llamada a `sync_mirrors`, para confirmar
directamente que es esa la que crashea y no una tercera invocación no
contemplada) y/o instrumentar qué otras ventanas/paneles se crean o qué
mensajes se procesan en el intervalo de 350 ms entre `WM_CREATE` y el
primer `WM_TIMER`.

**Build restaurado** después de revertir esta instrumentación.

### 14. `WM_TIMER` instrumentado — confirmado: crashea en el `tick#1`, en la propia llamada a `sync_mirrors`, antes de llegar a nada más

Cierra directamente la pregunta que dejó abierta §13. Instrumentación
temporal (`opus_win95_chrome.cpp`, revertida después, diff confirmado
limpio): checkpoints en cada paso del cuerpo de `WM_TIMER` — contador de
ticks, valores de `suppress_sync_until`/`GetTickCount64()`, antes/después
de `sync_mirrors`, antes/después de `subclass_all_document_panes`, y de
las tres ramas condicionales (refresco de regla, arranque de vista de
página, pintado de regla horizontal).

**Resultado, 5/5 corridas: el crash ocurre en el primerísimo `tick#1`**
(`suppress_sync_until=0`, así que la condición nunca suprime nada),
**inmediatamente después de "calling sync_mirrors" y antes de
"sync_mirrors returned ok"** — es decir, dentro de esa llamada, nunca
llega a retornar. **Ninguna de las cinco corridas alcanza
`subclass_all_document_panes` ni ninguna de las ramas posteriores** —
el crash ocurre antes de que el código tenga oportunidad de ejecutarlas.

**Confirma sin ambigüedad la hipótesis de §13:** es la segunda llamada a
`sync_mirrors` (la de `WM_TIMER`, no la de `WM_CREATE`) la que llega al
`count=1395` de §12 y crashea — no hay una tercera ruta ni nada más
dentro del propio `WM_TIMER` involucrado. `subclass_all_document_panes`
y el resto del cuerpo de `WM_TIMER` quedan **descartados como
candidatos** por esta misma razón: nunca se ejecutan antes del crash en
ninguna corrida.

**Consecuencia — precisa la ventana de búsqueda que quedaba abierta:**
como `WM_TIMER` llama a `sync_mirrors` como su primer paso, nada del
propio manejador de `WM_TIMER` corre "en paralelo" antes del crash. La
ventana de interés real vuelve a ser, como en §13, **el intervalo de
~350 ms entre que `WM_CREATE` retorna y que este primer `WM_TIMER`
llega** — código que corre por fuera de `toolbar_window_proc` por
completo (bombeo de mensajes de Wine, creación de otras ventanas/panes,
u otro código de arranque de `WORD1`), no instrumentado todavía por
esta investigación. Con esto, los diez candidatos dentro de la cadena
`toolbar_window_proc`/`sync_mirrors`/`sync_combo`/
`locate_source_combos`/`collect_original_combos` quedan agotados sin
excepción — cualquier paso siguiente que quiera seguir la pista del
heap corrupto necesita mirar fuera de esta cadena de funciones.

**Build restaurado** después de revertir esta instrumentación.

### 15. Backtrace real de la llamada que crashea — no es el `WM_TIMER` de 350 ms: es una llamada síncrona desde `FCreateMw`; corrige §14

Retoma el punto pendiente de §14 ("instrumentar qué otras ventanas/paneles
se crean o qué mensajes se procesan en el intervalo de 350 ms"). Antes de
instrumentar ese intervalo se intentó primero la vía más directa —
breakpoint en la propia `sync_mirrors` con `gdb` para capturar el
backtrace real en el momento de la llamada que crashea — y esa vía reveló
que la premisa de §13/§14 (que hay dos llamadas separadas por ~350 ms
gobernadas por `SetTimer`) es incorrecta.

**Bloqueador nuevo, no visto en sesiones anteriores: los breakpoints por
archivo:línea dejaron de resolver en esta sesión.** `break
opus_win95_chrome.cpp:916` (`sync_mirrors`) y `break
opus_win95_chrome.cpp:2829` (`OpusCreateWin95Chrome`, `extern "C"`, no en
namespace anónimo — descarta la hipótesis de que fuera un problema de
visibilidad de símbolo) quedan `<PENDING>` para siempre en 5 corridas
distintas bajo `gdb -x script --args wine WORD1.exe.so` (`set breakpoint
pending on`), incluso con las dos combinadas en el mismo script. `info
sharedlibrary` en el momento exacto del `SIGABRT` reporta **"No shared
libraries loaded at this time"** — ni siquiera `libc.so.6` aparece
registrado, con el proceso corriendo código real. Reproducido también con
un comando trivial no relacionado (`break main` sobre `wine cmd /c "echo
hi"`): mismo resultado, `<PENDING>` eterno, cero shared libraries. Es una
regresión genérica de integración `gdb`↔`wine` en esta sesión concreta —
no específica de `WORD1.exe.so` ni de namespaces anónimos —, que contradice
directamente lo que §12.1/§12.2 documentaron funcionando en un entorno
descrito como el mismo (EndeavourOS, wine-staging 11.15, `gdb` 17.2). No
se investigó la causa de la regresión (podría ser una actualización de
paquete entre sesiones). Adicionalmente, `gdb -p <PID>` sobre un proceso
ya lanzado falla con `ptrace: Operación no permitida` —
`/proc/sys/kernel/yama/ptrace_scope` vale `1` en este entorno, bloqueo de
Yama independiente del anterior; no se tocó (requeriría privilegios y
autorización explícita, fuera de alcance).

**Técnica alternativa usada — sin gdb, sin tocar `src/Opus/`:**
instrumentación temporal en `opus_win95_chrome.cpp` (namespace anónimo, en
`src/port/`, sin restricción) con `backtrace()` de `<execinfo.h>` (glibc,
disponible porque este archivo compila como C++ nativo bajo `winegcc`, no
tiene nada específico de MSVC) en tres puntos: antes de la llamada a
`sync_mirrors` dentro de `WM_CREATE` (línea 2590), antes de la llamada
dentro de `WM_TIMER` (línea 2602), y — la que resultó decisiva — al
principio de `OpusSyncWin95Toolbar()` (línea 2802), que hace
`SendMessageW(vhwndWin95Toolbar, WM_TIMER, kSyncTimer, 0)` de forma
**síncrona y directa**, sin pasar por ninguna cola de mensajes ni por
`SetTimer`. Cada captura imprime también `GetTickCount64()` y la dirección
de la propia función de diagnóstico (`&ChromeTraceDiag`) como referencia
para poder restar el ASLR de cada corrida y resolver las direcciones
crudas contra el binario estático con `addr2line -e WORD1.exe.so -f -C`
(la misma técnica de §3, aplicada ahora de forma proactiva en vez de sobre
un core ya corrupto). Instrumentación revertida después
(`git checkout -- src/port/original/opus_win95_chrome.cpp`, diff limpio
confirmado) y build restaurado.

**Resultado, 3/3 corridas, mismo patrón exacto:**

```
[CHROME TRACE] WM_CREATE before sync_mirrors                           tick=T
[CHROME TRACE] OpusSyncWin95Toolbar direct call (init2.c) before ...   tick=T+~260..282ms
[CHROME TRACE] WM_TIMER before sync_mirrors                            tick=T+~260..282ms   <-- MISMO tick, al milisegundo
free(): invalid pointer   (o double free or corruption (!prev))
```

El marcador puesto dentro de `OpusSyncWin95Toolbar` y el marcador puesto
dentro del `case WM_TIMER` de `toolbar_window_proc` imprimen **el mismo
`GetTickCount64()` exacto** las 3 veces — no hay forma de que sean dos
eventos distintos separados por trabajo real; es la misma llamada vista
desde dos puntos de instrumentación. Además el intervalo real medido
(~260-282 ms) es **menor que los 350 ms** del `SetTimer` — un `WM_TIMER`
genuino de cola nunca dispara antes del intervalo pedido, lo que ya era
indicio de que no era ese el mecanismo. El `SetTimer(window, kSyncTimer,
350, ...)` sigue vivo y armado, pero **el proceso muere antes de que
tenga oportunidad de disparar ni una sola vez** — el "tick#1" que §14
identificó y atribuyó al timer real nunca fue el timer real.

**La captura dentro de `OpusSyncWin95Toolbar` desenrolla 13 frames**
completos (a diferencia de las capturas en `WM_CREATE`/`WM_TIMER`, que se
cortan en 3 — el mismo límite de unwind por CFI roto en la frontera
ABI Unix/PE de Wine que ya bloqueaba a `gdb` en §3; aquí no aplica porque
`OpusSyncWin95Toolbar` se alcanza por una llamada a función C directa, sin
cruzar esa frontera hasta el frame final). Resuelta con `addr2line` contra
el offset real de cada corrida (base = dirección en vivo de
`ChromeTraceDiag` menos su dirección estática, `nm -C WORD1.exe.so`):

```
__wine_spec_exe_wentry
wmain
wWinMain                    opus_original_startup_probe.cpp:512
OpusOriginalWinMain         wproc.c:516
FInitWinInfo                wproc.c:775
FInitPart2                  init2.c:659        (ElNewFile(stType, fFalse) — documento nuevo sin título)
ElNewFile                   open.c:1353
FCreateMw                   open.c:595         (creando la ventana del documento — TODAVÍA NO shown)
EndStartup1                 open.c:591         (llamada directa, no vía el wrapper EndStartup() de init2.c:703)
DisplayRibbonInit           init2.c:817
OpusSyncWin95Toolbar        opus_win95_chrome.cpp:2803
ChromeTraceDiag             (sonda de esta sesión)
```

**Localización exacta del sitio de llamada, confirmada leyendo
`open.c:584-600`:** dentro de `FCreateMw`, bajo el comentario `/* BEGIN
VISUAL DISPLAY OF WINDOW */`, hay dos llamadas separadas —
`EndStartup1()` en la línea 591 (si `vhwndStartup != NULL`) y
`EndStartup2()` en la línea 639 — con la creación/exhibición real de la
ventana del documento **entre medio** (`ShowWindow(hwndMw, ...)` en la
línea 600 es *posterior* a `EndStartup1()`). Es decir: `sync_mirrors`
crashea mientras `FCreateMw` todavía está construyendo la primera ventana
de documento del arranque — **antes de que esa ventana llegue a mostrarse
en pantalla** —, disparado por el efecto colateral de
`EndStartup1()`→`DisplayRibbonInit()`→`OpusSyncWin95Toolbar()` que existe
específicamente para ocultar la ribbon clásica y sincronizar la toolbar
Win95 en cuanto termina el splash de arranque.

**Corrección concreta sobre §13/§14:** no hay una "segunda llamada a
`sync_mirrors` vía `WM_TIMER` 350 ms después" — hay una **única llamada
adicional síncrona**, disparada por `SendMessageW` (no por el temporizador
real), que llega recursivamente por una ruta de llamadas de función C
normal (`FInitPart2`→`ElNewFile`→`FCreateMw`→`EndStartup1`→
`DisplayRibbonInit`→`OpusSyncWin95Toolbar`), completamente dentro de
`FInitPart2` y sin pasar nunca por `GetMessage`/`DispatchMessage` de tope.
La lectura de §13 ("la ventana de interés es el intervalo de ~350 ms en
que el bombeo de mensajes de Wine sigue corriendo") queda refutada: no
hay bombeo de mensajes de por medio en absoluto entre las dos llamadas a
`sync_mirrors` que sí ocurren (la de `WM_CREATE`, vacía, y esta), es una
única rama de ejecución síncrona del propio hilo principal.

**Consecuencia — foco preciso para la próxima sesión:** el candidato ya
no es "código no identificado corriendo en paralelo" — es la construcción
de la primera ventana de documento en sí. `FCreateMw` (`open.c`, entre la
Scribble `'D'`/`'E'` de la línea 566 y el punto de la línea 591) construye
estado (documento, `ww` activo, `hmwd`, controles) que **todavía no está
terminado** cuando `EndStartup1` dispara `sync_mirrors`/`sync_combo` sobre
los combos de la toolbar — y `locate_source_combos` sí encuentra combos
reales en este punto (a diferencia de la primera llamada, vacía, dentro
de `WM_CREATE` — §13), lo que sugiere que los controles "fuente" que la
toolbar espejea ya existen para entonces pero el documento/ventana que los
respalda puede no estarlo. Siguiente paso obvio, no intentado esta sesión:
instrumentar qué controles concretos son `state->source_style` /
`source_font` / `source_size` en el momento del crash (ya se sabe que
`locate_source_combos` los encuentra — §12; falta identificar *cuáles*
`HWND` son y si pertenecen a la ventana de documento a medio construir) y/o
revisar qué mutación de heap ocurre en el tramo de `FCreateMw` entre la
línea 566 y la 591 que podría dejar una estructura compartida en estado
inconsistente para el `free()` posterior dentro de `sync_combo`.

**No perseguido esta sesión:** diagnosticar la regresión de `gdb`
documentada arriba (bloquea seguir usando breakpoints interactivos hasta
resolverse); confirmar si la llamada `EndStartup1()`/`DisplayRibbonInit()`
de `open.c:591` es *siempre* la que crashea o si en alguna corrida el
`SetTimer` real llega a disparar primero (no observado en 3/3, pero
tampoco es imposible dado que el margen es de solo ~70-90 ms).

### 16. Identidad concreta de los HWND source_style/source_font/source_size — el crash es siempre en `sync_combo(font)`, y `source_style` nunca llega a intentarse

Retoma el "siguiente paso obvio" que cerraba §15: instrumentar qué HWNDs
concretos resuelven `state.source_style`/`source_font`/`source_size` en el
momento del crash, y si pertenecen a la ventana de documento que `FCreateMw`
todavía está construyendo.

**Técnica:** misma familia que §15 — instrumentación temporal en
`opus_win95_chrome.cpp` (namespace anónimo, `src/port/`, revertida al
terminar, build limpio confirmado antes y después). Esta vez con
`fprintf(stderr, ...)` + `fflush` (no `backtrace()`) en tres puntos:
`ChromeDumpHwndOwnership(label, hwnd)` — nueva función helper que vuelca
`HWND`, `IsWindow`, `IsWindowVisible`, `GetClassNameW`, `GetWindowTextW`,
`GetWindowRect` y sube la cadena de `GetParent` hasta la raíz (máx. 8
niveles) — llamada desde dentro de `locate_source_combos` (para cada
candidato encontrado y para el resultado final de cada uno de los tres
roles) y desde dentro de `sync_mirrors` (justo antes de cada una de las tres
llamadas a `sync_combo`); y una segunda ronda con instrumentación adicional
directamente dentro de `sync_combo` — vuelca el `mirror`, imprime
`count`/`copied_count`, y añade una línea por cada iteración del bucle
`CB_ADDSTRING` con el índice y el string ANSI real a punto de copiarse.

**Gotcha nuevo, no documentado antes en este archivo:** `wchar_t` bajo
winelib se compila a 2 bytes (`-fshort-wchar`, para calzar con `WCHAR` de
Win32), pero el CRT de este `.cpp` es el glibc nativo del sistema, cuyo
`printf`/`fprintf` con `%ls` asume `wchar_t` de 4 bytes. Volcar un buffer
`WCHAR` directamente con `%ls` produce basura (caracteres de cuadro
ilegibles) — no es corrupción de datos, es un desalineamiento de ancho de
tipo en el propio volcado. **Solución:** convertir con `WideCharToMultiByte`
a ANSI antes de imprimir con `%s` (mismo patrón que la función
`ansi_from_wide` que ya existe en este archivo, solo que aplicado en un
helper local porque `ChromeDumpHwndOwnership` se sitúa antes en el archivo
que `ansi_from_wide`). Cualquier instrumentación futura en código winelib
que imprima texto ancho debe pasar por esta conversión.

**Resultado, 4/4 corridas (`locate_source_combos`) + 3/3 corridas detalladas
(`sync_combo`), patrón idéntico:**

En la primera llamada a `sync_mirrors` (dentro de `WM_CREATE` del propio
toolbar) `locate_source_combos` encuentra **0 candidatos** — consistente con
§13 — y los tres roles quedan `null`; los tres `sync_combo` no hacen nada
(guard `source == nullptr`).

En la segunda llamada (la síncrona identificada en §15, vía
`FCreateMw`→`EndStartup1`→`DisplayRibbonInit`→`OpusSyncWin95Toolbar`),
`locate_source_combos` encuentra siempre exactamente **3 candidatos** con
clase `ComboBox`, con esta identidad estable entre corridas (solo cambian
los valores exactos de `HWND`, no la estructura):

| Candidato | rect | visible | cadena de padres | clasificado como |
|---|---|---|---|---|
| A | (52,50,204,71) | sí | `OpusSdmDialog` → `OpusApp("Microsoft Word")` | `source_font` (contiene "Courier New" y "Arial") |
| B | (254,50,310,71) | sí | `OpusSdmDialog` → `OpusApp` | `source_size` (contiene "24" y "72") |
| C | (6196,113,6348,134) | **no** | `OpusSdmDialog` → `OpusMwd` → `OpusDesk` → `OpusApp` | **ninguno** — `source_style` queda `null` |

**A y B son controles legítimos y ya poblados** de la ribbon clásica
(`OpusSdmDialog`, un diálogo hijo directo de `OpusApp`) — nada que ver con
la ventana de documento a medio construir. La hipótesis de cierre de §15
("los `source_*` podrían pertenecer a la ventana de documento todavía sin
terminar") **no se confirma para A/B**.

**C sí encaja con esa hipótesis** — está fuera de pantalla
(`rect.left = 6196`, un valor de posicionamiento típico de "todavía no
colocada"), invisible, y cuelga de una cadena de clases distinta y más
profunda (`OpusMwd`/`OpusDesk`) que no aparece en A/B — casi con certeza es
un control propio de la ventana de documento (`hwndMw`/`ww`) que `FCreateMw`
está construyendo en ese instante. Pero **C nunca se clasifica como
`source_style`** en ninguna de las 4 corridas — el `else if` de
`locate_source_combos` exige `combo_contains(combo, "Normal")` o
`CB_GETCOUNT > 0`, y C no cumple ninguna de las dos en este punto exacto
(consistente con ser un combo recién creado, sin ítems todavía). Por tanto
`state.source_style` queda `null` las 4 veces, y el primer `sync_combo`
(estilo) es un no-op seguro. **C es un testigo de que la ventana de
documento está a medio construir, pero no es la causa del crash.**

**El crash es siempre dentro de `sync_combo(mirror=state.font_combo,
source=state.source_font)`** — nunca en estilo (no-op, arriba) ni llega a
alcanzar tamaño (el flujo muere en fuente antes). El `mirror`
(`state.font_combo`) se confirma válido, visible, hijo de
`OpusWin95Toolbar`/`OpusApp`, y ya con texto `"Arial"` (fijado en su propio
`WM_CREATE`) — no es un handle obsoleto ni reciclado. El `source`
(candidato A) reporta **`count=1395`**, coincidiendo exactamente con el
hallazgo independiente de una sesión anterior (`fc-list` en este mismo
entorno, commit `7185ce1`) — confirma que A es la fuente real de fuentes
del sistema vía `fc-list`, y que ese hallazgo y este apuntan al mismo
control.

**El índice del bucle `CB_ADDSTRING` donde crashea, con datos reales
impresos por iteración:** las 3 corridas detalladas mueren en la vecindad
de **idx=14/1395 ("DejaVu Sans Light")** o **idx=15/1395 ("DejaVu Sans
Mono")** — nunca antes, nunca después, en 3/3. El mensaje de glibc varía
entre corridas (`free(): invalid pointer`, `free(): invalid next size
(normal)`, `double free or corruption (!prev)`) — los tres son detectores
distintos del mismo tipo de daño (metadata de heap corrupta), no evidencia
de tres bugs distintos.

**Lectura de esta franja estrecha (14-15 de 1395), no perseguida más allá de
anotarla:** un índice que varía por ±1 entre corridas pero se mantiene en
una banda tan angosta —en vez de disperso a lo largo de las 1395
iteraciones, o fijo siempre en 0— es la firma típica de una corrupción de
heap **detectada tarde**: el `free()`/`malloc()` que realmente escribe fuera
de límites puede haber ocurrido bastante antes (incluso fuera de
`sync_combo`, quizás en la propia `locate_source_combos` recorriendo 1395
`combo_contains`/`CB_FINDSTRINGEXACT`, o en código anterior en la cadena
`FCreateMw`→...→`OpusSyncWin95Toolbar`), dejando un chunk con metadata
inconsistente; el bucle de `CB_ADDSTRING`/`combo_item`/`wide_from_ansi` de
`sync_combo` simplemente hace suficiente churn de allocate/free como para
que, tras el mismo número aproximado de operaciones cada vez, el
allocator reutilice o intente consolidar ese chunk ya dañado y el
detector de glibc dispare. Esto **no descarta** que el bug esté en
`sync_combo` mismo, pero sí abre una hipótesis concreta que la próxima
sesión no ha probado: usar un detector que atrape la escritura real en el
momento en que ocurre (`valgrind` — ya disponible en este entorno, versión
3.25.1 según sesiones previas — o `MALLOC_CHECK_=3`/`mallopt` con chequeo
más agresivo) en vez de esperar a que glibc lo detecte tarde por
casualidad de índice.

**Consecuencia — foco para la próxima sesión:** (1) correr bajo `valgrind`
(no intentado en ninguna sesión de esta serie hasta ahora pese a estar
disponible) para localizar la escritura real fuera de límites, en vez de
seguir leyendo el punto de detección tardía de glibc; (2) si valgrind no es
viable por la superposición Winelib/Wine (posible, dado el historial de
fricciones de herramientas en este proyecto — ver el bloqueo de `gdb` en
§15), instrumentar con la misma técnica de esta sección el tramo
`locate_source_combos` en sí (1395 `combo_contains` llaman a
`SendMessageA(..., CB_FINDSTRINGEXACT, ...)` sobre el mismo combo de 1395
ítems, dos veces por candidato — volumen de trabajo comparable al bucle que
sí se instrumentó) para descartar que la corrupción ya haya ocurrido ahí,
antes de que `sync_combo` la "descubra"; (3) confirmar la identidad de C
(candidato descartado, `OpusMwd`/`OpusDesk`) contra el código de `FCreateMw`
en `open.c` — se sospecha que es el combo de estilos de la ventana de
documento nueva, pero no se confirmó contra el código fuente esta sesión.

**No perseguido esta sesión:** correr bajo `valgrind` (mencionado arriba
como plan, no ejecutado); confirmar C contra `open.c`; determinar si la
franja 14-15 se mantiene bajo un juego de fuentes del sistema distinto
(dependería de qué haya entre `fc-list` y estas posiciones, así que no es
necesariamente estable entre máquinas).

### 17. `valgrind` — tres intentos, ninguno alcanza el crash; conclusión: no viable en este entorno sin más trabajo de infraestructura

Retoma el plan que cerraba §16: correr bajo `valgrind` (3.25.1, ya disponible
en este entorno) para atrapar la escritura real fuera de límites en vez de
seguir leyendo la detección tardía de glibc.

**Intento 1 — sin mitigaciones, `--track-origins=yes`, timeout 240s:**
`valgrind --tool=memcheck --track-origins=yes --trace-children=yes
--error-exitcode=99 wine WORD1.exe.so`. Resultado: **timeout, no llega a
crashear.** El log más grande (proceso `WORD1.exe.so` real, distinguible por
`Command:` en el log) muestra que en 240s reales solo avanzó hasta
`NtQueryDirectoryFile`/registro (arranque de `wineboot`/`explorer`), sin
llegar siquiera a crear la ventana de la aplicación. La carga de
`libnvidia-glcore`/`libGLX_nvidia`/`libEGL_nvidia` (el driver propietario de
la GPU de este equipo — ver `CLAUDE.md`, GTX 1050 Max-Q) bajo instrumentación
genera **93.442 allocs / 49.463 frees** solo en sus constructores de carga,
dominando el tiempo disponible. Los 6 "Invalid write/read" que sí aparecieron
apuntan todos a `Address 0x... is on thread 1's stack`, originados en
`__wine_syscall_dispatcher` — el trampolín de cambio de stack Unix↔PE de Wine
en su despachador de syscalls, un falso positivo de `valgrind` frente a Wine
**ampliamente documentado** (Wine cambia el puntero de pila a mano al cruzar
esa frontera; `valgrind` no lo entiende y lo marca como acceso inválido).
No hay archivo de supresiones de Wine instalado en este sistema
(`find / -iname "*wine*.supp"` → nada; el paquete `wine-staging` de este
Arch no lo incluye) para filtrar este ruido.

**Intento 2 — forzando el vendor EGL a mesa para evitar la carga del blob
nvidia:** `__EGL_VENDOR_LIBRARY_FILENAMES=.../50_mesa.json
LIBGL_ALWAYS_SOFTWARE=1`, `--leak-check=no` (menos overhead), timeout 1100s
en background. Resultado: **mucho más rápido** — en ~2 minutos reales llegó
hasta código real de creación de ventana/menú (`NtUserCreateWindowEx`,
`calc_menu_bar_size`, `DrawTextW` — muy por delante de donde llegó el intento
1) — pero murió con un fallo **distinto y ajeno al bug perseguido**:
`nodrv_CreateWindow` — *"Application tried to create a window, but no driver
could be loaded"* / *"The explorer process failed to start"* — seguido del
propio manejo de error de Opus, `Win32 error 1400` en `init2.c:324`. Forzar
el vendor EGL a mesa rompió la carga de `winex11.drv` antes de que Word
llegara a la ventana de documento — un problema de entorno nuevo, inducido
por la propia mitigación, no relacionado con `sync_combo`.

**Intento 3 — solo `LIBGL_ALWAYS_SOFTWARE=1`, sin forzar el vendor EGL
(para aislar si el problema del intento 2 era el override de vendor o
otra cosa):** mismo resultado — **el mismo `nodrv_CreateWindow`**, esta vez
en menos de 90s reales (no timeout; el proceso murió solo, con un código de
salida 137 sin explicación clara — no hay evidencia de OOM en `dmesg`/dmesg
del kernel, memoria disponible de sobra según `free -h`). Que el fallo se
repita **sin** el override de vendor EGL refuta que ese override fuera la
causa; el sospechoso más plausible es que el intento 1, al morir por
`timeout` (`SIGTERM`) a mitad de la inicialización de `wineboot`/`explorer`,
dejó el `WINEPREFIX` compartido en un estado a medio escribir (registro,
locks, estado de `explorer.exe`) que los intentos 2 y 3 heredaron —
**no verificado**, solo la explicación más consistente con la evidencia
disponible.

**Conclusión de esta sesión: `valgrind` no es viable aquí sin trabajo de
infraestructura adicional, no intentado.** Tres corridas, cero alcanzaron el
punto de crash real (`sync_combo`/`CB_ADDSTRING`). Los únicos "Invalid
write/read" observados son el falso positivo conocido de
`__wine_syscall_dispatcher`. Para que este camino sea viable haría falta,
como mínimo: (1) un `WINEPREFIX` dedicado y desechable para corridas de
`valgrind` (para que un intento matado a mitad de camino no envenene el
siguiente); (2) el archivo de supresiones oficial de Wine
(`tools/valgrind/wine.supp` en el árbol fuente de Wine — no viene empaquetado
en este Arch, habría que extraerlo del código fuente de wine-staging 11.15 o
construirlo a mano) para eliminar el ruido de `__wine_syscall_dispatcher`;
(3) entender por qué evitar el blob nvidia rompe la carga del driver de
ventana — no se investigó si es el propio `LIBGL_ALWAYS_SOFTWARE`, el
`WINEPREFIX` contaminado, o alguna otra interacción.

**Recomendación para la próxima sesión, dado el costo ya incurrido:** no
insistir con `valgrind` como primer paso. La técnica de instrumentación
manual de §15/§16 (`fprintf`/`backtrace()` + `addr2line`, sin `gdb`, sin
`valgrind`) ya localizó el bug con precisión razonable (`sync_combo`,
bucle `CB_ADDSTRING`, franja de índice 14-15 de 1395) en corridas nativas de
menos de un segundo. Más rendimiento por el mismo esfuerzo probablemente
venga de (a) auditar a mano el código de `sync_combo`/`combo_item` y lo que
hace Wine internamente en su implementación *builtin* de `COMBOBOX`/listbox
alrededor de `CB_ADDSTRING`/`CB_RESETCONTENT` en ese rango de operaciones, o
(b) si se retoma `valgrind`, hacerlo solo después de resolver (1) y (2)
arriba, no como exploración rápida.

**No perseguido esta sesión:** crear un `WINEPREFIX` dedicado para
`valgrind` (cambio de infraestructura más grande, no intentado sin acordarlo
antes); extraer/generar el archivo de supresiones de Wine; diagnosticar el
código de salida 137 del intento 3; probar `AddressSanitizer` como
alternativa más liviana a `valgrind` (mencionado como idea, no evaluado —
incierto si winegcc/Wine toleran bien el modelo de shadow memory de ASan).

### 18. `valgrind` — infraestructura completa construida, conclusión definitiva: incompatible con este `winex11.drv` en este entorno, no es un problema de tuning

Instrucción explícita: "apply whatever is necessary to make valgrind
available and continue". Se construyó la infraestructura que faltaba y se
hicieron 2 intentos más (4 y 5) con ella, más una corrida de control nativa
que aísla la causa real.

**Infraestructura construida:**
- **`WINEPREFIX` desechable, pre-calentado fuera de `valgrind`:** `wineboot
  --init` corrido nativamente (sin `valgrind`) en un prefijo nuevo, luego
  snapshot vía `tar` a un `.tar.gz` "pristine" restaurable en segundos. Esto
  elimina de la corrida bajo `valgrind` toda la fase lenta de arranque
  (`wineboot`/registro/`explorer`) que dominaba el intento 1 de §17.
- **Archivo de supresiones real de Wine:** no viene empaquetado en este Arch
  (confirmado, §17); obtenido de
  [`austin987/wine-valgrind-scripts`](https://github.com/austin987/wine-valgrind-scripts)
  (mantenedor de Wine, colección de supresiones basada en los scripts
  originales de Dan Kegel) — `valgrind-suppressions-external` (ruido de
  drivers nvidia/mesa, glibc, etc.) + `valgrind-suppressions-ignore`
  (comportamiento intencional de Wine), combinados en un solo archivo.
- **`--vex-iropt-register-updates=allregs-at-mem-access`:** el flag que el
  propio `vg-wrapper.sh` de esa colección usa para correr Wine bajo
  `valgrind` — atenúa falsos positivos por el modelo de actualización de
  registros de VEX (el motor de instrumentación de `valgrind`) frente a los
  cambios de contexto manuales de Wine.
- **Supresión propia adicional**, escrita a mano a partir de la evidencia
  directa de las 3 corridas de §17 (no del archivo externo, que probablemente
  es anterior a este nombre de función): 5 entradas `Memcheck:Addr{1,2,4,8,16}`
  con `fun:__wine_syscall_dispatcher` — el trampolín exacto que producía
  el 100% de los falsos positivos observados hasta ahora.

**Intento 4** (prefijo pre-calentado nuevo, `LIBGL_ALWAYS_SOFTWARE=1`, toda
la infraestructura anterior): **mismo `nodrv_CreateWindow` que en los
intentos 2/3 de §17**, esta vez con un prefijo genuinamente nunca tocado por
`valgrind` antes — **refuta la hipótesis de §17** de que un `WINEPREFIX`
contaminado por el intento 1 (matado a mitad de arranque) fuera la causa.

**Intento 5** (mismo prefijo pristine restaurado de nuevo, **sin ningún
override de GL/EGL** esta vez — para aislar si el propio
`LIBGL_ALWAYS_SOFTWARE`/override de vendor era la causa real —, Xvfb
reiniciado con `+iglx` por si el rechazo de contextos GLX indirectos por
defecto en Xvfb importaba): **el mismo `nodrv_CreateWindow` otra vez**, en
menos de 3 minutos reales. Esto descarta tanto el override de vendor EGL
como `LIBGL_ALWAYS_SOFTWARE` como causa — ninguno de los dos estaba presente
esta vez.

**Corrida de control decisiva:** el mismo prefijo pristine restaurado una
vez más, mismo Xvfb (`+iglx`), pero **sin `valgrind`** — `wine
WORD1.exe.so` directo. Resultado: **llega limpio al crash real conocido**
(`elf_search_auxv can't find symbol in module` — el manejador de crash de
Wine intentando symbolizar tras el `SIGABRT` de `sync_combo`, la misma firma
de §15/§16). Es decir: **con exactamente el mismo prefijo y el mismo Xvfb,
quitar `valgrind` es lo único que hace falta para que la creación de ventana
funcione.**

**Conclusión definitiva de esta serie de intentos (5 en total entre §17 y
este apartado): no es un problema de configuración, supresiones, prefijo ni
variables de entorno de GL — `valgrind` en sí mismo rompe la carga de
`winex11.drv` en la combinación wine-staging 11.15 / Xvfb / `valgrind`
3.25.1 de este entorno.** La causa raíz exacta no se investigó más allá de
aislar que es `valgrind` el factor (no se probó, por ejemplo, si es
específicamente la extensión `MIT-SHM` de X11 — un punto de fricción
conocido e independiente entre `valgrind` y memoria compartida de X, y una
hipótesis razonable dado que `winex11.drv` típicamente usa `XShm` al
inicializar — ni si un servidor X real en vez de `Xvfb` cambia el
resultado).

**Esto cierra el camino de `valgrind` para esta investigación, con
evidencia sólida en vez de una sospecha.** La recomendación de §17 se
mantiene, ahora con más peso: seguir con la instrumentación manual nativa de
§15/§16 (ya localizó el bug en `sync_combo`/`CB_ADDSTRING`, franja de índice
14-15 de 1395, en corridas de menos de un segundo, sin ninguna fricción de
herramientas). Si en el futuro se quiere retomar `valgrind`, el punto de
partida ya no es "arréglalo" sino una pregunta más específica y acotada:
¿por qué exactamente rompe `winex11.drv` — `XShm`, el modelo de hilos de
Wine, u otra cosa? — antes de invertir más tiempo.

**No perseguido esta sesión:** aislar `MIT-SHM` como causa (probar
`Xvfb ... -extension MIT-SHM` o equivalente); probar contra un servidor X
real; AddressSanitizer como alternativa (sigue sin evaluar, mencionado en
§17).
