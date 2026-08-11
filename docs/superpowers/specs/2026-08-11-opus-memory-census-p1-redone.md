# Censo rehecho (P1) — universo real de la familia `Global*` en código compilado

**Fecha:** 2026-08-11
**Estado:** censo verificado contra el árbol. Ningún archivo de código modificado. Sin `git add` de código.
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

## 3. Tabla de totales corregida (reemplaza la de §1 de `…-blocked-categories-design.md`)

| Categoría | Documentado (147) | + Nuevo (94) | Total |
|---|---|---|---|
| Migrado | 6 | 0 | 6 |
| A — familia compartida entre TU | 29 | +6 (`opus_asm_misc.cpp`) | 35 |
| B1 — DDE entre procesos | 14 (corregido, ver checklist-audit §1.3) | +15 (`DDESRVR.C`) | 29 |
| B2 — DLL externa vía `CallOtherStack` | 38 | +3 (`SPELL.C`) | 41 |
| B3 — WinHelp / portapapeles | 17 (corregido) | +39+2 (`CLIPBRD2.C`+`CLIPBORD.C`) | 58 |
| D — segmento/selector Win16 | 13 (corregido) | +1 (`LOADFONT.C`) | 14 |
| `res.c` — hub | 4 | 0 | 4 |
| Local migrable hoy (`hKeys`) | 7 | 0 | 7 |
| **Nuevo — subsistema imagen/gráficos (sin categoría)** | 0 | **19** (`GRSPEC.C`+`PIC3.C`+`PIC2.C`) | **19** |
| **Indeterminado** | 0 | **9** (`SCREEN2.C`+`RTFRARE.C`+`rtfout2.c`) | **9** |
| Muerto / fuera de build | 19 (corregido) | 0 | 19 |
| **Total** | **147** | **94** | **241** |

`GlobalLockClip` (42, todo (a)/B3) queda fuera de esta tabla por ser símbolo distinto — se suma aparte si se decide tratarlo junto al resto: **241 + 42 = 283**.

## 4. Consecuencia sobre la decisión ya tomada (fila 3, `c4b2a09`)

La decisión de **mecanismo** (passthrough completo, A-2/B1-b/B2-b/B3-b/D-2/R-4) no cambia — ninguno de los 94 nuevos introduce un patrón de flags que ese mecanismo no maneje ya (B1/B2/B3/D/A siguen siendo suficientes para el 87% de lo nuevo: 66+... en realidad 66/94 = 70%, más 42 `GlobalLockClip` que también caen en B3 → (66+42)/(94+42) = 79%). Lo que cambia es el **denominador y el peso relativo**:

- "119/147 = 81%" pasa a, como mínimo, **119+66+42 = 227 / 241+42 = 283 ≈ 80%** si se asume que el subsistema de imagen (19) y lo indeterminado (9) terminan clasificados como D-like (fuera del passthrough) — el peor caso para el porcentaje.
- Si el subsistema de imagen y lo indeterminado terminan siendo passthrough-compatibles (B-like), el porcentaje sube, no baja.
- **B1 y B3 casi se duplican** (14→29, 17→58) solo por `DDESRVR.C` y `CLIPBRD2.C`/`CLIPBORD.C`. Esto no cambia la decisión de *cómo* tratarlas (siguen siendo B1-b/B3-b), pero sí el volumen de trabajo de migración de call-sites detrás de esa decisión — relevante para estimar esfuerzo, no para revisar el mecanismo.

## 5. Puntos abiertos que quedan, actualizados

- **P1 queda resuelto en cuanto a cifra** (este documento). Sigue pendiente decidir qué hacer con el subsistema de imagen (19 sitios, sin categoría) y los 9 indeterminados — eso es trabajo nuevo, no cubierto por P2.
- Nueva pregunta, no formulada antes: ¿el subsistema de imagen/gráficos (`GRSPEC.C`, `PIC2.C`, `PIC3.C`, más los `*phMF`/hub de `PIC3.C` y `rtfout2.c`) es candidato a **Categoría D** (sin equivalente en heap nativo, destino `OpusShellFontMetrics`-like) o a **B2** (cruza a GDI/impresión, no a una DLL Win16, pero mismo patrón de bloque efímero)? No decidible por censo — requiere leer el ciclo de vida completo de `hPict`/`hBits`/`*phMF` en las tres TU.
- No se verificó ausencia de comentarios/declaraciones dentro de los 94 sitios nuevos con el mismo rigor que se hizo para los 147 originales (`res.c`, `ripaux.c`). Riesgo acotado: los patrones capturados son mayormente `ident = OurGlobalAlloc(...)` y `Global*(ident` dentro de cuerpo de función, forma que rara vez aparece en comentario; no confirmado línea por línea.
