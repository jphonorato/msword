# Auditoría independiente del checklist §8 de passthrough

**Fecha:** 2026-08-11
**Estado:** resultado de auditoría. **Ningún archivo del árbol fue modificado.** Sin `git add`, sin commit.
**Documento auditado:** `docs/superpowers/specs/2026-08-11-opus-memory-passthrough-design.md`, checklist §8 y su respuesta ya escrita en §9.1–§9.4 (commits `a1b5695`, `aa9ef20`).
**HEAD:** `c4b2a09`. Verificado que `src/Opus/`, `src/OpusEtAl/` y `src/port/` no cambiaron desde `2a36b1a` (el HEAD que cita el censo): `git diff --stat 2a36b1a..HEAD -- src/Opus src/OpusEtAl src/port` → vacío.

## 0. Por qué existe este documento y por qué es un archivo aparte

El checklist §8 ya tenía respuesta en el árbol (§9.1–§9.4). Esta sesión lo re-audita de forma
independiente, con la instrucción explícita de no fiarse de las cifras documentadas y recontarlas
contra el árbol. El resultado **contradice §9 en varios puntos, incluido uno estructural**, y también
contradice el censo base del que §9 depende.

Se escribe como archivo nuevo, no como sección añadida a `…-passthrough-design.md`, por dos razones:
§9 está commiteado como "resultado del checklist" y reescribirlo in situ borraría la traza de qué se
creyó y cuándo; y los hallazgos alcanzan también a
`2026-08-11-opus-memory-blocked-categories-design.md` §1 y a `2026-08-10-opus-memory-migration-design.md` §5,
que son documentos distintos. Este archivo **supersede §9.1–§9.4** en los puntos donde diverge, y lo dice
sitio por sitio.

Método: cuatro auditores de solo lectura, alcances disjuntos, sin resolución automática de conflictos.
Donde dos auditores se contradijeron, la discrepancia se arbitró con verificación directa y queda
registrada como tal (§1.1).

---

## 1. Ítem 1 — censo

### 1.1 Arbitraje: el censo de 147 sitios está incompleto por un glob sensible a mayúsculas

Dos auditores llegaron a conclusiones incompatibles. El auditor de censo reconcilió las cifras
documentadas de forma exacta (208 ocurrencias en el árbol → 150 en las 17 TU → 147 tras descontar 3
comentarios en `res.c`) y concluyó que el censo era correcto. El auditor de `Realloc` afirmó que
faltaban TUs compiladas con extensión `.C` mayúscula.

**Verificación directa, arbitrada a favor del segundo:**

```
$ ls src/Opus/*.C | wc -l
20
$ grep -rEno "\bGlobal(Alloc|Free|Lock|ReAlloc|Size|Unlock)\b" src/Opus/*.C | wc -l
84
$ sed -n '659,867p' src/CMakeLists.txt | grep -oE "Opus/[A-Za-z0-9_]+\.C\b" | wc -l
18
```

18 de esas 20 TUs están en `OPUS_ORIGINAL_ENGINE_SOURCES` (`src/CMakeLists.txt:659-867`) — se compilan.
Las otras dos (`PIC3.C`, `RTFIN2.C`) entran por el mapeo de shim de mayúsculas
(`src/CMakeLists.txt:990-991`), es decir vía `#include` desde otra TU.

Desglose de las 84 ocurrencias:

| TU | Ocurrencias | En `ENGINE_SOURCES` |
|---|---|---|
| `CLIPBRD2.C` | 39 | sí |
| `DDESRVR.C` | 15 | sí |
| `GRSPEC.C` | 10 | sí |
| `PIC3.C` | 5 | vía shim (`:990`) |
| `PIC2.C` | 4 | sí |
| `SPELL.C` | 3 | sí |
| `SCREEN2.C` | 3 | sí |
| `RTFRARE.C` | 2 | sí |
| `CLIPBORD.C` | 2 | sí |
| `LOADFONT.C` | 1 | sí |

Además, `rtfout2.c` **sí se compila**, al contrario de lo que asumen los dos documentos base:
`Opus/RTFOUT.C:2222` hace `#include "rtfout2.c"`, y `RTFOUT.C` está en `ENGINE_SOURCES`. Aporta 4
ocurrencias más de los 6 símbolos, 2 de `GlobalLockClip` y 1 de `OurGlobalReAlloc`.

**Por qué el primer auditor no lo vio:** usó `grep -rE --include='*.c'`, que en Linux es sensible a
mayúsculas — exactamente el mismo punto ciego que el censo original. Su reconciliación 208/150/147 es
internamente consistente y aritméticamente correcta; simplemente reconcilia un universo recortado
contra sí mismo. Es un recordatorio de que cuadrar cifras no es lo mismo que verificar alcance.

### 1.2 Recuento correcto

Censo insensible a mayúsculas, mismos 6 símbolos, nivel raíz de `src/Opus/`:

```
$ grep -rEno "\bGlobal(Alloc|Free|Lock|ReAlloc|Size|Unlock)\b" src/Opus/*.c src/Opus/*.C | wc -l
237
```

Cuadra exacto contra el universo documentado:

| Fragmento | Ocurrencias |
|---|---|
| Las 14 TU de raíz que sí cuenta el censo (de sus 17) | 140 |
| TUs `.C` mayúscula omitidas | 84 |
| `rtfout2.c` (compilado vía `RTFOUT.C:2222`), omitida | 4 |
| `profwin.c` (no compilado), omitida | 6 |
| `ripaux.c` (no compilado, y 0 de sus 3 son llamadas), omitida | 3 |
| **Total `Opus/*.c` + `Opus/*.C`** | **237** |

Las otras 3 TU del censo son `Opus/debug/{debug1,debug2,debugwin}.c` (10 ocurrencias), fuera de este
glob y **fuera del build**. 140 + 10 = 150, que es la cifra bruta documentada.

Símbolos y sitios adicionales que ningún censo cuenta:

- **`GlobalLockClip` — 42 ocurrencias** en `Opus/*.c` + `Opus/*.C` (25 en minúsculas, 17 en `.C`). El
  propio `…-blocked-categories-design.md` §1.1 establece que `debugwin.h:209` lo define como
  `GlobalLock` cuando `DEBUG` no está definido (y no lo está: `src/CMakeLists.txt:1058-1065`), pero el
  regex del censo (`\bGlobal(Alloc|…|Unlock)\b`) no lo captura. De las 25 en minúsculas, 21 son vivas y
  compiladas.
- **`src/port/original/opus_asm_misc.cpp` — 6 sitios vivos** (`:246,265,268,272,275,277`), en
  `ENGINE_SOURCES` (`src/CMakeLists.txt:857`). Declara `extern HANDLE* lphevtHead;` y
  `extern HANDLE* lphrgbKeyState;` (`:103-104`) — **los mismos globales de `eldde.c:40-41`**, es decir
  familia de Categoría A. Es el `PlaybackHook` nativo del port. Ningún documento lo cuenta.

**Conclusión del ítem 1:** el universo real de sitios de la familia `Global*` en código compilado es
del orden del doble del documentado. La cifra de 147 no es el total del árbol; es el total de un
subconjunto de 17 TU seleccionado por un glob defectuoso.

### 1.3 Errores internos del censo de 147 (independientes del punto ciego)

Cuatro correcciones que se compensan entre sí y **no alteran el total de 147**:

