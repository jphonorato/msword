# Censo rehecho (P1) — universo real de la familia `Global*` en código compilado

**Fecha:** 2026-08-11
**Estado:** censo verificado contra el árbol; los 3 puntos abiertos de §5 (subsistema de imagen, 9 indeterminados, `GlobalLockClip`) cerrados en §6 con lectura de ciclo de vida completo, sin categoría nueva ni cambio de mecanismo. Ningún archivo de código modificado. Sin `git add` de código.
**Responde a:** P1 de `2026-08-11-opus-memory-passthrough-checklist-audit.md` §7 — "rehacer el censo completo con glob insensible a mayúsculas y clasificar los nuevos sitios por categoría antes de implementar nada". Opción (a) elegida, ejecutada aquí.
**Documentos que este censo actualiza/supersede en su tabla de totales:** `2026-08-11-opus-memory-blocked-categories-design.md` §1 (147 sitios) y, por extensión, el "119/147 = 81%" de su §0/§7. No se reescriben esos documentos in situ — la decisión de la fila 3 (`c4b2a09`) queda intacta; este censo cambia el denominador, no la decisión ya tomada sobre passthrough como mecanismo.

---

## 0. Método

Mismo regex que el censo original, insensible a mayúsculas, sobre **todo** `src/Opus/` (raíz + `debug/`), sin restringirse a las 17 TU documentadas:

```
grep -cEo '\bGlobal(Alloc|Free|Lock|ReAlloc|Size|Unlock)\b' src/Opus/*.c src/Opus/*.C src/Opus/debug/*.c
```

Verificación de estado de compilación por archivo: contraste contra `OPUS_ORIGINAL_ENGINE_SOURCES` (`src/CMakeLists.txt:659-867`, 142 entradas), el mapeo de shim de mayúsculas (`:990-991`, `pic3.c→PIC3.C`, `rtfin2.c→RTFIN2.C`) y grep de `#include "*.c"` para encontrar TU incluidas (`elsubs.c→elxprocs.c`, `fieldpic.c→inter.c`, `pic.c→pic3.c`, `RTFOUT.C→rtfout2.c`, `RTFIN.C→rtfin2.c`). `opus_asm_misc.cpp` (`src/port/original/`) se cuenta aparte, ya establecido como Categoría A en la auditoría previa.

## 1. Total corregido

```
src/Opus/*.c + *.C:      237
src/Opus/debug/*.c:       10   (fuera de build, DEBUG no definido)
opus_asm_misc.cpp:          6   (Categoría A, src/port/, no src/Opus/)
─────────────────────────────
Total 6-símbolos, en árbol:  253
Ya documentado (147) cubre:  147  (137 en 14 TU raíz + 10 en debug/)
Nuevo, no documentado:        94  (94 = 237 − 137 − 6 de opus_asm_misc.cpp ya sumados aparte)
```

`profwin.c` (6 ocurrencias) y `ripaux.c` (3) **no** entran en los 253: `profwin.c` no está en ninguna lista de fuentes ni se incluye desde ninguna TU compilada; los 3 de `ripaux.c` (`:248,249,261`) son un `#undef`, una declaración `extern` y una toma de dirección (`&GlobalAlloc`), ninguno es llamada, y el archivo mismo no compila (solo se referencia a sí mismo vía `ripaux.cpt`, que tampoco compila).

`GlobalLockClip` — símbolo aparte, mismo mecanismo (`debugwin.h:209` lo define `GlobalLock` cuando `DEBUG` no está definido): **42 sitios**, todos en código compilado, repartidos en 13 TU (5 nuevas: `CLIPBORD.C` 2, `CLIPBRD2.C` 6, `DDESRVR.C` 7, `RTFRARE.C` 1, `SPELL.C` 1 = 17; 8 ya censadas: `ddeclnt.c` 4, `eldde.c` 4, `etcmd.c` 4, `filecvt.c` 6, `help.c` 2, `raremsg.c` 2, `rtfout2.c` 2, `spelcore.c` 1 = 25). Universo ortogonal — no suma al 253, tampoco lo resta.

