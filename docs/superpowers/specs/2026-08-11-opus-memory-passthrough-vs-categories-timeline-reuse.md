# Cronología passthrough vs. reclasificación por categorías, y reutilización frente al universo de 237

**Fecha:** 2026-08-11
**Estado:** hallazgo de auditoría. Ningún archivo del árbol modificado salvo este. Sin `git add`, sin commit.
**Alcance:** dos preguntas independientes, ninguna repite el censo ni la auditoría del checklist §8 ya entregada (`2026-08-11-opus-memory-passthrough-checklist-audit.md`), que se toma como insumo.

---

## 1. Pregunta 1 — Cronología

### 1.1 Comandos usados

```
git log --follow --format='%H %ai %s' -- docs/superpowers/specs/2026-08-10-opus-memory-migration-design.md
git log --follow --format='%H %ai %s' -- docs/superpowers/specs/2026-08-11-opus-memory-blocked-categories-design.md
git log --follow --format='%H %ai %s' -- docs/superpowers/specs/2026-08-11-opus-memory-passthrough-design.md
git show --stat a4de106 a1b5695 aa9ef20 c4b2a09
```

`2026-08-11-opus-memory-passthrough-checklist-audit.md` no tiene historial — es archivo nuevo, no commiteado (`git status --short` → `??`).

### 1.2 Línea de tiempo verificada (timestamps normalizados a UTC)

| Hora UTC | Commit | Archivo | Qué hace |
|---|---|---|---|
| 2026-08-11 01:54:13 | `bdc51c5` | `2026-08-10-opus-memory-migration-design.md` | crea el censo original de 147 sitios |
| 03:18:09 | `3c7c0db` | `elsubs2.c` | migra 2 sitios (`elsubs2.c`) a `OpusMem*` |
| 03:33:35 | `a116848` | `2026-08-10-...-migration-design.md` | §B3, análisis de familias de handle cruzando TU |
| 04:27:46 | `96e6a11` | `2026-08-10-...-migration-design.md` | §B3, revisión de alcance |
| **08:54:40** | **`a4de106`** | `2026-08-11-opus-memory-blocked-categories-design.md` | **crea** el documento de categorías A/B1/B2/B3/D, estado "propuesta para decidir" |
| **09:53:17** | **`a1b5695`** | `2026-08-11-opus-memory-passthrough-design.md` | cierra §9 ítems 1-2 del checklist passthrough |
| **09:58:31** | **`aa9ef20`** | `2026-08-11-opus-memory-passthrough-design.md` | corrige §3.3, cierra §9 ítems 3-4 |
| **10:00:47** | **`c4b2a09`** | `2026-08-11-opus-memory-blocked-categories-design.md` | marca la fila 3 (Passthrough completo) como **DECIDIDA** |

Ventana crítica: **08:54:40 → 10:00:47 UTC, 66 minutos**, los cuatro commits centrales en ese orden exacto.

### 1.3 Conclusión: entrelazado, en ambos sentidos, no secuencial limpio

No es "§9 antes de la reclasificación" ni "después". Es **entrelazado con dependencia cruzada real**, verificable por contenido, no solo por orden de commit:

- `a4de106` (08:54) **ya existía** cuando se escribió `a1b5695` (09:53) — con casi una hora de margen, no es coincidencia de reloj. El propio mensaje de `a1b5695` usa el vocabulario de familias de handle que `a4de106` introdujo (`hevt/lphevtHead/lphrgbKeyState`, "familia de Categoría A") y **lo contradice explícitamente**: "Contradice §3.3, que afirma que A converge al camino propio tras la migración del productor". Es decir, §9 leyó la propuesta de categorías y encontró un bug en el propio diseño de passthrough gracias a ella.
- `aa9ef20` (09:58) corrige justo esa contradicción (§3.3) citando la misma familia.
- `c4b2a09` (10:00), **posterior** a los dos cierres de §9, es el commit que finalmente marca la fila 3 como DECIDIDA — y su mensaje referencia explícitamente `2026-08-11-opus-memory-passthrough-design.md` como "próximo paso", es decir, la decisión final de categorías se cerró usando el diseño de passthrough como fundamento, no al revés.

Conclusión explícita: **§9 no ignoró la reclasificación por no existir aún** (existía, en estado propuesta, con margen de casi una hora) **ni la reclasificación se cerró sin pasar por §9** (se cerró después, citándolo). Ambos documentos se escribieron en la misma sesión de trabajo, retroalimentándose: la propuesta de categorías alimentó una corrección a §9, y §9 fue la base sobre la que se cerró la decisión de categorías. El censo original de 147 sitios (`bdc51c5`, 01:54) es el único genuinamente anterior a todo lo demás, por ~7 horas — ese sí es una dependencia limpia hacia adelante.

---

## 2. Pregunta 2 — Reutilización de A/B1/B2/B3/D frente al universo de 237