| Categoría | Documentado | Corregido | Motivo |
|---|---|---|---|
| B1 — DDE entre procesos | 16 | **14** | `eldde.c` tiene **dos** variables `hData` distintas: la de `:242` (`OurGlobalAlloc(GMEM_DDE)`, `:265`) es DDE real; la de `:1199` (`OurGlobalAlloc(GMEM_FIXED\|GMEM_LOWER)`, `:1261`) es el bloque `DRVDATA` del hook de journal-playback. Sus 2 sitios (`:1221,:1270`) son familia `hPlaybackHook`, categoría D |
| B3 — WinHelp / portapapeles | 19 | **17** | `help.c` `hsz` (`:1087,:1131`) está dentro de `#ifdef NOT_USED_CURRENTLY` (`:1057-1154`); el símbolo no se define en ningún punto del árbol |
| D — segmento/selector | 11 | **13** | +2 de la corrección de B1 |
| Muerto / fuera de build | 17 | **19** | +2 de la corrección de B3 |

Consecuencia aritmética: la fila 3 del §7 de `…-blocked-categories-design.md` (la decidida) pasa de
**119/147** a **115/147** — `147 − 13 (D) − 19 (muerto) = 115`. El porcentaje "81%" ya no se sostiene
ni siquiera dentro del universo recortado; contra el universo real es mucho menor.

Corrección menor adicional: los 3 sitios de `res.c` descontados son **los tres comentarios** de
`:1829,:1855,:1879`. La doc dice "comentario/declaración `extern`", pero
`extern HANDLE GlobalAlloc2();` (`res.c:1826`) no casa con `\bGlobalAlloc\b` — el `2` es carácter de
palabra.

### 1.4 Clasificación propio / ajeno bajo la regla de enrutado del §4

`OurGlobalAlloc` (`res.c:1833`) → `GlobalAlloc2` (`:1861`) y `OurGlobalReAlloc` (`:1883`) **pasan
`wFlags` verbatim** a Wine; no añaden ni quitan bits. Solo añaden el rechazo `dwBytes > 0x00010000L` y
el reintento `ShrinkSwapArea`/`GrowSwapArea`. Por tanto **los flags del sitio de llamada son la
clasificación completa** — no hay indeterminación por el hub.

Header realmente en efecto: **Wine, no `qwindows.h`**. `wordtech/word.h:38-42` bajo `#ifdef OPUS_X64`
(definido en `src/CMakeLists.txt:1063`) toma `opus_x64_compat.h` → `<windows.h>` de Wine. La rama
`qwindows.h` es el `#else`. Los documentos citan `qwindows.h:1739-1744` como origen de los valores;
coinciden numéricamente en los bits de enrutado, pero la provenance está mal. Divergencia real entre
ambos headers: `GMEM_DISCARDABLE` = `0x0F00` (qwindows) vs `0x0100` (Wine, `winbase.h:479`) — no afecta
al enrutado, sí al `Assert(f & GMEM_DISCARDABLE)` de `idle.c:784`.

Macros compuestos resueltos:

| Macro | Definición | Valor | ¿`OPUS_MEM_FOREIGN`? |
|---|---|---|---|
| `GMEM_DDE` | `dde.h:28` = `DDESHARE\|MOVEABLE` | `0x2002` | sí (DDESHARE) |
| `GMEM_SENDKEYS` | `dde.h:29` = `LOWER\|MOVEABLE` | `0x1002` | sí (LOWER) |
| `gmemLibShare` | `filecvt.h:91` = `MOVEABLE\|DDESHARE` | `0x2002` | sí (DDESHARE) |
| literal `help.c:265,2110` | `MOVEABLE\|SHARE\|NOT_BANKED` | `0x3002` | sí (ambos bits) |
| literal `eldde.c:1261,1263` | `FIXED\|LOWER` | `0x1000` | sí (LOWER) |
| `GHND` | `winbase.h:489` | `0x0042` | no |
| `GMEM_MOVEABLE` | `winbase.h:474` | `0x0002` | no |

Clasificación de los 147 (subconjunto recortado; la clasificación del resto del universo queda
pendiente):

| Clase | Sitios |
|---|---|
| **Ajeno por bit de flag** | **70** — A 29 · `filecvt.c` 22 · `help.c hHlp` 9 · B1-DDE 6 · D-lower 4 |
| **Ajeno por productor** (nunca pasa por `OpusMemAlloc`) | **21** — params de mensaje DDE 5 · `GetClipboardData` 5 · `GetCodeHandle` 4 · `GetPhysicalFontHandle` 5 · … |
| **Propio** | **27** — `eldde.c hKeys` 7 · `spelcore.c` 9 · `etcmd.c` 7 · `raremsg.c` 4 |
| hub `res.c` (clase = la del llamador) | 4 |
| Migrado | 6 — `catalog.c` 4 propio (`GHND`) · `elsubs2.c` 2 **ajeno** (`GMEM_SENDKEYS`) |
| Muerto / fuera de build | 19 |

Los 6 sitios de `opus_asm_misc.cpp`, fuera de esta tabla, son **ajenos por flag** (familia
`GMEM_SENDKEYS`).

**Tres casos donde la regla del §4 clasifica mal:**

1. **`spelcore.c` (9) + `etcmd.c` (7) = 16 sitios.** `GMEM_MOVEABLE` plano → el §4 los enruta al
   `malloc` privado, pero el handle cruza a una DLL Win16 vía `CallOtherStack`
   (`spelcore.c:1306,1315`, `etcmd.c:1202`). Un puntero de `malloc` entregado a una DLL que espera un
   `HGLOBAL` es el modo de fallo que el passthrough existe para evitar, y el enrutado por flag no lo ve.
2. **`raremsg.c:1337→1363→SetClipboardData(CF_TEXT, hdata)` (`:1364`).** `OurGlobalAlloc(GHND)` =
   `0x0042` → camino propio, y el bloque se **cede al portapapeles del sistema**. Mismo problema.
3. **`raremsg.c:1434,1436` (`hdata2`) — procedencia decidida en tiempo de ejecución.**
   `HFedtStripText` (`:1454`) devuelve *o* su `hdata2` propio (`OurGlobalAlloc(GHND)`, `:1478`) *o* el
   `hdata` del portapapeles recibido (`GetClipboardData(CF_TEXT)`, `:1422`; `hdata = hdata2` y
   `return(hdata)` en `:1509`). Ningún análisis estático por flag puede clasificar ese `GlobalFree`.
   El `IsOwn()` por registro del §2 **sí** lo resuelve correctamente — es un argumento a favor del
   mecanismo elegido y en contra de confiar el enrutado solo a los flags.

---

## 2. Ítem 2 — semántica de `Realloc` que reubica el handle

### 2.1 Censo corregido

18 sitios de llamada reales en código compilado (§9.2 lista 9, y su prosa dice "8"). Los omitidos por
§9.2 son precisamente los de las TUs `.C`: `CLIPBRD2.C:444,2076,2081`, `DDESRVR.C:1006`, más
`rtfout2.c:897`. Fuera de build: `debug/debugwin.c:350`, `profwin.c:456`,
`OpusEtAl/tools/src/convtest/conv-tst.c` (5), `OpusEtAl/tools/src/draw/ddprint.c:987`.

