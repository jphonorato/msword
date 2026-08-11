# Migración de memoria Win16 → contrato `OpusShellMemory` (Qt-2 §B3)

**Fecha:** 2026-08-10
**Estado:** propuesto — pendiente de autorización (issue GitHub + primer commit)
**No toca `src/Opus/` en este documento**, solo lo especifica.

## 0. Verificación previa (contra árbol real, no contra docs)

Corrida real de `docs/port-qt/scripts/audit_win32_v2.py` en HEAD `d0c3aa3`, árbol limpio.

Sitios en la categoría *Memoria Win16* (`docs/port-qt/00-inventario-win32.md`, Vista 3):

| Símbolo | Sitios | TUs | En alcance |
|---|---|---|---|
| `GlobalUnlock` | 57 | 15 | sí |
| `GlobalFree` | 38 | 11 | sí |
| `GlobalLock` | 34 | 12 | sí |
| `GlobalLockClip` | 23 | 7 | no — contrato portapapeles Qt-6, ya decidido |
| `GMEM_MOVEABLE` | 17 | 8 | no — flag literal, no sitio propio (dentro de llamadas a `GlobalAlloc`/`GlobalReAlloc` ya contadas) |
| `GlobalAlloc` | 8 | 4 | sí |
| `GlobalCompact` | 6 | 4 | no — sin equivalente en el contrato, issue futura separada |
| `GlobalReAlloc` | 5 | 3 | sí |
| `GlobalSize` | 5 | 3 | sí |
| `GMEM_FIXED` | 2 | 1 | no — flag literal |
| `GlobalAddAtom` | 2 | 2 | no — atom table, concepto distinto |
| `GlobalFlags` | 2 | 2 | no — segment flags sin equivalente |
| `GlobalWire` | 2 | 2 | no — segment wiring sin equivalente |

**Total en alcance: 57+38+34+8+5+5 = 147 sitios.** Coincide con la cifra dada. `GlobalHandle`/`Local*`/`GLOBALHANDLE`/`LOCALHANDLE` están en el diccionario de la auditoría pero no aparecen listados en Vista 3 → cero sitios reales en el motor; `OpusMemHandle` en el header ya cubre `GlobalHandle` como equivalente aunque no haya sitios que migrar hoy.

**`GMEM_ZEROINIT`:** cero apariciones en las 173 TU del motor y en `Opus/debug/`. Aparece solo en `Opus/lib/qwindows.h` (SDK vendorizado, excluido de conteo por ser superficie de API) y en `Opus/asm/windows.inc` (ensamblador legado, no compilado, fuera de alcance por decisión de proyecto). El comentario de `OpusShellMemory.h:58-64` que documenta `OPUS_MEM_ZEROINIT` como "equivalente a GMEM_ZEROINIT" **no referencia un sitio inexistente por error** — es fidelidad a la semántica del SDK Win16 documentada para el flag, sin que haya (todavía) una llamada real que lo use. No es una corrección necesaria, solo una aclaración: `OpusMemFlagsFromWin16()` puede mapear `GMEM_ZEROINIT` sin que ninguna de las 147 migraciones de este spec vaya a ejercer esa rama.

### Adenda a §0 — Revisión de alcance (2026-08-11, previa al lote 2)

**Total de 147 sitios en alcance queda obsoleto como cifra de "migrables ya".** Auditoría de cruce de proceso/frontera (§10-§14) reclasifica los 147 sitios en seis categorías:

| Categoría | Sitios | TUs | Migrable ahora |
|---|---|---|---|
| **Ya migrado** | 2 | `elsubs2.c` | sí — hecho (`3c7c0db`) |
| **Lote 2 propuesto** | 4 | `catalog.c` | sí — pendiente autorización de ejecución |
| **A — familia de handle compartida entre TUs, mismo proceso** | 15 | `eldde.c` (parcial: `hevt`/`hPlaybackHook`/`lphrgbKeyState`) + `quit.c` (5) | no — orden de lote, pendiente de que `eldde.c` se desbloquee |
| **B — frontera de proceso** (fusiona antigua Categoría C con nuevo hallazgo C2, mismo fenómeno con mecanismo distinto — ver §11) | ~85 | `eldde.c` (resto), `ddeclnt.c`, `filecvt.c`, `spelcore.c`, `etcmd.c`, `help.c` (parcial), `raremsg.c` | no — bloqueado, decisión de diseño pendiente |
| **D — segmento/selector Win16 sin equivalente conceptual** | ~5 | `eldde.c` (`hPlaybackHook`, parcial ya contado en A), `idle.c`, `initwin.c` | no — sin equivalente en el contrato, como `GlobalWire`/`GlobalFlags` ya excluidos |
| **Muerto / fuera de build** | 10 | `initwin.c`, `rcinit.c`, `debug/debug1.c`, `debug/debug2.c`(*), `debug/debugwin.c`(*), `help.c` (`hText`/TEXTEVENT) | no — no se compila o `#if` nunca cierto |
| **`res.c` — hub estructural** | 4 | `res.c` | no — asignador central de 12 TU consumidoras, orden pendiente |

`(*)` `debug/debug2.c` y `debug/debugwin.c` no están en `OPUS_ORIGINAL_ENGINE_SOURCES` — no compilan, independientemente de `RPDEBUG`.

**Total migrable real confirmado hoy: 6 de 147** (2 hechos + 4 propuestos en lote 2). El resto (139 sitios) requiere una o más decisiones de diseño explícitas antes de tocarse, documentadas por categoría en §11-§14.