### 2.1 Universo de partida (de la auditoría §8, no recalculado aquí)

- 84 sitios en 10 TU `.C` mayúscula no contadas por el censo original.
- 4 sitios en `rtfout2.c` (compilado vía `#include` desde `RTFOUT.C`).
- 6 sitios en `opus_asm_misc.cpp`, ya identificados en la auditoría como Categoría A (mismos `extern lphevtHead`/`lphrgbKeyState` que `eldde.c:40-41`).
- **Subtotal: 94 sitios nuevos** (no ~90; la cifra de la tarea era aproximada, el número exacto es 94: 84+4+6).
- Aparte, 42 sitios de `GlobalLockClip` (25 en TU minúscula ya censadas, 17 en TU mayúscula nuevas), símbolo que ningún regex de ningún censo captura — universo ortogonal, no incluido en el recuento de 237 ni en los 94.

### 2.2 Método

Para cada TU nueva, `grep -noE` del identificador que recibe el resultado de `Global(Alloc|ReAlloc)` o que se pasa a `Global(Free|Lock|Unlock|Size)`, comparado contra las familias con nombre de `2026-08-11-opus-memory-blocked-categories-design.md` §1 (`hevt`, `lphevtHead`, `lphrgbKeyState`, `hData`, `hKeys`, `wLow`, `hExec`, `hCmds`, `hCode`, `hPlaybackHook`, `ghsz`, `ghBuff`, `ghIniName`, `ghIniExt`, `hsz`, `hHlp`, `hText`, `hdata`, `hdata2`, `hfontPhy`, `hDMFarMem`). No se leyó el cuerpo completo de ninguna TU salvo donde se indica.

```
grep -noE "\b[A-Za-z_][A-Za-z0-9_>.-]*\s*=\s*(Our)?Global(Alloc|ReAlloc)\(|\bGlobal(Free|Lock|Unlock|Size|ReAlloc)\(\s*[A-Za-z_][A-Za-z0-9_>.-]*" src/Opus/<TU>
```

### 2.3 Tabla de reutilización — 94 sitios nuevos

| TU | Sitios | Identificador | Caso | Motivo |
|---|---|---|---|---|
| `CLIPBRD2.C` | 39 | `hData`, `hDataDescriptor`, `hReturn`, `h` | **(a)** | patrón de transferencia de datos de portapapeles (`GlobalLock`/`GlobalFree` sobre bloques de intercambio); mismo subsistema que `raremsg.c`, ya resuelto por decisión B3-a ("debería ir con esa decisión ya tomada") |
| `DDESRVR.C` | 15 | `hData` | **(a)** | servidor DDE, `OurGlobalAlloc`; mismo identificador y subsistema que `eldde.c`/`ddeclnt.c` categoría B1 |
| `SPELL.C` | 3 | `ghsz`, `pghd->ghsz` | **(a)** | acceso a campo **idéntico** (`pghd->ghsz`) al de `spelcore.c`, ya categorizado B2 |
| `CLIPBORD.C` | 2 | `hps`, `hrc` (vía `GlobalLockClip`) | **(a)** | usa `GlobalLockClip` directamente — confirmado por lectura de contexto (`CLIPBORD.C:608,650`) — mismo mecanismo que la decisión B3-a de portapapeles |
| `LOADFONT.C` | 1 | `hfontPhy` | **(a)** | identificador **idéntico** al de `idle.c`, ya categorizado D (→ `OpusShellFontMetrics`, decisión D-2) |
| `opus_asm_misc.cpp` | 6 | `lphevtHead`, `lphrgbKeyState` | **(a)** | ya establecido en la auditoría §8, Categoría A |
| **Subtotal (a)** | **66** | | | 70% de los 94 |
| `GRSPEC.C` | 10 | `hData`, `hPict`, `h1`, `h2` | **(b)** | `hPict` (handle de imagen/gráfico) no aparece en ninguna familia del documento de categorías; subsistema de gráficos, no de A/B/D |
| `PIC3.C` | 5 | `hBits` | **(b)** | ídem, subsistema de imagen, sin familia previa |
| `PIC2.C` | 4 | `hData` | **(b)** | mismo subsistema que `PIC3.C`/`GRSPEC.C` (imagen); el identificador `hData` coincide de nombre con DDE pero el dominio es gráfico, no de mensajería — no se trata como (a) por esa sola coincidencia de nombre |
| **Subtotal (b)** | **19** | | | 20% de los 94 |
| `SCREEN2.C` | 3 | `h` | **(c)** | patrón autocontenido de carga de recursos (`HLoadRes1`), sin identificador de familia reconocible; no se leyó la TU completa |
| `RTFRARE.C` | 2 | `hData` | **(c)** | un solo símbolo (`GlobalSize`), contexto insuficiente para decidir familia |
| `rtfout2.c` | 4 | `h` | **(c)** | consume el hub `res.c` (documentado en §6 del doc de categorías: "consume el asignador pero no tiene ninguno de los 147 sitios"), pero no tiene familia asignada en ningún documento |
| **Subtotal (c)** | **9** | | | 10% de los 94 |
| **Total** | **94** | | | |