### 2.2 La afirmación central de §9.2 es falsa para el árbol completo

§9.2 afirma: *"Todos descartan el valor de retorno … ninguno reasigna `h = OurGlobalReAlloc(...)`"*, y
construye sobre eso el argumento de que el patrón demuestra una garantía semántica.

Los 5 sitios omitidos hacen exactamente lo contrario, y con cuidado deliberado:

- `CLIPBRD2.C:2076-2087` — guarda el handle original en `h`, asigna `*ph = OurGlobalReAlloc(*ph,…)`, y
  **restaura `*ph = h` en cada rama de fallo**.
- `CLIPBRD2.C:444-448` y `rtfout2.c:897-901` — mismo rollback vía `hT` / `rhpcchw.h`.
- `DDESRVR.C:1006-1010` — `h = OurGlobalReAlloc(...)`, luego `*ph = h`.

Es decir: **los autores originales sí trataban el handle como potencialmente reubicable donde les
importaba.** Eso invierte el argumento de §9.2. El patrón "descartar el retorno" no es evidencia de una
garantía; es evidencia de dos convenciones distintas conviviendo en el mismo árbol.

### 2.3 Verificación empírica de la premisa

§9.2 deduce de la semántica Win32 que un bloque `GMEM_MOVEABLE` nunca cambia de `HGLOBAL` en un
realloc. No estaba medido. Se midió, con sondas `winegcc` contra la misma toolchain del proyecto
(código en scratchpad temporal, nada escrito en el repo), Wine 10.0 (Debian `10.0~repack-6`):

- `0x0002`, `0x0042`, `0x2002` (`GMEM_DDE`), `0x1002` (`GMEM_SENDKEYS`): handle **estable** en todos los
  reallocs, incluido crecer 8 B → 1 MB.
- Estrés: 400 reallocs consecutivos con churn de 256 handles vivos → handle cambió **0 veces**.
- Realloc con el bloque bloqueado: handle igual, **puntero sí se movió** (`0x7ffffe223490` →
  `0x7ffffe7a0040`). Ningún sitio del censo hace realloc con un `lp*` vivo — comprobado uno a uno.
- `GMEM_MODIFY` (`idle.c:786,790`): handle estable. Confirma que esos dos sitios son inocuos (cambio de
  atributos, no reasignación).
- **`GMEM_FIXED` (`0x0000`) y `0x1000` sin `MOVEABLE`: `GlobalReAlloc` falla siempre**, `err=8`
  (`ERROR_NOT_ENOUGH_MEMORY`), incluso reduciendo tamaño. Sin `GMEM_MOVEABLE` no es que el handle pueda
  cambiar: es que la llamada no funciona.
- Patrón `filecvt.c` (alocar `0x2002`, reallocar `0x0002`): mismo handle, sin error.

**Conclusión: la premisa de §9.2 es correcta en esta versión de Wine**, con dos advertencias que el
documento no recoge. (a) Es observación empírica sobre Wine 10.0, no un contrato encontrado en
documentación local — si Wine cambia su tabla de handles moveables, los sitios de riesgo se rompen en
silencio y no hay test que lo detecte. (b) El razonamiento correcto no es "`GMEM_MOVEABLE` garantiza
estabilidad" sino "en Wine el `HGLOBAL` moveable es la dirección de una entrada de tabla desacoplada
del bloque" — propiedad de implementación, no de la API.

### 2.4 Sitios de riesgo real

Criterio: retorno descartado **y** el valor obsoleto queda en un almacén que otro consumidor lee.

1. **`eldde.c:1239` (+ `:1176`/`:1229`) — `*lphevtHead`.** Campo de `struct DRVDATA` (`dde.h:302`) al que
   apunta el `FAR*` global `lphevtHead` (`eldde.c:41`, fijado en `:1270`). Leído desde **4 TUs**:
   `elsubs2.c:326-339` (ya migrado a `OpusMemLock`/`Unlock`), `quit.c:805-817`,
   `port/original/opus_asm_misc.cpp:241-269` (que además escribe `*lphevtHead = nullptr`). Ninguna
   cachea el handle — todas releen el campo — así que una actualización correcta se propagaría sola;
   el defecto es que `eldde.c` no actualiza el campo.
2. **`filecvt.c:1469` y `:1488` — `FCopySzToGhsz`/`FCopyStToGhsz`.** El handle llega **por valor**
   (`HANDLE ghsz;`) y la función devuelve `BOOL`. **No existe camino sintáctico** por el que un handle
   nuevo pueda volver al llamador. Los almacenes reales son
   `vpexcr->ghszFn/ghszSubset/ghszVersion` (`:1116,1120,1123,1265,1269`), que se pasan después al DLL
   convertidor externo. Peor caso del árbol: no es "olvidaron asignar", es que la firma no lo permite —
   y la firma está en el árbol restringido.
3. **`spelcore.c:687` — `pghd->ghsz`.** Realloc en `spelcore.c`, estructura leída y reutilizada en
   `SPELL.C:345,626,950,1067,1216,1249,1261,1299-1300`. Cruce de TU literal.
4. **`etcmd.c:1131,1258` y `filecvt.c:1336`** — una sola TU dentro de `Opus/`, pero el handle cruza al
   DLL externo. Mismo tipo de riesgo, distinta frontera.

Matiz que conviene no perder: en los sitios 2-4 el valor obsoleto queda **en el propio almacén
canónico** (`pghd->ghsz`, `vpexcr->ghBuff`). Si el handle cambiara, esos sitios estarían rotos incluso
sin cruce de TU; el cruce solo amplía el radio.

**Modo de fallo, medido:** `GlobalLock` sobre un handle obsoleto devuelve `NULL` con `err=6`
(`ERROR_INVALID_HANDLE`) — no crashea. Pero `DEBUG` no está definido en este build, así que `Assert(f)`
expande a nada y `AssertDo(f)` a `(f)` (`Opus/DEBUG.H:98,100`): `eldde.c:826,1178`
(`Assert(lpevt!=NULL)`) y `filecvt.c:1472,1491` (`AssertDo(lpsz = GlobalLock(ghsz))`) **no comprueban
nada en runtime** → desreferencia de `NULL` inmediata. `etcmd.c`/`spelcore.c` sí comprueban de verdad.

### 2.5 Mecanismo de invalidación entre TUs: no existe

Búsqueda sobre `src/core/` y `src/port/` (`invalidat|notify|notificac|re-?sync|stale`): un único hit, y
es un comentario sin relación (`OpusShellSpine.h:20`). `OpusMemSetPassthrough` y
`OpusMemPassthroughOps` **no existen en el árbol** (cero ocurrencias en `src/`) — el diseño no está
implementado. El único acoplamiento existente es accidental: todos los lectores de `*lphevtHead` y
`pghd->ghsz` releen el campo compartido en cada uso en vez de cachear el handle.

### 2.6 Lo que §9.2 no cubre: comportamiento de `OpusMemRealloc`

Camino propio, verificado en código (`src/core/src/OpusShellMemory.cpp:102-122`): **`OpusMemRealloc`
nunca puede devolver un handle distinto del de entrada.** La identidad es el `OpusHandleImpl*`;
`std::realloc` solo cambia `h->ptr` (`:119`) y la función termina en `return h;` (`:121`). Descartar el
retorno es formalmente seguro hoy — pero por un detalle de implementación que **nadie ha escrito como
invariante en `OpusShellMemory.h`**.