**Universo real combinado, código compilado: 253 (familia `Global*`) + 42 (`GlobalLockClip`) = 295.**

## 2. Los 94 nuevos, por TU y clasificación

Método: `grep` de identificador en el primer argumento de cada llamada (patrón `ident = (Our)?Global(Alloc|ReAlloc)\(` o `Global(Free|Lock|Unlock|Size)\(ident`), comparado contra las familias con nombre de `…-blocked-categories-design.md` §1. Clasificación en tres clases:

- **(a) reutilizable sin categoría nueva** — extiende una familia/decisión ya existente por identificador o mecanismo compartido.
- **(b) requiere categoría nueva** — subsistema sin familia previa.
- **(c) indeterminado** — contexto insuficiente en esta pasada, requiere lectura completa de la TU.

| TU | Sitios | Compilación | Identificador(es) | Clase | Motivo |
|---|---|---|---|---|---|
| `CLIPBRD2.C` | 39 | directa (`ENGINE_SOURCES`) | `hData`, `hDataDescriptor`, `hReturn`, `hBits`, `hDIB`, `h`, `picInfo.mfp.hMF` | **(a)** | transferencia de datos de portapapeles (incluye bits de imagen embebida en el flujo de portapapeles) — mismo subsistema que la decisión B3-b ya tomada |
| `DDESRVR.C` | 15 | directa | `hData`, `h` | **(a)** | servidor DDE, mismo identificador `hData` que `eldde.c`/`ddeclnt.c` — extiende B1-b |
| `GRSPEC.C` | 10 | directa | `hData`, `hPict`, `h1`, `h2` | **(b)** | `hPict` es bloque `METAFILEPICT` local (`GRSPEC.C:1607-1628`), sin equivalente en A/B1/B2/B3/D; subsistema gráfico/imagen |
| `PIC3.C` | 5 | vía shim (`pic.c` `#include "pic3.c"` → `PIC3.C`) | `hBits` (1, local, `:287`), `*phMF` (4, `:1859-1906`, vía `GlobalAlloc2`/hub `res.c`) | **(b)** | `hBits` es bloque local sin familia; los 4 de `*phMF` consumen el hub `res.c` igual que `rtfout2.c` (ver fila abajo) — matiz: no es una sola familia, son dos patrones distintos que este censo agrupa por prudencia bajo (b) |
| `PIC2.C` | 4 | directa | `hData` | **(b)** | mismo subsistema de imagen que `GRSPEC.C`/`PIC3.C`; coincide de *nombre* con DDE (`hData`) pero el dominio es gráfico, no mensajería — no se trata como (a) por esa sola coincidencia |
| `SPELL.C` | 3 | directa | `ghsz`, `pghd->ghsz` | **(a)** | campo **idéntico** al de `spelcore.c`, ya B2 |
| `SCREEN2.C` | 3 | directa | `h` | **(c)** | patrón autocontenido, un solo identificador local, sin lectura completa de la TU |
| `RTFRARE.C` | 2 | directa | `hData` | **(c)** | un solo símbolo (`GlobalSize`), contexto insuficiente |
| `CLIPBORD.C` | 2 | directa | `hps`, `hrc` (vía `GlobalUnlock`, contexto `GlobalLockClip`) | **(a)** | mismo mecanismo de portapapeles que decisión B3-a existente |
| `LOADFONT.C` | 1 | directa | `hfontPhy` | **(a)** | identificador **idéntico** al de `idle.c`, ya D (→ `OpusShellFontMetrics`) |
| `rtfout2.c` | 4 | vía `#include` (`RTFOUT.C:2222`) | `h` | **(c)** | consume el hub `res.c` (como los 4 de `PIC3.C`), sin familia asignada en ningún documento |
| `opus_asm_misc.cpp` | 6 | directa (`ENGINE_SOURCES:857`) | `lphevtHead`, `lphrgbKeyState` | **(a)** | ya establecido, Categoría A |
| **Total** | **94** | | | | (a) 66 · (b) 19 · (c) 9 |