Los 42 `GlobalLockClip` (universo ortogonal, fuera de los 94 y fuera de los 237): **todos caso (a)** — `debugwin.h:209` los define como `GlobalLock` literal, y `2026-08-11-...-blocked-categories-design.md` §1.1(3) y la sección B3 ya tratan ese mecanismo como resuelto por la decisión de portapapeles existente. Confirmado además leyendo `CLIPBORD.C:608,650`, que los usa directamente.

### 2.4 Confirmación específica sobre `CLIPBRD2.C` y `DDESRVR.C`

La auditoría del checklist §8 señala que estas dos TU "alteran el reparto por categoría, no solo el total". **Confirmado, con matiz:**

- Sí alteran el reparto: 39 + 15 = 54 de los 66 sitios caso (a) — el 82% de la reutilización — caen en B3 (portapapeles) y B1 (DDE), justo las dos categorías que la decisión ya tomada (fila 3, `B1-b`/`B3-b`, passthrough) cubre. No requieren categoría nueva, pero si el universo real es ~2× el documentado, el peso relativo de B1/B3 dentro del total resuelto sube de forma no trivial: solo estas dos TU casi duplican el tamaño combinado de B1 (16) y B3 (19) documentado en §1 del doc de categorías.
- No alteran el *mecanismo* de decisión: ambas se resuelven por la misma regla ya escrita (B1-b/B3-b passthrough), no exigen una categoría nueva. La afirmación de la auditoría de que "alteran el reparto" es correcta sobre el peso numérico, no sobre la necesidad de rediseño.

### 2.5 Estimación cuantitativa final

Usando (a)=reutilizable sin categoría nueva (extiende A/B1/B2/B3/D o su decisión ya tomada), (b)=requiere categoría nueva, (c)=no determinable en esta sesión:

- **Dentro de los 94 sitios nuevos:** 66 (a) / 19 (b) / 9 (c) → **70% reutilizable, 20% trabajo nuevo, 10% indeterminado**.
- **Contra el universo completo de 237** (147 ya categorizados + 94 nuevos por regex; los 42 `GlobalLockClip` quedan fuera de este denominador porque tampoco entran en el propio 237):
  - Reutilizable sin trabajo nuevo de categorización: 147 (ya decidido) + 66 (a) = **213/237 ≈ 90%**.
  - Requiere categorización nueva o lectura profunda: 19 (b) + 9 (c) = **28/237 ≈ 12%**.
  - (Los dos porcentajes no suman exactamente 100% por redondeo: 89.9% + 11.8%.)
- Si se añaden los 42 `GlobalLockClip` como universo aparte (279 = 237 + 42 sitios totales conocidos), los 42 son 100% caso (a) — no mueven el porcentaje de "requiere trabajo nuevo", solo bajan ligeramente el peso relativo de (b)+(c): 28/279 ≈ 10%.

**Lectura:** la proporción de sitios nuevos que caen fuera de la categorización existente (~30% de los 94, o ~12% del universo de 237) se concentra casi enteramente en un solo subsistema no documentado hasta ahora — manejo de imágenes/gráficos (`GRSPEC.C`, `PIC2.C`, `PIC3.C`, 19 sitios) — más tres TU sueltas de contexto insuficiente (9 sitios). No es una dispersión amplia; es un bloque identificable.

---

## 3. No determinable en esta sesión

- **`SCREEN2.C`, `RTFRARE.C`, `rtfout2.c` (9 sitios, caso c):** requieren lectura completa de la TU para decidir si forman familia propia o se adjuntan a una existente. Fuera del propósito de estimación de esta tarea, según su propia restricción.
- **Si `GRSPEC.C`/`PIC2.C`/`PIC3.C` forman una única familia o dos:** `hPict` (`GRSPEC.C`) y `hBits` (`PIC3.C`) podrían ser el mismo objeto en distintas fases de su ciclo de vida, o dos objetos distintos. No se leyó suficiente código para decidirlo; se cuentan juntos como (b) por prudencia, no por confirmación.
- **Si `CLIPBRD2.C`/`CLIPBORD.C` comparten exactamente la misma familia de handle entre sí** (más allá de compartir subsistema "portapapeles"): no verificado a nivel de identificador compartido, solo a nivel de mecanismo (`GlobalLockClip` en un caso, patrón de transferencia en el otro).
- **Impacto de estos hallazgos sobre la fila 3 decidida (`c4b2a09`) o sobre P1–P9 de la auditoría anterior:** no evaluado aquí, fuera de alcance — esta tarea no emite recomendación sobre si rehacer el censo.

---

**Detenido. Sin proponer cambios de código ni de documentos existentes.**