Camino passthrough: `ops.Realloc` devuelve lo que devuelva la `GlobalReAlloc` real.

**Ambigüedad de especificación que conviene cerrar antes de implementar:** §4 titula su bloque de código
"`OpusMemAlloc`/`OpusMemRealloc`" pero solo muestra el cuerpo de `OpusMemAlloc`. Si alguien implementa
el enrutado de `Realloc` **por flags** (como sugiere ese encabezado) en vez de por `IsOwn(h)` (como dice
§2.2), un bloque DDESHARE ajeno se reallocaría por el heap privado. Y hay un caso real que lo dispara:
los bloques `vpexcr->ghszFn/ghszSubset/ghBuff/ghszVersion` **nacen** con `gmemLibShare` = `0x2002`
(`filecvt.c:491,495,499,503`) pero se **reallocan** con `GMEM_MOVEABLE` = `0x0002`
(`:1336,1469,1488`) — pierden el bit `0x2000` en el realloc.

**Hallazgo colateral, preexistente e independiente del passthrough:** `struct GHD` está definida dos
veces con anchos distintos. `etcmd.c:160-164` declara `HANDLE ghsz;`; `splshare.h:64-68` — la que ven
`spelcore.c` y `SPELL.C` — declara **`unsigned ghsz;`**, 32 bits, sin guarda `OPUS_X64`/`__GNUC__`. Los
handles de Wine en este build son de 64 bits (`0x7ffffe350008` medido), así que `spelcore.c:681,687` y
`SPELL.C:1300` truncan.

---

## 3. Ítem 3 — punto de instalación de `OpusMemSetPassthrough`

### 3.1 Verificación de §9.3

| Afirmación de §9.3 | Estado |
|---|---|
| `OpusRegisterOriginalDialogCallbacks()` en `opus_sdm_runtime.cpp:2416-2428`, 7 punteros | **cierta, exacta** (firma en 2416, asignaciones 2421-2427, `}` en 2428) |
| Se llama en `opus_original_startup_probe.cpp:461-465` | **cierta, exacta** (única llamada del árbol) |
| Ese es el `wWinMain` real de WORD1, `WORD1_SOURCES` en `src/CMakeLists.txt:1239` | **cierta** — y es la **única** fuente C++ del target (`:1247`) |
| Orden: filtros de excepción → `ResetRibbonTrace()` → callbacks (461) → `OpusOriginalWinMain` (473) | **cierta, exacta** (452, 453, 454-456, 457, 461-465, 473-474) |
| Línea 473 es el primer punto que ejecuta `Opus/` | **cierta** — `OpusOriginalWinMain` = `wproc.c:451`; su primer statement `InitApploader()` (`wproc.c:468`) es un stub vacío (`opus_win16_platform.cpp:51-53`); el primer código original real es `FInitSegTable(sbMax)` (`wproc.c:488`) |
| "`opus_product_entry.cpp` es un target de verificación de enlace distinto" | **falsa en la caracterización.** Ese archivo **no está en ningún target**: cero referencias en `src/CMakeLists.txt`, `src/core/CMakeLists.txt`, `src/port/tools/host/CMakeLists.txt` y `*.cmake`. Es un huérfano que no se compila. La conclusión (que no es el entry de WORD1) sí es correcta |

### 3.2 Demostración de que nada de `Opus/` corre antes de `wWinMain`

Censo empírico, no por inspección: `objdump -h` sobre **todos** los objetos construidos de los tres
targets del grafo de enlace de WORD1, buscando sección `.init_array`. Solo tres TUs la tienen:

- `opus_x64_runtime.dir/port/original/opus_sdm_runtime.cpp.o`
- `opus_original_engine.dir/port/original/opus_asm_file2.cpp.o`
- `opus_original_engine.dir/port/original/opus_win95_chrome.cpp.o`

Desensamblando `__static_initialization_and_destruction_0()` en cada uno y leyendo las reubicaciones,
las únicas llamadas externas son constructores por defecto de libstdc++ (`unordered_map`, `vector`,
`basic_string`) y `__cxa_atexit`. **Ninguno llama `GlobalAlloc`, `GlobalLock`, `OpusMem*` ni ninguna
función de `Opus/`.** Contenedores vacíos por defecto no asignan.

Cobertura: las 16 TU `.cpp` de `opus_original_engine` y las 16 de `opus_x64_runtime` están todas
escaneadas. Las ~191 TU restantes del engine son **C**, y C no admite inicialización dinámica de objetos
con duración estática — garantía de lenguaje, no observación.

- `opus_original_startup_probe.cpp` no tiene constructores globales; su único `static` local
  (`writing_exception`, `:371`) es `static volatile LONG = 0` (inicialización constante).
- `libopus_shell_memory.a` y `libopus_shell_config.a`: **cero** `.init_array`. `PtrRegistry()`
  (`OpusShellMemory.cpp:39-41`) es un static local de función, perezoso — sin dependencia de orden de
  inicialización estática.
- **`__attribute__((constructor))` / `DllMain` / `DLL_PROCESS_ATTACH`: cero ocurrencias** en todo el
  árbol.
- Entry point: sin override. `toolchain-winelib.cmake` no toca entry/init;
  `target_link_options(WORD1 PRIVATE -mwindows -municode)` (`:1327`) selecciona el entry ancho estándar
  de `winecrt0` → `wWinMain`. `GenerateWord1Spec.cmake` emite solo `@ cdecl` (427 exports), ninguna
  directiva `init`.

Caminos de las categorías A/B1/B2/B3: registro de clases y `WM_CREATE` ocurren dentro de
`FInitWinInfo()` (`wproc.c:516`), 43 líneas después de `OpusOriginalWinMain`; DDE entrante no puede
llegar antes de que exista una ventana; DLL de conversión y WinHelp son `LoadLibrary`/`WinHelp` en
respuesta a comandos, posteriores al bucle de mensajes (`wproc.c:523`).

Los 2 sitios `OpusMem*` de hoy tampoco son alcanzables temprano: `catalog.c` es gestión documental por
comando; `elsubs2.c:330,337` (`FSendKeysPending`) tiene como único llamador `elsubs2.c:415` y está
guardado por `if (lphevtHead != NULL && *lphevtHead != NULL)` — `lphevtHead` está en BSS (NULL) hasta
`eldde.c:1270`.

### 3.3 Propuesta

**`src/port/original/opus_original_startup_probe.cpp`, función `wWinMain`, nueva línea entre la 456
(`#endif`) y la 457 (`ResetRibbonTrace();`).** Estrictamente anterior al punto de §9.3 (entre 465 y 473):

1. Primer punto tras los manejadores de excepción (452-456), así que un fallo dentro de la propia
   instalación queda capturado por `WriteCrashStack`/`ObserveVectoredException`.
2. Cubre también cualquier código que se inserte en el futuro entre 457 y 473 sin re-auditar el orden.
3. `OpusMemSetPassthrough(&kWineMemOps)` es un único store a un puntero global sin efectos secundarios;
   adelantarlo tres statements no tiene coste.