## 1. Estado real de `opus_shell_memory` en CMake

`src/core/CMakeLists.txt` ya declara el target (verificado, no asumido):

```cmake
add_library(opus_shell_memory STATIC
    src/OpusShellMemory.cpp
)
target_include_directories(opus_shell_memory PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
...
install(TARGETS opus_shell_memory ARCHIVE DESTINATION lib)
```

Sin `target_link_libraries(... Qt6::Core)` — confirmado, no depende de Qt6 (`OpusShellMemory.cpp` usa solo `<cstdlib> <cstring> <new> <unordered_map>`). `OpusShellMemory.h` y `.cpp` ya existen con implementación completa: handle opaco (`OpusHandleImpl`), contador de fijación, registro puntero→handle para `OpusMemHandle`, y las seis funciones (`OpusMemAlloc/Lock/Unlock/Realloc/Size/Handle/Free`). `OpusMemUnlock` devuelve `void` — importa para el caso especial de §4.

**No hay `add_executable(opus_shell_memory_test ...)` ni `add_test(...)` en `src/core/CMakeLists.txt`** — a diferencia de `opus_shell_config_test` y `opus_shell_font_substitution_test`, la implementación de memoria solo tiene verificación por `docs/port-qt/scripts/handle-check/` (programa de un solo uso, no CTest), consistente con lo que dice CLAUDE.md.

**`grep opus_shell_memory src/CMakeLists.txt` → sin resultados.** A diferencia de `opus_shell_config` (enlazado en `WORD1` desde el commit `9997eed`), `opus_shell_memory` **no está importado en el build de nivel superior**. Ningún archivo en `Opus/` puede incluir `OpusShellMemory.h` ni enlazar contra él todavía. Esto confirma el prerequisito: es bloqueante, no opcional.

## 2. Prerequisito CMake — wiring de `opus_shell_memory` en `src/CMakeLists.txt`

Camino **más simple** que el de `opus_shell_config` (commit `9997eed`), porque no hay dependencia Qt6 que propagar:

- No hace falta el bloque `find_program(QMAKE6_EXECUTABLE ...)` / `execute_process(qmake6 -query ...)` / `find_package(Qt6 REQUIRED COMPONENTS Core)` — eso solo existe para resolver `Qt6::Core` como dependencia transitiva de `opus_shell_config`.
- Sí hace falta añadir `libopus_shell_memory.a` a `BUILD_BYPRODUCTS` del `ExternalProject_Add(opus_core_build ...)` ya existente (mismo build produce ambas bibliotecas; sin este byproduct declarado, Ninja no tiene regla para el archivo — mismo motivo documentado en `9997eed` para `opus_shell_config`).
- `find_path(OPUS_WINE_INCLUDE_DIR ...)` ya generalizado por `9997eed` se reutiliza tal cual, sin cambios.

Diff a aplicar en `src/CMakeLists.txt`, dentro del bloque `if(OPUS_WINELIB_BUILD)` ya existente, inmediatamente después del bloque que declara `opus_shell_config`:

```cmake
    ExternalProject_Add(opus_core_build
        ...
        BUILD_BYPRODUCTS
            "${OPUS_CORE_PREFIX}/lib/libopus_shell_config.a"
            "${OPUS_CORE_PREFIX}/lib/libopus_shell_memory.a"   # nuevo
        ...
    )
    ...
    # opus_shell_memory: sin dependencia Qt6, a diferencia de
    # opus_shell_config -- IMPORTED_LOCATION basta, no hace falta
    # find_package(Qt6) ni INTERFACE_LINK_LIBRARIES.
    add_library(opus_shell_memory STATIC IMPORTED GLOBAL)
    set_target_properties(opus_shell_memory PROPERTIES
        IMPORTED_LOCATION "${OPUS_CORE_PREFIX}/lib/libopus_shell_memory.a")
    add_dependencies(opus_shell_memory opus_core_build)
```

Y en el bloque de enlace de `WORD1`:

```cmake
if(OPUS_WINELIB_BUILD)
    target_link_libraries(WORD1 PRIVATE opus_shell_config)
    target_link_libraries(WORD1 PRIVATE opus_shell_memory)   # nuevo
endif()
```

**Tarea 0 (bloqueante, antes de tocar cualquier sitio en `src/Opus/`):**
1. Aplicar el diff anterior.
2. `cmake --preset linux-winelib-debug` limpio (reconfigurar) — confirmar que no rompe la configuración existente.
3. `ninja -C out/linux-winelib-debug opus_shell_memory` — confirmar que el import resuelve y produce el `.a`.
4. Commit aislado: `build(cmake): wire opus_shell_memory into WORD1 link (Winelib)`. Sin cambios en `src/Opus/`, no requiere issue de `src/Opus/` (ver §3, la issue es para el paso siguiente).
5. **No se puede verificar el link real de `WORD1`** contra `opus_shell_memory` todavía, por la misma razón que `9997eed` dejó sin verificar el link contra `opus_shell_config`: `opus_original_engine` no compila en este entorno (VPS Debian, GCC 14.2.0) por el conflicto de flexible array member preexistente en `Opus/rsb.h`/`Opus/wordtech/disp.h`, no relacionado con este trabajo. Documentar el mismo pendiente: confirmar en Fedora (u otro entorno con GCC más permisivo) antes de dar el link por bueno. No asumir qué máquina se usará — resolver la ruta de verificación dinámicamente en cada entorno, no hardcodear a una distro.