## 3. Tabla de totales corregida (reemplaza la de §1 de `…-blocked-categories-design.md`) — redistribución final, ver §6

| Categoría | Documentado (147) | + Nuevo (94) | Total |
|---|---|---|---|
| Migrado | 6 | 0 | 6 |
| A — familia compartida entre TU | 29 | +6 (`opus_asm_misc.cpp`) | 35 |
| B1 — DDE entre procesos | 14 (corregido, ver checklist-audit §1.3) | +15 (`DDESRVR.C`) | 29 |
| B2 — DLL externa vía `CallOtherStack`/carga directa | 38 | +4 (`SPELL.C` +3, `GRSPEC.C` `h2`/`hMF` +1 — §6.1) | 42 |
| B3 — WinHelp / portapapeles | 17 (corregido) | +41 (`CLIPBRD2.C`+`CLIPBORD.C`) | 58 |
| D — segmento/selector Win16 | 13 (corregido) | +1 (`LOADFONT.C`) | 14 |
| `res.c` — hub | 4 | 0 | 4 |
| Local migrable hoy (`hKeys`) | 7 | 0 | 7 |
| **B1/B3 — dual DDE/portapapeles (nuevo, §6.2)** | 0 | **6** (`RTFRARE.C` 2 + `rtfout2.c` 4) | **6** |
| **Local/propio migrable (nuevo, §6.1+§6.2)** | 0 | **21** (`GRSPEC.C` `hData`+`h1`+`hPict` 9 + `PIC3.C` 5 + `PIC2.C` 4 + `SCREEN2.C` 3) | **21** |
| Muerto / fuera de build | 19 (corregido) | 0 | 19 |
| **Total** | **147** | **94** | **241** |

Las filas "Nuevo — subsistema imagen/gráficos (sin categoría)" e "Indeterminado" de la versión anterior de esta tabla quedan cerradas: sus 19+9=28 sitios se redistribuyeron arriba con evidencia de código (§6), sin dejar ninguno sin categoría y sin inventar ninguna categoría nueva.

`GlobalLockClip` (42 sitios, universo ortogonal — no suma al 241) ya no se trata como bloque aparte. Desglosado por categoría, verificado identificador por identificador en §6.3: **15 B1, 12 B2, 12 B3, 3 B1/B3 dual** (`RTFRARE.C`/`rtfout2.c`) — cero sitios huérfanos. Si se decide sumarlo al recuento tratado: **241 + 42 = 283**, ahora con los 42 ya categorizados, no como bloque indiferenciado.

## 4. Consecuencia sobre la decisión ya tomada (fila 3, `c4b2a09`)

La decisión de **mecanismo** (passthrough completo, A-2/B1-b/B2-b/B3-b/D-2/R-4) no cambia — ninguno de los 94 nuevos introduce un patrón de flags que ese mecanismo no maneje ya (B1/B2/B3/D/A siguen siendo suficientes para el 87% de lo nuevo: 66+... en realidad 66/94 = 70%, más 42 `GlobalLockClip` que también caen en B3 → (66+42)/(94+42) = 79%). Lo que cambia es el **denominador y el peso relativo**:

- "119/147 = 81%" pasa a, como mínimo, **119+66+42 = 227 / 241+42 = 283 ≈ 80%** si se asume que el subsistema de imagen (19) y lo indeterminado (9) terminan clasificados como D-like (fuera del passthrough) — el peor caso para el porcentaje.
- Si el subsistema de imagen y lo indeterminado terminan siendo passthrough-compatibles (B-like), el porcentaje sube, no baja.
- **B1 y B3 casi se duplican** (14→29, 17→58) solo por `DDESRVR.C` y `CLIPBRD2.C`/`CLIPBORD.C`. Esto no cambia la decisión de *cómo* tratarlas (siguen siendo B1-b/B3-b), pero sí el volumen de trabajo de migración de call-sites detrás de esa decisión — relevante para estimar esfuerzo, no para revisar el mecanismo.