**Salvedad que §9.3 no menciona:** la rama `--self-test` (`:411-450`) hace `return` **antes** de la 452,
así que el passthrough no se instala en ese camino. Hoy es inocuo — solo hace
`GetModuleHandleW`+`GetProcAddress` — pero es el camino que ejecuta `word1_port_smoke_test`
(`src/CMakeLists.txt:1449`, `WORD1 --self-test`). Si esa rama llegara a llamar código de `Opus/`, la
instalación tendría que subir a la línea 411. Merece un comentario de una línea.

**Dos requisitos de CMake que §9.3 no menciona y que son bloqueadores reales:**

1. **WORD1 no tiene `src/core/include` en su include path.** El bloque
   `target_include_directories(WORD1 ...)` (`:1266-1270`) lista solo `OPUS_CASE_SHIM_INCLUDE_DIRS`,
   `generated/original` y `port/original`. Verificado con `ninja -t commands`: el comando real de
   `opus_original_startup_probe.cpp.o` no lleva `-I…/src/core/include`. `catalog.c`/`elsubs2.c` lo
   encuentran porque `OPUS_ORIGINAL_INCLUDE_DIRS` (`:1047-1057`) lo incluye en `:1051`, y ese conjunto lo
   usan `opus_original_engine` y `opus_x64_runtime`, no WORD1.
2. **`WORD1_SOURCES` (`:1239`) es incondicional**, así que un archivo nuevo añadido ahí entraría también
   al build MSVC. Debe añadirse dentro de un `if(OPUS_WINELIB_BUILD)`, como ya se hace con `.spec`/`.res`
   en `:1322`.

**El guard `#if defined(__GNUC__) && !defined(_MSC_VER)` es obligatorio, y por una razón más fuerte que
la de §9.3.** No es que "no tendría consumidor" en MSVC: es que **no enlazaría**.
`opus_original_startup_probe.cpp` sí entra en el build MSVC (`WORD1_SOURCES` y `add_executable` son
incondicionales; los presets `x64-debug`/`x64-release` existen en `src/CMakePresets.json:23,28`, y el
propio archivo tiene bloques `#if defined(_MSC_VER)` en `:2-8`, `:267-278`, `:454-456`). Pero
`opus_shell_memory` solo se declara y enlaza dentro de `if(OPUS_WINELIB_BUILD)` (`:272-275`,
`:1254-1257`), así que bajo MSVC la librería no existe y una llamada sin guardar sería un símbolo sin
resolver. Convención ya establecida por los 2 archivos migrados: guardar **también el `#include`**
(`catalog.c:19-21`, `elsubs2.c:7-9`).

### 3.4 Otros targets que enlazan el contrato

**Un solo punto de instalación, en WORD1, es suficiente.** WORD1 (`:1256`) es el único binario que
ejecuta código de `Opus/`. Los targets `opus_original_sttb_test`, `opus_original_plc_test`,
`opus_sdm_cab_test`, `opus_x64_runtime_test` enlazan `opus_x64_runtime` pero no el engine, así que no
arrastran ningún sitio `OpusMem*`; el fallo controlado (`ops == NULL` → `NULL`) es correcto ahí.
`opus_word1_ui_test` lanza `WORD1` como proceso hijo, no lo enlaza. `src/core/CMakeLists.txt` no define
ningún test para `opus_shell_memory`.

**Rotura de enlace latente detectada de paso** (preexistente, no la introduce el passthrough): el target
`opus_original_startup_probe` (`:1338-1346`, `EXCLUDE_FROM_ALL`) enlaza `opus_original_engine` — que
contiene `catalog.o`/`elsubs2.o` con referencias a `OpusMemAlloc`/`Lock`/`Unlock`/`Free` — **sin**
`opus_shell_memory`. No se manifiesta porque ningún preset lo construye.

---

## 4. Ítem 4 — diseño de la prueba de round-trip

### 4.1 Correcciones a §9.4

1. **`wineg++` compila `.c` como C++.** Verificado: `wineg++` sobre `int *p = malloc(4);` da
   `error: invalid conversion from 'void*' to 'int*'` y `In function 'int main()'`. El comentario de
   `handle_check.c:14-18` ("la fuente sigue siendo C") es inexacto. Restringe el código del test nuevo,
   o exige `winegcc` + `-lstdc++` explícito (como `link-check/run.sh:20`).
2. **`handle-check/run.sh:8` apunta a un directorio de build inexistente en un clon limpio**
   (`../../../../build/linux-winelib-debug/opus_core_build-prefix/…`). El `binaryDir` del preset es
   `${sourceDir}/../out/${presetName}` (`src/CMakePresets.json:35`). Funciona aquí solo por un
   `build/linux-winelib-debug/` obsoleto (CMakeCache del 2026-08-10 21:42 frente a `out/` del 2026-08-11
   04:07). **No copiar esa ruta**: usar `out/linux-winelib-debug/core/lib/libopus_shell_memory.a`, que es
   la que apunta el target IMPORTED (`src/CMakeLists.txt:272-274`).
3. **`run.sh:35-38` no exige 134**; exige `!= 0` y reporta el código. El 134 está solo en comentarios.
4. Los **4 targets `_test` de `8173596`** (`opus_original_sttb_test:1136`, `opus_original_plc_test:1160`,
   `opus_original_strtbl_test:1359`, `opus_x64_runtime_test:1383`, vía la INTERFACE
   `opus_original_c_dialect`) **no son reutilizables**: compilan TU de `Opus/` con
   `-std=gnu89 -funsigned-char -fms-extensions -fpermissive` bajo `$<COMPILE_LANGUAGE:C>`, dialecto
   opuesto a lo que necesita una TU que llama a una lib C++ y a `windows.h`; tres de los cuatro enlazan
   `opus_x64_runtime` y arrastran el bloqueador `disp.h:248`; ninguno enlaza `opus_shell_memory`.
   **Target nuevo.**
5. `CLAUDE.md` está desactualizado sobre el conjunto gating: falta `opus_shell_font_substitution_test`
   (`src/CMakeLists.txt:1436`).

### 4.2 Dos hallazgos medidos que invalidan aserciones propuestas en §9.4

**A. Doble `GlobalFree` en Wine no aborta.** Medido en Wine 10.0: la primera llamada devuelve `NULL`
(éxito), la segunda devuelve **el propio handle** (fallo según contrato Win32) con
`GetLastError() == 0`, y el proceso sigue vivo y sale 0. Trasladar la convención `--double-free`/exit 134
de `handle-check/` al camino foreign produciría una **aserción falsa**.

**B. Desreferenciar un `HGLOBAL` moveable de Wine "funciona" por accidente.** Medido: leer 16 bytes en la
dirección de un handle moveable vivo no falla, y los primeros 8 bytes son un puntero válido al payload
(`0x7ffffe226af0`, mismo rango que `GlobalLock`). Interpretado como `OpusHandleImpl`, el campo `ptr`
(offset 0) cae sobre un puntero plausible y `freed` leyó 0. **Una implementación defectuosa que
desreferenciara handles ajenos pasaría los casos A y B de §9.4 sin inmutarse** — devolvería el puntero
correcto por casualidad. Sin un mecanismo específico, la prueba no prueba la propiedad que dice probar.

**Discriminador fiable, medido:** `GlobalSize` es el único. Sobre un puntero `malloc` nativo (que es lo
que sería un `OpusHandleImpl*`) Wine devuelve `0`, mientras que sobre un `HGLOBAL` real devuelve `cb`.
`GlobalFlags` y `GlobalLock` **no** discriminan: Wine trata un puntero desconocido como bloque fijo y
`GlobalLock` devuelve el propio puntero.