## 3. Issue de GitHub

`CONTRIBUTING.md` exige autorización explícita vía issue antes de tocar `src/Opus/`. Abrir:

**Título:** `Qt-2 §B3: migrar 147 sitios Global*/GMEM_* a OpusShellMemory (contrato ya implementado)`

**Cuerpo:**
- Referencia a este spec (`docs/superpowers/specs/2026-08-10-opus-memory-migration-design.md`) y a `docs/port-qt/01-frontera-nucleo-shell.md` §B3.
- Alcance: 147 sitios en 17 TU de `src/Opus/` (lista completa por TU en §5, 14 en `Opus/` raíz + 3 en `Opus/debug/`), símbolos `GlobalAlloc`(8) `GlobalFree`(38) `GlobalLock`(34) `GlobalReAlloc`(5) `GlobalSize`(5) `GlobalUnlock`(57).
- Fuera de alcance explícito y por qué (tabla de §0): `GlobalLockClip`, `GlobalCompact`, `GlobalAddAtom`, `GlobalFlags`, `GlobalWire`.
- Guard obligatorio: `#if defined(__GNUC__) && !defined(_MSC_VER)` / `OpusMem...` / `#else` / `Global...` sin cambio / `#endif` — patrón ya en uso (`dlgmisc.c:2317-2343`, migración `OpusShellProfile*`).
- Granularidad de commit: por archivo (§6), no en bloque.
- Build MSVC (`x64-debug`) no debe cambiar de comportamiento: la rama `#else` preserva el código original byte a byte.

No proceder con ningún cambio en `src/Opus/` hasta que la issue esté abierta y haya autorización explícita del mantenedor, por sitio o por lote — igual que exige `CONTRIBUTING.md`.

## 4. Helper `OpusMemFlagsFromWin16()`

No existe todavía en `OpusShellMemory.h` (verificado: solo declara las 6 funciones, sin helper de mapeo de flags). Añadir como `static inline` en el header, junto a `OPUS_MEM_ZEROINIT`:

```c
/* Traduce los flags Win16 de una llamada GlobalAlloc/GlobalReAlloc a los
   de este contrato. GMEM_MOVEABLE y GMEM_FIXED distinguían estrategias
   del heap segmentado de 16 bits sin contraparte aquí -- se ignoran, no
   es error pasarlos. GMEM_ZEROINIT es el único bit con semántica propia;
   se traduce aunque ningún sitio migrado por
   docs/superpowers/specs/2026-08-10-opus-memory-migration-design.md lo
   ejerza hoy (0 apariciones en las 173 TU del motor, ver auditoría) --
   documentación fiel del SDK Win16, no una rama muerta por error. */
static inline unsigned OpusMemFlagsFromWin16(unsigned win16Flags)
{
    unsigned flags = 0;
    if (win16Flags & 0x0040u /* GMEM_ZEROINIT */)
        flags |= OPUS_MEM_ZEROINIT;
    return flags;
}
```

Nota: `GMEM_ZEROINIT` se pasa como literal `0x0040u` en el helper, no como macro `GMEM_ZEROINIT` — el header de `core/` no incluye `Opus/lib/qwindows.h` (es del lado núcleo, no depende del SDK Win16 vendorizado; ver regla de frontera en `01-frontera-nucleo-shell.md`). El valor está confirmado en `Opus/lib/qwindows.h:1736`.

Sitios que llaman `GlobalAlloc`/`GlobalReAlloc` con `GMEM_MOVEABLE`/`GMEM_FIXED` migran así (ejemplo real, patrón repetido en los 8+5 sitios):

```c
#if defined(__GNUC__) && !defined(_MSC_VER)
hMem = (HANDLE)OpusMemAlloc((unsigned long)cb, OpusMemFlagsFromWin16(GMEM_MOVEABLE));
#else
hMem = GlobalAlloc(GMEM_MOVEABLE, cb);
#endif
```

(`GMEM_MOVEABLE` sigue definido y usado en la rama `#else`/MSVC; en la rama GCC solo pasa por el helper, que la ignora — comportamiento documentado, no un cambio silencioso.)

## 5. Sitios en alcance por TU (mecánico)

Reconteo directo (mismo tokenizador que `audit_win32_v2.py`, filtrado a los 6 símbolos en alcance únicamente) sobre las 173 TU del motor + `Opus/debug/`. **17 TU, no 15** — la cifra de "15 TU" que se podría inferir de la fila `GlobalUnlock` en §0 es el conteo de TU que tocan *ese símbolo*, no la unión de las 6; `Opus/res.c` (`GlobalAlloc`+`GlobalReAlloc`) y `Opus/debug/debugwin.c` (`GlobalAlloc`+`GlobalLock`+`GlobalReAlloc`) no llaman `GlobalUnlock` y quedan fuera de esa fila pero dentro del alcance real:

| TU | Sitios | Símbolos (conteo) |
|---|---|---|
| `Opus/eldde.c` | 48 | GlobalUnlock 19, GlobalFree 12, GlobalLock 15, GlobalSize 2 |
| `Opus/filecvt.c` | 22 | GlobalAlloc 4, GlobalFree 6, GlobalUnlock 9, GlobalLock 3 |
| `Opus/help.c` | 15 | GlobalLock 4, GlobalFree 5, GlobalUnlock 6 |
| `Opus/spelcore.c` | 9 | GlobalUnlock 4, GlobalLock 3, GlobalFree 2 |
| `Opus/raremsg.c` | 8 | GlobalUnlock 4, GlobalFree 1, GlobalLock 2, GlobalSize 1 |
| `Opus/etcmd.c` | 7 | GlobalFree 3, GlobalUnlock 4 |
| `Opus/ddeclnt.c` | 5 | GlobalUnlock 3, GlobalFree 2 |
| `Opus/idle.c` | 5 | GlobalReAlloc 2, GlobalSize 2, GlobalUnlock 1 |
| `Opus/quit.c` | 5 | GlobalFree 3, GlobalLock 1, GlobalUnlock 1 |
| `Opus/catalog.c` | 4 | GlobalAlloc 1, GlobalLock 1, GlobalUnlock 1, GlobalFree 1 |
| `Opus/res.c` | 4 | GlobalAlloc 2, GlobalReAlloc 2 |
| `Opus/debug/debug2.c` | 4 | GlobalLock 1, GlobalFree 2, GlobalUnlock 1 |
| `Opus/debug/debug1.c` | 3 | GlobalLock 1, GlobalUnlock 1, GlobalFree 1 |
| `Opus/debug/debugwin.c` | 3 | GlobalAlloc 1, GlobalLock 1, GlobalReAlloc 1 |
| `Opus/elsubs2.c` | 2 | GlobalLock 1, GlobalUnlock 1 |
| `Opus/rcinit.c` | 2 | GlobalLock 1, GlobalUnlock 1 |
| `Opus/initwin.c` | 1 | GlobalUnlock 1 |
| **TOTAL** | **147** | **17 TU** |

`Opus/debug/*` (3 TU, 10 sitios) no enlaza en `WORD1` pero está en alcance del proyecto por decisión ya tomada (ver `00-inventario-win32.md`, región `Opus/debug/`) — mismo guard, mismo commit granular, sin trato especial más allá de eso.

**Adenda 2026-08-10 (lote 1, validación del patrón) — verificado contra el árbol, no asumido:**

- **3 de los 147 sitios son código muerto bajo `OPUS_X64`** (definido incondicionalmente para `opus_original_engine`, `src/CMakeLists.txt:1036`): `Opus/initwin.c:781` (dentro de `#ifndef OPUS_X64` … `#endif`, líneas 764–788) y `Opus/rcinit.c:84,86` (dentro de la rama `#else` de `#ifdef OPUS_X64` en `HLoadRes0`, líneas 39–88). Cruce hecho contra los 6 símbolos en las 17 TU de esta tabla — los 144 sitios restantes son código vivo. Aplicar el guard ahí sería inocuo pero no ejercitable: la verificación de enlace (§8.4, `nm` sobre WORD1) no puede mostrar el símbolo `OpusMem*` referenciado porque el preprocesador descarta el bloque entero en ambas ramas. Se dejan sin migrar hasta un lote aparte de "código muerto bajo OPUS_X64", explícitamente distinto del resto de la migración mecánica (mismo criterio que catalog.c §7: no mezclar con el commit mecánico normal).
- **Riesgo no cubierto por §6: familia de handle compartida entre TU con distinto estado de migración.** `Opus/elsubs2.c:325,328` (`GlobalLock`/`GlobalUnlock` sobre `*lphevtHead`, tipo `HEVT`) opera sobre el mismo objeto que `Opus/eldde.c` asigna/libera (~15 sitios `hevt`/`lphevtHead`/`phevt`, TU de 48 sitios, no migrada en el lote 1) y que `Opus/quit.c:809` también libera. `OpusMemLock`/`OpusMemUnlock` (`src/core/src/OpusShellMemory.cpp`) operan sobre un registro privado (`OpusHandleImpl*`), no envuelven `GlobalAlloc` de Wine — si el handle se asigna con `GlobalAlloc` real (TU no migrada) y se bloquea con `OpusMemLock` (TU migrada), es memoria ajena reinterpretada como el struct interno del contrato, no solo un mismatch de tipos. No bloquea el lote 1 porque WORD1 no arranca hoy (heap corruption en constructores, bloqueador de Fase 6, `CONTRIBUTING.md`) — el código no se ejecuta. **Antes de que `eldde.c` (u otra TU que toque `hevt`) se migre por separado, migrar `elsubs2.c` y `quit.c:809` junto con ella en el mismo lote**, no como TUs independientes por conteo de sitios — el orden "TUs de un solo símbolo primero" de §6 no debe aplicarse ciegamente a sitios que comparten un handle con una TU grande aún sin migrar.

Antes de tocar cada archivo, correr:
```bash
grep -n "GlobalAlloc\|GlobalFree\|GlobalLock\|GlobalReAlloc\|GlobalSize\|GlobalUnlock" src/Opus/<archivo>.c
```
para obtener los números de línea reales en el momento de implementación (los de este spec pueden desalinearse si el árbol cambia entre redacción y ejecución).

## 6. Granularidad de commit — corrección deliberada sobre el diseño original

El diseño previo (fuera del árbol git) agrupaba la migración completa en un "paso 3" monolítico, sin especificar granularidad — 147 sitios en un commit contradice la disciplina de verificación por archivo que el propio proyecto exige (`ninja -t commands` aislado por TU, ya usado en `01-frontera-nucleo-shell.md:773` para la migración de config).