## 5. Puntos abiertos que quedan, actualizados

- **P1 queda resuelto en cuanto a cifra** (este documento). Sigue pendiente decidir qué hacer con el subsistema de imagen (19 sitios, sin categoría) y los 9 indeterminados — eso es trabajo nuevo, no cubierto por P2.
- Nueva pregunta, no formulada antes: ¿el subsistema de imagen/gráficos (`GRSPEC.C`, `PIC2.C`, `PIC3.C`, más los `*phMF`/hub de `PIC3.C` y `rtfout2.c`) es candidato a **Categoría D** (sin equivalente en heap nativo, destino `OpusShellFontMetrics`-like) o a **B2** (cruza a GDI/impresión, no a una DLL Win16, pero mismo patrón de bloque efímero)? No decidible por censo — requiere leer el ciclo de vida completo de `hPict`/`hBits`/`*phMF` en las tres TU.
- No se verificó ausencia de comentarios/declaraciones dentro de los 94 sitios nuevos con el mismo rigor que se hizo para los 147 originales (`res.c`, `ripaux.c`). Riesgo acotado: los patrones capturados son mayormente `ident = OurGlobalAlloc(...)` y `Global*(ident` dentro de cuerpo de función, forma que rara vez aparece en comentario; no confirmado línea por línea.

---

## 6. Cierre de los tres puntos abiertos de §5 (2026-08-11, lectura de ciclo de vida completo)

**Estado:** los tres puntos que §5 dejaba pendientes por "contexto insuficiente"/"no decidible por censo" se resolvieron leyendo el cuerpo completo de las funciones involucradas en `GRSPEC.C`, `PIC2.C`, `PIC3.C`, `SCREEN2.C`, `RTFRARE.C` y `rtfout2.c`, más los 42 sitios de `GlobalLockClip`. **No se necesitó ninguna categoría nueva.** No se toca la decisión de mecanismo (`c4b2a09`/`cfc59d1`, passthrough completo) — esto es redistribución de censo, igual que el resto de este documento.

### 6.1 Subsistema de imagen/gráficos (19 sitios) — no era un solo mecanismo

La pregunta de §5 estaba planteada como disyuntiva (¿D o B2?). Leyendo el ciclo de vida completo, ninguna de las dos encaja para la mayoría de los sitios — hay dos mecanismos distintos mezclados por el censo bajo una sola etiqueta:

**`GRSPEC.C` `h2`/`hMF` (1 sitio, `GlobalFree(h2)` en `GRSPEC.C:180`) → B2, sin ambigüedad.** `HReadStPict()` → `HReadPgribSt()` (`GRSPEC.C:1524-1637`) carga una DLL externa de conversión gráfica y llama a su punto de entrada exportado:

```c
if ((hLib = HOurLoadLibrary(pgrib->szName, NULL)) >= 32) {
    ...
    if (hLib >= 32 && hdc != NULL &&
        (lpfnReadPict = GetProcAddress(hLib, MAKEINTRESOURCE(wProcPict))) != NULL) {
        ...
        wRet = OpusCallReadPict(lpfnReadPict)(hdc, &grfs, &grpi, pgrib->hPref);
        if (!wRet && grpi.hMF != NULL) {
            if ((hPict = OurGlobalAlloc(GMEM_MOVEABLE, sizeof(mfp))) != NULL) {
                ...
                *ph = mfp.hMF = grpi.hMF;   /* handle asignado por la DLL externa */
```

`HOurLoadLibrary` + `GetProcAddress` + llamada a la DLL + handle recibido de vuelta (`grpi.hMF`, asignado a `*ph` = `h2`) es exactamente la misma arquitectura que `filecvt.c` (B2 ya decidida): un handle que otro binario Win16 asignó, no el proceso. `h2` se libera con `GlobalFree(h2)` en el sitio de llamada (`GRSPEC.C:180`) tras usarlo — mismo patrón de "recibir, usar, liberar" que el resto de B2.