| Situación | `GlobalSize` | `GlobalFlags` | `GlobalLock` |
|---|---|---|---|
| `GMEM_MOVEABLE` vivo, 0 / 1 / 2 locks | `cb` | `0x0000` / `0x0001` / `0x0002` | puntero ≠ handle |
| tras `GlobalFree` | `0` | `0x8000` (`GMEM_INVALID_HANDLE`) | `NULL` |
| puntero `malloc` nativo (≈ `OpusHandleImpl*`) | **`0`** | `0x0000` | **el mismo puntero** |

### 4.3 Decisión: target de CTest nuevo, gating, más `run.sh` espejo

**`opus_shell_memory_foreign_test`**, `add_executable` bajo `if(OPUS_WINELIB_BUILD)`, un binario con
sub-casos por argumento, registrado sin label (gating).

La restricción dura —enlace cruzado `wineg++` contra una lib compilada con g++ nativo y `-fPIC`— **ya
está probada, pero no donde parece**: `src/CMakeLists.txt:1256` hace
`target_link_libraries(WORD1 PRIVATE opus_shell_memory)`, pero **WORD1 nunca ha enlazado** (no existe
ningún `WORD1.exe.so` ni en `build/` ni en `out/`) — ese enlace es declarativo, no evidencia. La
evidencia real es `handle-check/run.sh`, y se reprodujo: `wineg++ -I…/src/core/include probe.c
-L…/out/linux-winelib-debug/core/lib -lopus_shell_memory -o probe` enlaza, corre bajo Wine y sale 0.
Dato nuevo que ninguna sonda existente demuestra: **`windows.h` de Wine y `OpusShellMemory.h` conviven
en la misma TU sin colisión de macros ni de tipos**.

A favor del target frente al script suelto de §9.4:

- **No hereda el bloqueador.** Solo depende de `opus_shell_memory` (IMPORTED) y de `kernel32` (implícito
  en winegcc). Sería el único test *nuevo* capaz de ser gating hoy.
- Precedente exacto de "un binario, N `add_test` por flag": `opus_word1_ui_test` registra 8 tests sobre
  el mismo ejecutable (`:1451-1482`).
- Precedente de test Winelib de consola en CTest: `opus_original_strtbl_test` (`:1428`). CI
  (`.github/workflows/build.yml`) hace `wineboot --init` **antes** del `ctest -LE word1_startup_blocked`,
  y el test es consola pura — **no necesita DISPLAY/Xvfb**.
- **El script suelto no corre en CI.** El camino foreign no es una verificación puntual: es la única red
  que existirá para los ~115 sitios que, según §9.2, viven **permanentemente** en `ops`.

Contrapartida honesta: introduce dependencia de un prefijo Wine funcional para un test gating que hoy no
la tiene en el subconjunto verde. CI ya la satisface; un desarrollador sin `wineboot` vería un fallo
nuevo. Mitigación: `run.sh` espejo en `docs/port-qt/scripts/handle-check-foreign/` compartiendo la misma
fuente. Fuente recomendada en `src/port/tests/` — es la capa a la que pertenece el código que usa
`windows.h`; `src/core/` no puede hospedarlo por construcción (`src/core/CMakeLists.txt:4-7`).

**Lo que no debe hacerse:** extender `opus_shell_config_test`/`opus_shell_font_substitution_test`. Corren
con g++ nativo dentro del ExternalProject; añadirles `windows.h` rompe la invariante de que `src/core/`
no conoce Win32.

### 4.4 Casos y aserciones

Convención `Check(cond, "qué")` + `g_failures` de `OpusShellConfig_test.cpp:32-37`. Cada caso instala su
tabla con `OpusMemSetPassthrough(&ops)` y hace teardown con `OpusMemSetPassthrough(NULL)`.

**Caso A — productor ajeno** (simula `elsubs2.c:330` con `eldde.c` sin migrar; flags reales
`GMEM_SENDKEYS` = `GMEM_LOWER|GMEM_MOVEABLE`, `dde.h:29`):

1. `h = GlobalAlloc(GMEM_LOWER|GMEM_MOVEABLE, PATTERN_LEN)` → `h != NULL`, `GlobalSize(h) == PATTERN_LEN`.
2. `p0 = GlobalLock(h)`; escribir patrón; `GlobalUnlock(h)` → `GlobalFlags(h) & 0xFF == 0`.
3. Instalar `ops`; `p1 = OpusMemLock((OpusHandle)h)` → **`p1 == p0`**, **`memcmp(p1, PATTERN, …) == 0`**,
   **`GlobalFlags(h) & 0xFF == 1`** (prueba que corrió el `GlobalLock` real), `counters.lock == 1`.
4. `OpusMemUnlock` → `GlobalFlags(h) & 0xFF == 0`, `counters.unlock == 1`.
5. `OpusMemSize((OpusHandle)h) == PATTERN_LEN`.
6. `OpusMemHandle(p1) == NULL` — el valor **documentado** en §2.4, no "algo".
7. `OpusMemFree` → `counters.free == 1`; desde fuera `GlobalFlags(h) == 0x8000`, `GlobalSize(h) == 0`,
   `GlobalLock(h) == NULL`.
8. `OpusMemLock` tras el free → `NULL`.

**Caso B — `OpusMemAlloc` con flags foreign** (B1/B2/B3 migrados), dos sub-corridas
(`OPUS_MEM_DDESHARE`, `OPUS_MEM_LOWER`):

1. `h = OpusMemAlloc(64, OPUS_MEM_LOWER)` → `h != NULL`, `counters.alloc == 1`.
2. **`GlobalSize((HGLOBAL)h) == 64`** — aserción central; un `OpusHandleImpl*` daría 0 (medido).
3. Si moveable: **`GlobalLock((HGLOBAL)h) != (void*)h`** — un `OpusHandleImpl*` daría igualdad (medido).
4. Round-trip por el contrato + **verificación cruzada literal**: `pw = GlobalLock((HGLOBAL)h)` desde el
   test → `pw == p` y patrón intacto; lock count `GlobalFlags & 0xFF == 2`.
5. `OpusMemSize(h) == 64`; `OpusMemFree(h)` → `GlobalFlags == 0x8000`, `counters.privateAlloc == 0`.
6. **Control negativo en la misma corrida:** `hp = OpusMemAlloc(64, OPUS_MEM_ZEROINIT)` →
   `GlobalSize((HGLOBAL)hp) == 0` (es privado, no HGLOBAL), `OpusMemLock(hp) != NULL`, `counters.alloc`
   sigue en 1. Asevera que el enrutado por flag discrimina de verdad.

**Caso C — sin `ops`, fallo cerrado:**

1. `OpusMemAlloc(64, OPUS_MEM_DDESHARE) == NULL`; ídem `OPUS_MEM_LOWER`; ídem
   `OPUS_MEM_DDESHARE|OPUS_MEM_ZEROINIT` (el bit foreign manda).
2. Control: `OpusMemAlloc(64, OPUS_MEM_ZEROINIT) != NULL` y los 64 bytes en cero.
3. Consumo de handle ajeno sin ops: `OpusMemLock == NULL`, `OpusMemSize == 0`, y `OpusMemFree` **no**
   debe abortar ni llamar a `GlobalFree` (comprobable: `GlobalFlags(h) != 0x8000`, el bloque sigue vivo).