**Regla de este spec:** un commit por archivo, o por lote de 2-3 archivos cuando son triviales y del mismo patrón (mismo símbolo, mismo idiom de guard, sin llamadas anidadas ni casos especiales). Nunca un commit que toque más de 5 sitios sin que cada uno se haya verificado individualmente contra el binario compilado del archivo. El caso especial de `catalog.c:1612` (§7) es su propio commit, no se mezcla con la migración mecánica de ese archivo.

Orden sugerido: TUs con un solo símbolo y pocos sitios primero (para validar el patrón), luego las TUs con más TUs/sitios acopladas.

Mensaje de commit por lote, formato:
```
core(port-qt): §B3 -- migrar Global*/GMEM_* a OpusMem* en <archivo(s)>

<N> sitios (<símbolos>). Guard #if defined(__GNUC__) && !defined(_MSC_VER),
rama MSVC sin cambio. Verificado: ninja -t commands aislado + 0 nuevos
errores/warnings OpusMem en ninja -k 0.
```

## 7. Caso especial — `catalog.c:1612`

```c
FreeDMFarMem()
{
	if (GlobalUnlock(hDMFarMem) != 0)
		Assert(fFalse);
	GlobalFree(hDMFarMem);
	...
```

`GlobalUnlock` Win16 devuelve el contador de fijación restante; el código asume que este handle se bloquea una sola vez (`Assert(fFalse)` dispara si el contador queda >0 tras el unlock, señal de un lock anidado no esperado). `OpusMemUnlock` en el contrato ya implementado **devuelve `void`** (verificado en `OpusShellMemory.h`/`.cpp`) — no hay valor de retorno que comparar.

Migración:
```c
#if defined(__GNUC__) && !defined(_MSC_VER)
FreeDMFarMem()
{
	OpusMemUnlock((OpusHandle)hDMFarMem);
	OpusMemFree((OpusHandle)hDMFarMem);
	hDMFarMem = DMFarMem = NULL;
	cbDMFarMem = 0;
}
#else
FreeDMFarMem()
{
	if (GlobalUnlock(hDMFarMem) != 0)
		Assert(fFalse);
	GlobalFree(hDMFarMem);
	hDMFarMem = DMFarMem = NULL;
	cbDMFarMem = 0;
}
#endif
```

**Pérdida de comportamiento aceptada, documentada aquí explícitamente:** el `Assert` solo detectaba lock-count > 1 en un handle que este código trata como de un solo lock; la implementación de `OpusShellMemory.cpp` sí mantiene un `lockCount` interno (verificado) pero no lo expone por la API pública del contrato, así que no hay forma de reproducir el assert sin ampliar la superficie de `OpusShellMemory.h` solo para este sitio. Se acepta la pérdida porque: (a) es un diagnóstico de debug, no lógica de producto; (b) el único caso real en el árbol; (c) ampliar la API pública por un sitio viola YAGNI del contrato de frontera.

**Tratar como commit separado**, después de que el resto de `catalog.c` (si tiene otros sitios en alcance) ya esté migrado y verificado — no mezclar la pérdida de comportamiento documentada con la migración mecánica de líneas contiguas.

## 8. Verificación

Por cada commit (archivo o lote pequeño):

1. `ninja -k 0 -C out/linux-winelib-debug opus_original_engine` — medición válida de errores es *solo* `-k 0`, nunca `-k 1`. Excepción ya documentada y no imputable a este trabajo: fallo preexistente por flexible array member en `Opus/rsb.h`/`Opus/wordtech/disp.h` (GCC 14 incompatible), no cuenta contra el veredicto de este cambio.
2. Aislamiento por archivo: `ninja -t commands -C out/linux-winelib-debug | grep <archivo>.o` — confirmar que el comando de compilación de ese `.o` específico no tiene nuevos errores, sin depender del estado del resto del árbol (mismo patrón que `01-frontera-nucleo-shell.md:773`).
3. `grep -rn "OpusMem\|OpusShellMemory" out/linux-winelib-debug/**/*.log 2>/dev/null` (o el log de build que corresponda) — cero errores o warnings nuevos mencionando los símbolos migrados.
4. Verificación del link real de `WORD1` contra `opus_shell_memory`: **pendiente, no realizable en este entorno hoy** (VPS Debian bloqueado por el mismo fallo GCC 14 de `rsb.h`/`disp.h` que ya bloqueó la verificación de `opus_shell_config` en `9997eed`). Queda como paso explícito a confirmar en Fedora u otro entorno con GCC más permisivo, sin asumir de antemano cuál — resolver dinámicamente (`gcc --version`, disponibilidad de wine-devel) en el momento, no hardcodear el entorno doméstico como si fuera el único.
5. Solo tras Tarea 0 (prerequisito CMake, §2) verificada — sin esto ningún sitio migrado compila, porque `OpusShellMemory.h` no resuelve y no hay símbolo `opus_shell_memory` que enlazar.

## 9. Resumen de secuencia

1. Tarea 0 — wiring CMake de `opus_shell_memory` (§2), commit aislado, sin tocar `src/Opus/`.
2. Abrir issue de GitHub (§3), esperar autorización.
3. Por archivo o lote pequeño (§5, §6): migrar sitios mecánicos con el guard estándar + `OpusMemFlagsFromWin16()` donde aplique.
4. Commit separado para `catalog.c:1612` (§7), con la pérdida de comportamiento documentada en el mensaje de commit, no solo en este spec.
5. Verificación por cada paso (§8); verificación de link real de `WORD1` queda pendiente de entorno hasta poder ejecutarla.