**El resto (18 sitios: `hData`+`h1`+`hPict` de `GRSPEC.C` 9, `PIC2.C` completo 4, `hBits`+`*phMF` de `PIC3.C` 5) → local/propio migrable, mismo patrón que `catalog.c`.** Ninguno cruza a un proceso, DLL o selector/segmento:

- **`GRSPEC.C:247-385` (`FImportFnChPic`, `hData`, 6 sitios) y `PIC2.C` (`hData`, 4 sitios):** `hData` se llena leyendo bytes de un **archivo** (`RgbFromVfcStm`) con `GlobalAlloc2`/`OurGlobalAlloc`, se pasa a `FReadPict`, se libera en la misma función. Sin DLL, sin otro proceso — es exactamente el patrón "asignar con datos propios, usar, liberar" de `catalog.c`.
- **`GRSPEC.C:1607-1628` (`h1`/`hPict`, 1+2=3 sitios) y `GRSPEC.C:178` (`GlobalFree(h1)`):** `hPict` es un buffer `METAFILEPICT` que **Word arma** con `OurGlobalAlloc` para envolver los datos que la DLL devolvió (`grpi.rc`, y el propio `h2` guardado como campo `mfp.hMF` dentro de sus bytes) — el handle que de verdad cruza la frontera es `h2`, no `hPict`/`h1`. `h1` es el mismo valor que `hPict` (retornado por `HReadStPict`), así que su ciclo de vida (asignar, usar, liberar) es local, no ajeno.
- **`PIC3.C:260-287` (`hBits`, 1 sitio):** caché de bits de mapa de bits para **pintar/imprimir** (`FDrawPicCacheable`/`FOurPlayMetaFile`, recibe `hdc`/`ww` como parámetros), liberada inmediatamente después de dibujar (`GlobalFree(hBits)`, línea 287). Sin cruce de ningún tipo.
- **`PIC3.C:1845-1911` (`*phMF`, 4 sitios):** caché de metarchivo llenada leyendo bytes **del propio documento** (`FetchPeData(fc, rgch, ...)` — almacenamiento interno de Word, no una fuente externa), verificado en el propio comentario del código: *"Build up all bytes associated with the picture ... into the global Windows handle hMF"*.

### 6.2 Los 9 indeterminados — los tres resueltos, ninguno es subsistema nuevo

- **`SCREEN2.C:1250-1270` (`HLoadRes1`, 3 sitios) → local/propio migrable.** `OurGlobalAlloc` + copia de bytes desde una tabla de recursos **compilada en el binario** (`rgrcds[i].rgchBits`), usada como caché de cursor mientras dura el proceso (`FreeIrcds` la libera al final, `SCREEN2.C:1291-1304`). Mismo patrón "propio" que §6.1.

- **`RTFRARE.C:203-278` (`FReadRTF`, 2 sitios) → B1/B3, consumidor.** El propio comentario del código lo dice explícitamente: *"Read text data from hData into docDest. Used by dde and clipboard"* (`RTFRARE.C:204-207`). `hData` se **recibe** (no se asigna): `GlobalSize`/`GlobalLockClip`/`GlobalUnlock` sobre un handle ajeno de DDE o del portapapeles del sistema — exactamente B1/B3, sin mecanismo nuevo.

- **`rtfout2.c:836-918` (`HWriteRTF`, 4 sitios) → B1/B3, productor.** Mismo comentario textual: *"Write doc... to a windows handle in RTF format. Used by dde and clipboard"* (`rtfout2.c:829-832`). El flag de asignación decide el destino en tiempo de ejecución: `rhpcchw.wAlloc = cbInitial ? GMEM_DDE : GMEM_MOVEABLE;` (`rtfout2.c:858`) — el mismo bit `GMEM_DDE` que ya enruta a `OPUS_MEM_DDESHARE` bajo el mecanismo de passthrough. Es el lado productor del mismo par DDE/portapapeles que `RTFRARE.C` consume.