**Caso D — "un handle foreign nunca se desreferencia como `OpusHandleImpl*`"** (no propuesto en §9.4; es
el que más falta hace, por el hallazgo B de §4.2). Dos mecanismos complementarios:

- **D1, página guardia (decisivo).** Tabla de ops sintética, sin Wine:
  `mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)` — verificado que funciona dentro de un
  binario Winelib. `ops.Alloc` devuelve `(OpusHandle)(guard + 64*i)` y guarda el bloque real en una tabla
  lateral indexada por **valor**; `Lock/Unlock/Size/Free` buscan por valor y nunca desreferencian.
  Implementación correcta → exit 0. Implementación que desreferencia → SIGSEGV en la primera lectura de
  `h->freed`. Se asevera **exit != 0** reportando el código (mismo criterio que `handle-check/run.sh`),
  no un valor fijo.
- **D2, centinela en el bloque real (barato, valida además el camino Wine).**
  `GlobalAlloc(GMEM_FIXED|GMEM_LOWER, 64)` — medido: con `GMEM_FIXED` **el handle ES el puntero al
  payload**, que es exactamente la forma de `eldde.c:1261`. Rellenar los primeros 64 bytes con `0xFF`
  **antes** de pasar el handle al contrato: así los bytes que una desreferencia leería como
  `OpusHandleImpl` son todos `0xFF` sea cual sea el orden de campos (no se codifica ninguna suposición de
  layout). Aserciones: `OpusMemLock((OpusHandle)h) == (void*)h`, `OpusMemSize(...) == 64`, el proceso no
  aborta.

**Caso E — ausencia de doble liberación en el camino foreign.** Test de caracterización, no de aborto
(ver hallazgo A de §4.2). La aserción fuerte y que sí protege es la de conteo: **cada `OpusMemFree`
produce exactamente un `GlobalFree`, ni cero ni dos**, y `GlobalFlags(h) == 0x8000` después. Un segundo
`OpusMemFree` se asevera contra **el contrato que se decida** (reenviar siempre, o llevar registro de
handles foreign vivos) — la aserción que no debe escribirse es "aborta", porque no ocurre.

### 4.5 Mock: dos tablas, no una

- **`opsWine` — envoltorio real + instrumentación.** Cada entrada llama al `Global*` real y registra
  contadores y `lastFreeReturn`. Da a la vez el round-trip de datos, la validación post-free vía
  `GlobalFlags == 0x8000`, la observación del lock count (que es en la práctica una aserción de que el
  par Lock/Unlock se conservó al cruzar), y el conteo exacto de liberaciones.
- **`opsGuard` — sintética, handles en página `PROT_NONE`** (caso D1). Su valor no es la fidelidad sino
  convertir la desreferencia indebida en un fallo duro y determinista. Beneficio colateral: demuestra que
  la firma `OpusMemPassthroughOps` es implementable por algo que no es Win32, que es el argumento de
  independencia del núcleo.

**No recomendado:** una tabla puramente contadora que no asigne nada. Sería un test de la aritmética de
`IsOwn()`, no del camino foreign.

**Cobertura extra casi gratis:** §6.5 del diseño señala que un error de reconstrucción de flags en
`src/port/` sería invisible desde `src/core/`. Si el target de test compila **la TU de producción**
(`opus_mem_passthrough_ops.cpp`, §3.3) en vez de una tabla propia para los casos A/B, y la
instrumentación se añade con una tabla *decoradora* que la envuelve entrada por entrada, el test cierra
también §6.5 y el objeto bajo prueba es el de verdad. Recomendado diseñarlo así desde el principio.

### 4.6 Qué no puede cubrir esta prueba

1. **La colisión de valores de puntero de §2.3.** No es provocable de forma determinista. Lo medido
   (`HGLOBAL` en `0x7ffffe……` vs `malloc` nativo en `0x55……`) es layout del preloader de Wine + ASLR, no
   un contrato; aseverar disyunción de rangos fallaría espuriamente si Wine cambia su mapa.
2. **Comportamiento de Wine ante doble `GlobalFree` más allá de lo medido** (si corrompe la entrada de
   tabla, si un handle reutilizado puede colisionar). Requiere instrumentar Wine, no un test negro.
3. **El punto de instalación (§3.3).** El test llama `OpusMemSetPassthrough` en su `main`; no puede
   demostrar que `wWinMain` lo instala antes de la primera TU de `Opus/`. Eso necesita una aserción de
   orden dentro de `WORD1`, hoy inalcanzable por el bloqueador de arranque.
4. **Compartición DDE real entre procesos.** `GMEM_DDESHARE` se prueba como bit de enrutado y como bloque
   local; que sea efectivamente compartible con otro proceso es otro experimento.
5. **Que los ~115 call sites usen bien el contrato.** Verifica el mecanismo, no la migración.

---

## 5. Cosas verificadas que sí siguen en pie

Para que el balance no quede sesgado: la mayor parte de §9 resiste la auditoría.

- §9.1 (ítem 1 tal como estaba planteado, solo `GlobalHandle`): **confirmado**. `GlobalHandle` aparece
  una sola vez en todo el árbol, `ripaux.c:52`, fuera de build. La pregunta abierta de §2.4 sigue sin
  materializarse.
- §9.2, corrección `GMEM_SENDKEYS`/`GMEM_LOWER`: **confirmada**. `dde.h:29` es
  `(GMEM_LOWER|GMEM_MOVEABLE)`; los sitios `eldde.c:805,821,1176,1240,1321` lo usan;
  `AllocSelector`/`PrestoChangoSelector`/`FreeSelector` (`eldde.c:1299-1312`) operan solo sobre
  `hPlaybackHook`. La conclusión de que **A permanece permanentemente en el camino foreign** es correcta,
  y la decisión de no reclasificar a D también.
- §9.2, premisa de estabilidad del handle bajo `GMEM_MOVEABLE`: **confirmada empíricamente** (§2.3), con
  las dos advertencias registradas allí.
- §9.3, todas las referencias de archivo y línea salvo la caracterización de `opus_product_entry.cpp`:
  **confirmadas exactas**.
- §5 del diseño (impacto en los 6 sitios migrados): **confirmado**. `catalog.c` usa
  `OpusMemFlagsFromWin16(GHND)` = `0x0042` → propio; `elsubs2.c` es ajeno.
- `OpusMemFlagsFromWin16` solo traduce `0x0040` (`OpusShellMemory.h:80-86`) y `OpusMemFree` aborta en
  doble free (`OpusShellMemory.cpp:142-161`; el diseño cita 146-154, el bloque es algo más ancho pero la
  afirmación se sostiene).
- Los 12 TU que tocan `OurGlobal*` y los 4 sitios reales de `res.c` (`:1867,1870,1900,1903`):
  **confirmados**.

---

## 6. No verificable

- **"Coincide con la cifra del issue #3"** (censo §1): el issue vive en GitHub, no accesible desde el
  árbol.
- **"las DLL destino son binarios Win16 que este port no puede cargar de todas formas"**
  (`…-blocked-categories-design.md` §4, B2-a): afirmación de runtime. El árbol muestra `LoadLibrary` y
  `CallOtherStack` hacia sus entry points; si Wine los carga no se decide leyendo fuente.