## 10. Familias de handle cruzando TUs — análisis previo al lote 2 (2026-08-10)

Hecho después de commitear `elsubs2.c` (`3c7c0db`), antes de elegir el siguiente archivo. Método: extracción del argumento de los 147 sitios (los 6 símbolos, script sobre las 17 TU de §5), agrupado por identificador base, y verificación individual en el código (declaración `extern`/global vs. local) para cada grupo con más de una TU — no basta con que el nombre coincida.

**Categoría A — global compartido real, mismo tipo de riesgo que `hevt` (§ya documentado en el commit de `elsubs2.c`):**

| Familia | TUs y símbolos | Evidencia |
|---|---|---|
| `hevt`/`lphevtHead`/`phevt` | `eldde.c` (Lock/Unlock/Free, ~15 sitios) + `elsubs2.c` (Lock/Unlock, ya migrado en `3c7c0db`) + `quit.c:809` (Free) | ya documentado |
| `hPlaybackHook` | `eldde.c:40` declara `HANDLE hPlaybackHook;` (global) + `quit.c:321` `extern HANDLE hPlaybackHook;` (Free) | `eldde.c` Lock, `quit.c` Free — mismo objeto |
| `lphrgbKeyState` | `eldde.c:42` declara `HANDLE FAR *lphrgbKeyState;` (global) + `quit.c:798` `extern ... far *lphrgbKeyState;` | `eldde.c` Lock/Free/Unlock, `quit.c` Lock/Unlock/Free — mismo objeto, ambos TUs gestionan el ciclo completo |

Los tres apuntan al mismo par `eldde.c` + `quit.c`. Implicación: cuando le toque el turno a `eldde.c` (si su parte no-DDE llega a migrarse — ver §11), debe ir en el mismo lote que `quit.c` completo (5 sitios), no aparte.

**Categoría B — falso positivo por nombre genérico, verificado que NO comparten objeto (migrables por TU independiente):**

- `hsz` (`help.c`, `spelcore.c`): local en cada función, sin `extern`, objetos distintos.
- `pghd->ghsz` (`etcmd.c`, `spelcore.c`): `pghd` es parámetro `struct GHD *` (tipo compartido, `splshare.h:64`); cada TU tiene su propia función alocadora sobre instancias de dueño distinto (`FCopySzToGhdET`/`vpetlib->*` en `etcmd.c`; `FCopySzToGhd`/`vpspl->*` en `spelcore.c`) — ciclo completo alloc→lock→unlock→free dentro de cada TU.
- `hData` en `debug/debug2.c` (no confundir con el `hData` de `eldde.c`/`ddeclnt.c`, categoría C): local, ciclo completo dentro de una sola función.
- `h` (`debug/debug1.c`, `rcinit.c`): locales sin relación entre sí; además ambos son código muerto (ver actualización en §12).

## 11. Categoría B — memoria cruzando la frontera del proceso (BLOQUEADO, fuera de alcance de este issue; unifica la antigua Categoría C)

**No es un problema de orden de lote como la Categoría A — es una pregunta de diseño abierta, sin resolver.**

Reescrito 2026-08-11: el fenómeno de fondo es único — el handle en cuestión tiene un consumidor real fuera de este binario/TU-tree, así que envolver su `Lock`/`Unlock`/`Free` en `OpusMemLock`/`OpusMemUnlock`/`OpusMemFree` (que operan sobre un registro privado `OpusHandleImpl*`, no sobre `HGLOBAL` de Wine) puede reinterpretar memoria ajena como el struct interno del contrato. El mecanismo de cruce varía (DDE, DLL externa, WinHelp, portapapeles del sistema); el riesgo no. Se documenta como una categoría con tres subcasos en vez de tres categorías separadas.

### Subcaso B1 — DDE vía mensajes de Windows (antiguo contenido de la Categoría C, sin cambios)

Cuatro sitios verificados en el árbol (no los 48 de `eldde.c` completos — ver alcance de la verificación más abajo):

- **`eldde.c` `hExec`** (`ElDDEExecute`, líneas 176–218): alocado localmente con `OurGlobalAlloc(GMEM_DDE,...)`, pero posteado a otra ventana/proceso vía `FPostDdeDcl(dcl, WM_DDE_EXECUTE, 0, hExec)` — el handle viaja por el sistema de mensajes real de Windows/Wine hacia el receptor DDE, que puede ser un proceso externo.
- **`eldde.c` `hCmds`** (líneas 1385–1436): bloqueado con `GlobalLockClip` (ya excluido del contrato, decisión Qt-6 de portapapeles) pero desbloqueado con `GlobalUnlock` (2 sitios, sí en alcance de este issue) — es un macro/comando **recibido** vía `WM_DDE_EXECUTE` desde otro proceso.
- **`eldde.c` `wLow`** (handler `WM_DDE_DATA`, líneas 540–620) y **`ddeclnt.c` `wLow`** (líneas 770–920): mismo patrón — el handle llega en el propio mensaje `WM_DDE_DATA`, se bloquea con `GlobalLockClip` (excluido) y se libera con `GlobalFree` (en alcance). El propio código confirma que es un bloque de datos que **otro proceso** alocó y envió.
- **`eldde.c`/`ddeclnt.c` `hData`** (`ddli.hData` y variantes de `ElDDEExecute`): mismo patrón de protocolo DDE.