Se agrupan `RTFRARE.C`+`rtfout2.c` (6 sitios) bajo una fila propia "B1/B3 — dual" en vez de forzarlos dentro de B1 o de B3 puros: el mecanismo real (`GMEM_DDE` condicional según `cbInitial`) sirve a los dos caminos según el llamador, y no hay evidencia de código que permita partir los 6 sitios entre uno y otro sin inventar una proporción — la misma disciplina de no forzar precisión que no está en el código, ya aplicada en `PIC3.C` §2 de este documento.

### 6.3 `GlobalLockClip` (42 sitios) — redistribución por identificador, cero huérfanos

Cada uno de los 42 sitios (listado completo, `grep -rn 'GlobalLockClip' src/Opus/*.c src/Opus/*.C`) cae dentro de un identificador ya clasificado por TU/familia — ninguno queda fuera:

| Categoría | TU (sitios) | Subtotal |
|---|---|---|
| **B1** — DDE | `eldde.c` (`hExec`/`hData`/`wLow`/`hCmds`, 4), `ddeclnt.c` (`hData`/`wLow`, 4), `DDESRVR.C` (`wLow`/`hData`/`h`, 7) | **15** |
| **B2** — DLL externa | `etcmd.c` (`pghd->ghsz`, 4), `filecvt.c` (`ghIniName`/`ghIniExt`/`ghBuff`, 6), `spelcore.c` (`pghd->ghsz`, 1), `SPELL.C` (`ghsz`, 1) | **12** |
| **B3** — WinHelp/portapapeles | `help.c` (`hsz`, 2), `raremsg.c` (`hdata`/`hdata2`, 2), `CLIPBORD.C` (`hps`/`hrc`, 2), `CLIPBRD2.C` (`h`/`hData`/`picInfo.mfp.hMF`, 6) | **12** |
| **B1/B3 dual** | `RTFRARE.C` (`hData`, 1, consumidor — §6.2), `rtfout2.c` (`h`, 2, productor — §6.2) | **3** |
| **Total** | | **42** |

`GlobalLockClip` es literalmente `GlobalLock` en este build (`debugwin.h:209`, ya documentado en §1.1(3) de `…-blocked-categories-design.md`) — el nombre sugiere un mecanismo de portapapeles propio, pero no tiene ninguna semántica distinta hoy: es la misma función, sobre handles que ya pertenecen a una familia conocida. Tratar los 42 como "todo B3" (como sugería con cautela la nota original de §1 de este documento) habría sido incorrecto para 27 de los 42 (B1+B2) — no son sitios de portapapeles, son sitios de DDE y de DLL externa que además de su `Lock`/`Unlock` normal usan esta variante del nombre.

### 6.4 Consecuencia sobre la decisión de mecanismo — sin cambios

Igual que en §4: la decisión de **mecanismo** (passthrough completo, `c4b2a09`/`cfc59d1`) no se toca. Reagrupando el universo final de 241 por si necesita o no la tabla `ops` del passthrough:

- **Requiere `ops` (camino foreign):** A (35) + B1 (29) + B2 (42) + B3 (58) + B1/B3 dual (6) = **170**.
- **No requiere `ops` (camino propio, incluye lo migrado y lo recategorizado en §6.1/§6.2):** Migrado (6) + `res.c` hub (4) + `hKeys` (7) + Local/propio nuevo (21) = **38**.
- **D, sin passthrough, destino ya cerrado (D-2):** 14.
- **Muerto/fuera de build:** 19.
- 170 + 38 + 14 + 19 = **241**, cuadra.

Esto no cambia ninguna fila de la decisión de `blocked-categories-design.md` §0 ni el "119/147 sitios pasan por el contrato nuevo" original — actualiza el denominador y la distribución, exactamente el mismo tipo de actualización que ya hizo §4 con los 94 nuevos, ahora con los 28 sitios de §6.1/§6.2 y los 42 de `GlobalLockClip` ya sin quedar sin categoría.