- **"`GlobalLockClip` ya está excluido y asignado al contrato de portapapeles Qt-6 — decisión ya
  existente"**: no aparece registrada en `docs/port-qt/00-inventario-win32.md` ni en los specs leídos.
  Puede estar en conversación fuera del repo. **Relevante**, porque §1.2 muestra 42 sitios de
  `GlobalLockClip` que ningún censo cuenta.
- **Garantía contractual** (no empírica) de que `GlobalReAlloc` con `GMEM_MOVEABLE` preserva el
  `HGLOBAL`: no hay fuente de Wine instalada, solo binarios y headers.
- **Comportamiento de `ops.Realloc`**: no existe código (`OpusMemPassthroughOps`/`OpusMemSetPassthrough`:
  cero ocurrencias en `src/`).
- **Si los DLL externos retienen el valor del handle entre llamadas**: no hay fuente en el árbol. El único
  convertidor de prueba (`OpusEtAl/tools/src/convtest/conv-tst.c`, no compilado) reproduce el mismo patrón
  de handle-por-valor con retorno descartado (`:321-327`).
- **Código de salida exacto bajo Wine cuando el fallo del caso D1 es SIGSEGV**: no medido (habría exigido
  una implementación defectuosa). El test debe aseverar `!= 0`, no fijar 139.
- **Layout interno del `HGLOBAL` moveable de Wine**: los primeros 8 bytes son un puntero al payload en
  Wine 10.0/Debian; no documentado ni estable entre versiones. El diseño de D2 **no depende** de ello.
- **Orden de arranque en ejecución**: no se pudo construir `WORD1`.
  `opus_original_startup_probe.cpp:249` falla aquí con
  `error: 'GetCurrentThreadStackLimits' was not declared in this scope` (no declarada en las cabeceras de
  este Wine; probado también con `-D_WIN32_WINNT=0x0602`, que el proyecto no define en ningún sitio).
  `opus_original_engine` tampoco construye completo: `Opus/wordtech/disp.h:248` →
  `error: flexible array member in union`. Todo el análisis de §3 es por enumeración exhaustiva de
  fuentes y objetos, no por ejecución. La cobertura de las **TU C++** —las únicas capaces de tener
  constructores globales— sí es del 100 %.
- **Quién ejecuta el `.init_array`** bajo Winelib (cargador de Wine en attach vs. `ld.so` en `dlopen`):
  inferencia a partir de tags dinámicos no estándar `0x60009994/95/96` observados en un `.exe.so` ya
  construido. La conclusión de orden no depende de ello.

---

## 7. Puntos abiertos para decisión del mantenedor

**P1 — Alcance del censo (bloqueante para cualquier cifra de progreso).** El universo real de sitios
`Global*` en código compilado es del orden del doble del documentado: +84 en TUs `.C` mayúscula, +4 en
`rtfout2.c`, +6 en `opus_asm_misc.cpp`, +42 de `GlobalLockClip`. Opciones: (a) rehacer el censo completo
con glob insensible a mayúsculas y clasificar los nuevos sitios por categoría antes de implementar nada;
(b) declarar explícitamente que el alcance del issue #3 son las 147 sitios ya censados y que el resto es
trabajo aparte, aceptando que "119/147 = 81%" mide un subconjunto arbitrario. **Recomendación: (a)** —
`CLIPBRD2.C` (39 sitios) y `DDESRVR.C` (15) son portapapeles y servidor DDE, exactamente los subsistemas
de B1/B3, así que casi con certeza cambian el reparto por categoría, no solo el total.

**P2 — Los 16 sitios de `spelcore.c`/`etcmd.c` que el enrutado por flag clasifica como propios pero
cruzan a una DLL Win16, y los 2 de `raremsg.c` que ceden el bloque al portapapeles.** Bajo §4 literal
recibirían un puntero de `malloc`. Opciones: enrutar por subsistema además de por flag; o marcar esos
sitios como excluidos; o aceptar el riesgo apoyándose en que las DLL Win16 no cargan (afirmación **no
verificable**, ver §6). Requiere decisión antes de migrar esas TUs.

**P3 — `filecvt.c:1469,1488` (`FCopySzToGhsz`/`FCopyStToGhsz`). RESUELTO 2026-08-11.**
`…-passthrough-design.md` §4.2 escribe como invariante que `OpusMemRealloc` nunca cambia el handle
(camino propio por construcción; camino foreign porque los flags reales siempre llevan `GMEM_MOVEABLE`,
verificado empíricamente en §2.3). El invariante convierte "la firma no puede propagar un handle nuevo"
de riesgo latente a hecho irrelevante — nunca hay un handle nuevo que propagar. No se toca la firma de
`src/Opus/`, no hace falta autorización.

**P4 — ¿Se escribe como invariante que `OpusMemRealloc` nunca cambia el handle? RESUELTO 2026-08-11.**
Escrito en `…-passthrough-design.md` §4.2: invariante de contrato, no detalle de implementación —
exigido en ambos caminos (propio y foreign), con la obligación explícita de que `gOps.Realloc` falle
controladamente (`NULL`) si algún día recibe un flag sin `GMEM_MOVEABLE`, en vez de reubicar en
silencio. Los ~14 sitios que descartan el retorno quedan formalmente correctos.

**P5 — Ambigüedad de §4 sobre el enrutado de `Realloc`. RESUELTO 2026-08-11.**
`…-passthrough-design.md` §4.1 añade el cuerpo de `OpusMemRealloc` explícitamente, con enrutado por
`IsOwn(h)` y **nunca** por flags — cita el caso real que lo disparaba (`filecvt.c`: nace `0x2002`,
realloca `0x0002`) como motivación en el propio texto.

**P6 — ¿El test foreign es gating?** Recomendado sí, pero introduce dependencia de un prefijo Wine
inicializado para el conjunto gating. CI ya lo satisface (`wineboot --init` antes del `ctest`); un
desarrollador sin prefijo vería un fallo nuevo. Nota: el conjunto gating **ya está rojo localmente**
(faltan binarios de `opus_original_sttb_test`, `opus_original_plc_test`, `opus_x64_runtime_test`,
`opus_sdm_cab_test` por el bloqueador `disp.h:248`).

**P7 — Contrato de doble `OpusMemFree` sobre handle foreign.** Wine no aborta ni diagnostica (§4.2 A). O
el núcleo reenvía siempre y se documenta que no hay red, o lleva un registro de handles foreign vivos y
la puede dar. Afecta a qué asevera el caso E y al riesgo 4 del §6 del diseño.

**P8 — `struct GHD` con `unsigned ghsz` de 32 bits en `splshare.h:64-68`** frente a `HANDLE ghsz` en
`etcmd.c:160-164`. Trunca handles de 64 bits en `spelcore.c:681,687` y `SPELL.C:1300`. Preexistente e
independiente del passthrough, pero cae justo encima de un sitio de riesgo. ¿Se abre issue aparte?

**P9 — Higiene menor, sin urgencia:** `opus_product_entry.cpp` es un archivo huérfano que no compila en
ningún target (§3.1); el target `opus_original_startup_probe` tiene una rotura de enlace latente (§3.4);
`handle-check/run.sh:8` apunta a un directorio de build obsoleto (§4.1); `CLAUDE.md` omite
`opus_shell_font_substitution_test` del conjunto gating (§4.1).