**Argumento central:** en los cuatro casos, el `Lock` ya vive permanentemente fuera del contrato `OpusShellMemory` (`GlobalLockClip`, decisión Qt-6 ya tomada), pero el `Free`/`Unlock` emparejado sí está contado entre los 147 sitios de este issue. Si el handle lo aloca un proceso externo real vía `WM_DDE_*` (no este código), envolver su liberación/desbloqueo en `OpusMemFree`/`OpusMemUnlock` puede ser **incorrecto de raíz** — no un problema de "todavía no se migró la otra TU" que se arregla reordenando lotes (a diferencia de la Categoría A).

**Alcance de la verificación — explícito, no asumir más de lo comprobado:** se verificaron estos 4 casos concretos, no los 48 sitios de `eldde.c` uno por uno. El patrón (par `GlobalLockClip` + `Free`/`Unlock` sobre un handle de mensaje DDE) es consistente en los 4, pero esto es **un patrón observado, no un hallazgo cerrado sobre toda la TU** — antes de tocar `eldde.c` o `ddeclnt.c` hace falta una revisión completa sitio por sitio, no solo extrapolar desde estos 4.

### Subcaso B2 — DLL externa vía `CallOtherStack`/`WCallOtherStack` (nuevo, 2026-08-11)

El handle se pasa por llamada a otra pila/DLL cargada dinámicamente (`GetProcAddress`), que hace su propio `Lock` sobre el mismo handle:

- **`filecvt.c`** (22 sitios): `ghszFn`/`ghszSubset`/`ghszVersion`/`ghBuff`/`ghIniName`/`ghIniExt`, todos pasados a la DLL de conversión (`ISFORMATCORRECT`/`FOREIGNTORTF`/`RTFTOFOREIGN`/`GETINIENTRY`, resueltas vía `GetProcAddress` en `filecvt.c:473-520`).
- **`spelcore.c`** (9 sitios en alcance): `hsz`/`pghd->ghsz` pasados a `MyGetAlternates`/`LMyLookUpWord` → `CallOtherStack`/`LCallOtherStack` hacia el corrector ortográfico externo.
- **`etcmd.c`** (7 sitios): `pghd->ghsz` (`vpetlib->ghdWord/ghdWorkspace/ghdRgpos`) pasado a `WMyETLookup` → `WCallOtherStack` hacia la DLL del motor de sinónimos/gramática (ET).

### Subcaso B3 — WinHelp / portapapeles del sistema (nuevo, 2026-08-11)

- **`help.c`** (11 de 15 sitios): `hHlp` (9 sitios) enviado a WINHELP.EXE vía `RegisterWindowMessage(szWINHELP)`/`LookForHelp`; `hsz` (2 sitios, `GlobalUnlock` en `KcFromSzHelp`/`SzFromKcHelp`) — funciones exportadas sin llamador interno, para que WinHelp las invoque.
- **`raremsg.c`** (parcial de 8): `hdata` en `FFedtCopy` pasa a `SetClipboardData` — ownership transferido al portapapeles del sistema; `hdata`/`hdata2` en `FFedtPaste`/`HFedtStripText` provienen de `GetClipboardData` — handle ya ajeno desde el origen.

**Estado: BLOQUEADO.** Ningún sitio de B1/B2/B3 se migra bajo ninguna circunstancia sin autorización nueva y específica del mantenedor — distinta de la ya dada para el resto de los 147 sitios en issue #3 — hasta que exista una decisión de diseño explícita sobre si estos sitios quedan excluidos del contrato `OpusShellMemory` (como ya pasó con `GlobalLockClip`) o se resuelven de otra forma.

## 12. Código muerto bajo condición no compilada — actualizado (6 sitios, 3 TU, no 3)

Además de `initwin.c:781` y `rcinit.c:84,86` (bajo `OPUS_X64`, ya documentado en §5), **`Opus/debug/debug1.c` tiene sus 3 sitios completos** (100% del archivo — `GlobalLock`, `GlobalUnlock`, `GlobalFree`, todos dentro de `FakeRtfClipboard`) bajo `#ifdef RPDEBUG`. `RPDEBUG` no está definido en ningún `.h` ni en `CMakeLists.txt` del árbol — código muerto, mismo tratamiento que los otros dos. Total actualizado: **6 sitios, 3 TU** (`initwin.c` 1, `rcinit.c` 2, `debug/debug1.c` 3), dos flags de exclusión distintos (`OPUS_X64`, `RPDEBUG`). `debug/debug1.c` queda fuera de cualquier lista de candidatos "seguros para migrar ya" por esta razón, no solo por pertenecer a `Opus/debug/`.

## 13. `res.c`/`OurGlobalAlloc` — el asignador central, no un archivo más de la lista (2026-08-10, previo al lote 2)

Encontrado al evaluar `res.c` como candidato al lote 2. No es una familia de handle más — es una dependencia estructural que atraviesa la mayoría de las 17 TU.

`Opus/res.c` define `OurGlobalAlloc`/`GlobalAlloc2`/`OurGlobalReAlloc` (comentario propio del código: *"Perform a GlobalAlloc. if it fails reduce our swap area and try again."*) — el punto de entrada de asignación que casi todo el árbol usa en vez de llamar `GlobalAlloc`/`GlobalReAlloc` directamente. Internamente, `OurGlobalAlloc`/`OurGlobalReAlloc` sí llaman la `GlobalAlloc`/`GlobalReAlloc` reales — son 2 de los 4 sitios en alcance de `res.c` en la tabla de §5.

**Verificado por grep, no asumido:** `OurGlobalAlloc`/`OurGlobalReAlloc` se llaman desde **12 de las 17 TU** en alcance: `eldde.c`, `ddeclnt.c`, `etcmd.c`, `filecvt.c`, `help.c`, `rcinit.c`, `spelcore.c`, `res.c`, `raremsg.c`, `rtfout2.c`, `debug/debug2.c`, `debug/debug1.c`. Confirmado además, específicamente, que el handle que cada una de estas TU asigna ahí es el mismo que sus propios sitios `GlobalLock`/`GlobalUnlock`/`GlobalFree` en alcance tocan después (no una coincidencia de nombre): `help.c:265` (`hHlp`) y `help.c:2110` (`hText`); `spelcore.c:681,687` (`pghd->ghsz`) y `spelcore.c:727,797` (`hsz`); `debug/debug2.c:714` (`hData`).

**Consecuencia:** migrar los 2 sitios `GlobalAlloc`/2 `GlobalReAlloc` de `res.c` por sí solos cambiaría de golpe la representación en tiempo de ejecución de *todo* handle obtenido vía `OurGlobalAlloc` — es decir, de las 12 TU consumidoras simultáneamente — mientras ninguna de ellas (salvo que se migre en el mismo lote) tenga sus propios `Lock`/`Unlock`/`Free` migrados. Es el mismo tipo de corrupción que la Categoría A (`hevt`) y la Categoría C (DDE), pero en vez de 2-3 TU compartiendo un handle, son potencialmente **todas las TU que dependen de `OurGlobalAlloc`** a la vez. Migrar `res.c` en un lote pequeño y aislado (como pide §6) es exactamente lo que rompe esto — sería el lote menos aislado de los 147 sitios, no el más simple.

**Implicación para el orden general, no solo para `res.c`:** cualquier TU cuyos handles en alcance provengan de `OurGlobalAlloc` (la mayoría de las 12 listadas arriba) hereda el mismo riesgo que ya se documentó para `elsubs2.c`/`hevt` — el "productor" (`res.c`, sin migrar) y el "consumidor" (la TU en cuestión, si se migra) quedan desincronizados hasta que `res.c` se migre. La diferencia entre `elsubs2.c` y estas 12 TU es que `elsubs2.c` no asigna nada — solo bloquea/desbloquea un handle ajeno (issue documentado, aceptado porque WORD1 no arranca hoy). Migrar `help.c` o `spelcore.c` (que sí enlazan en WORD1, a diferencia de `debug/*`) repetiría el mismo riesgo pero sobre TU que si el bloqueador de arranque de Fase 6 se resuelve, sí se ejecutan — acumular esto sin resolverlo es la deuda técnica que la granularidad de §6 buscaba evitar, no solo "todavía no le tocó el turno a esa TU".

**`res.c` queda fuera de la lista de candidatos para lotes tempranos.** Su migración requiere, o bien migrar sus 12 TU consumidoras en el mismo lote (contradice la granularidad de §6), o bien migrarlo al final, después de que todas ellas ya estén migradas — decisión de orden explícita pendiente, no asumida aquí. `Opus/debug/debugwin.c` es la única TU de las 17 que **no** depende de `OurGlobalAlloc` en absoluto (verificado, cero coincidencias) — no tiene alocador propio, son 3 funciones puente (`GlobalAllocFP`/`GlobalLockClipFP`/`GlobalReAllocFP`) que reciben el handle ya armado como parámetro genérico de quien las llama.

## 14. Categoría D — segmento/selector Win16 sin equivalente conceptual (2026-08-11, previa al lote 2)

Sitios donde el handle no es memoria simple sino un segmento con semántica de wiring/discardability o un selector real de Win16, sin contraparte en un contrato basado en `malloc`/`realloc`/`free`:

- **`eldde.c` `hPlaybackHook`** (`AddKeys`, líneas 1261-1332): `GMEM_FIXED|GMEM_LOWER` + `AllocSelector`/`PrestoChangoSelector`/`FreeSelector` reales sobre el handle, para instalar un hook de reproducción de teclado (`SetWindowsHook(WH_JOURNALPLAYBACK,...)`). Ya contado dentro de la familia A (`eldde.c`/`quit.c`) por el orden de lote, pero el motivo de exclusión real es este, no solo "compartido entre TUs".
- **`idle.c` `hfontPhy`** (líneas 770-792): bloque `GlobalReAlloc(...,GMEM_MODIFY)` + `GlobalWire` + `GlobalUnlock`, inseparable de `GlobalFlags`/`GlobalWire` (ya excluidos por §0) sobre el mismo handle en el mismo condicional — manejo de discardability de fuente física GDI.
- **`initwin.c` `hInstance`** (línea 781): opera sobre el handle de módulo (no un handle de `OurGlobalAlloc`), emparejado con `GlobalFlags`/`GlobalWire` ya excluidos. Doblemente no migrable — además de ser código muerto bajo `OPUS_X64` (§12).

Mismo tratamiento que `GlobalWire`/`GlobalFlags`/`GlobalCompact`: sin equivalente en el contrato, issue futura separada si alguna vez se decide abordar.
