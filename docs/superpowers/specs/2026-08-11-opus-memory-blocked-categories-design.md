# Categorías bloqueadas de la migración de memoria (issue #3): propuesta de diseño

**Fecha:** 2026-08-11
**Estado:** **DECIDIDO** (ver §0 Decisión). Documento de propuesta conservado tal cual para trazabilidad; la decisión no reescribe las secciones de análisis de abajo.
**Documento base:** `docs/superpowers/specs/2026-08-10-opus-memory-migration-design.md` (§0 adenda, §10-§14). Este documento no repite el análisis de allí; lo referencia y lo corrige donde el árbol dice otra cosa.

Objetivo: dejar las categorías A, B (B1/B2/B3), D y `res.c` en estado de *decisión rápida*, con opciones concretas, contrapartidas reales y una estimación de impacto por combinación.

---

## 0. Decisión

**Fecha:** 2026-08-11
**Quién decide:** mantenedor (Pablo Honorato / jphonorato), en conversación con Claude Code.
**Decisión:** fila 3 de §7, **"Passthrough completo"** — combinación **A-2, B1-b, B2-b, B3-b, D-2, R-4**.

`OpusShellMemory` pasa a ser una **capa de convivencia** con Wine/Win32 real (passthrough hacia handles ajenos vía tabla de function pointers instalada por el shell), no un reemplazo que excluye por subsistema. 119/147 sitios pasan por el contrato nuevo. Quedan fuera, con destino ya cerrado en D-2:
- **D** (11 sitios: `hCode`, `hPlaybackHook`, `hfontPhy`) — sin equivalente en heap nativo, reasignados a `OpusShellFontMetrics`/`OpusShellSpine` cuando esos contratos existan.
- **17 sitios muertos/fuera de build** — no se migran, no cuentan como pendientes.

Las filas 1/2/4 de §7 y las opciones "-a"/"-c" por categoría (B1-a, B1-c, B2-a, B3-a, R-1, R-3) quedan descartadas, no borradas del documento — el análisis de contrapartidas que las respalda sigue siendo válido como registro de por qué se descartaron.

**Próximo paso:** diseño del mecanismo de passthrough en detalle, antes de tocar `OpusShellMemory.h`/`.cpp` — ver `docs/superpowers/specs/2026-08-11-opus-memory-passthrough-design.md`.

---

## 1. Censo verificado (2026-08-11, HEAD `2a36b1a`)

Recuento propio contra el árbol, no copiado del spec anterior. Regex `\bGlobal(Alloc|Free|Lock|ReAlloc|Size|Unlock)\b` sobre las 17 TU en alcance, más desglose por identificador del primer argumento de cada llamada.

Total: **147 sitios** (150 coincidencias brutas − 3 en `res.c` que son comentario/declaración `extern`, no llamadas). Coincide con la cifra del issue #3.

Desglose por familia de handle — esto es nuevo, el spec anterior solo llegaba a nivel de TU:

| TU | Sitios | Familias (sitios) | Categoría |
|---|---|---|---|
| `eldde.c` | 48 | `hevt` 12 + `*phevt` 6 + `*lphevtHead` 4 = 22; `*lphrgbKeyState` 3 | **A** (25) |
| | | `hKeys` 7 | **local, migrable** (7) |
| | | `hData` 4, `wLow` 3, `hExec` 2, `hCmds` 2 = 11 | **B1** (11) |
| | | `hCode` 4, `hPlaybackHook` 1 | **D** (5) |
| `quit.c` | 5 | `*lphrgbKeyState` 3, `*lphevtHead` 1 | **A** (4) |
| | | `hPlaybackHook` 1 | **D** (1) |
| `filecvt.c` | 22 | `ghsz` 6, `ghBuff` 3, `ghIniName` 3, `ghIniExt` 3, `ghsz{Version,Subset,Fn}` 3, + 4 `GlobalAlloc(gmemLibShare, …)` | **B2** (22) |
| `spelcore.c` | 9 | `hsz` 6, `pghd->ghsz` 3 | **B2** (9) |
| `etcmd.c` | 7 | `pghd->ghsz` 4, `vpetlib->ghd*.ghsz` 3 | **B2** (7) |
| `help.c` | 15 | `hHlp` 9, `hsz` 2 | **B3** (11) |
| | | `hText` 4 | **muerto** (`#ifdef TEXTEVENT`) |
| `raremsg.c` | 8 | `hdata` 5, `hdata2` 3 | **B3** (8) |
| `ddeclnt.c` | 5 | `hData` 3, `wLow` 2 | **B1** (5) |
| `idle.c` | 5 | `hfontPhy` | **D** (5) |
| `res.c` | 4 | `GlobalAlloc` 2 (en `GlobalAlloc2`), `GlobalReAlloc` 2 (en `OurGlobalReAlloc`) | **hub** (4) |
| `initwin.c` | 1 | `hInstance` | **muerto** (`OPUS_X64`) + D |
| `rcinit.c` | 2 | `h` | **muerto** (`OPUS_X64`) |
| `debug/debug1.c` | 3 | `h` | **muerto** (`RPDEBUG`) |
| `debug/debug2.c` | 4 | `hData` | **fuera de build** |
| `debug/debugwin.c` | 3 | — | **fuera de build** |
| `elsubs2.c` | 2 | `*lphevtHead` | **migrado** (`3c7c0db`) |
| `catalog.c` | 4 | `hDMFarMem` | **migrado** (`c4e9ff0`, `2a36b1a`) |

**Totales por categoría** (`initwin.c:781` cuenta una sola vez, en *muerto*):

| Categoría | Sitios |
|---|---|
| Migrado | 6 |
| A — familia compartida entre TU | 29 |
| B1 — DDE entre procesos | 16 |
| B2 — DLL externa vía `CallOtherStack` | 38 |
| B3 — WinHelp / portapapeles | 19 |
| D — segmento/selector Win16 | 11 |
| `res.c` — hub | 4 |
| Local migrable hoy (`hKeys`) | 7 |
| Muerto / fuera de build | 17 |
| **Total** | **147** |

### 1.1 Correcciones al spec anterior

Tres hallazgos que cambian el mapa, verificados con `grep`/`sed` sobre el árbol:

1. **`eldde.c` `hKeys` (7 sitios) no es categoría B.** `HANDLE hKeys = NULL;` local (`eldde.c:735`), alocado con `OurGlobalAlloc(GMEM_MOVEABLE, ...)`, ciclo completo dentro de `eldde.c` (única TU del árbol que menciona el identificador). El spec anterior los absorbía en "`eldde.c` (resto)" dentro de B. Son migrables en cuanto se resuelva el orden de `res.c`.
2. **`eldde.c` `hCode` (4 sitios) es un caso D nuevo, no catalogado antes.** `hCode = GetCodeHandle(PlaybackHook)` (`eldde.c:1259`) — handle de *segmento de código* propiedad del cargador de módulos, no de ningún asignador. `GlobalSize`/`GlobalLock`/`GlobalUnlock` sobre él no son operaciones de heap.
3. **`GlobalLockClip` es `GlobalLock` en este build.** `Opus/debugwin.h:209`: `#else /* !DEBUG */` → `#define GlobalLockClip GlobalLock`. `DEBUG` no se define en `src/CMakeLists.txt` para `opus_original_engine`. El argumento de §11 del spec anterior ("el `Lock` ya vive permanentemente fuera del contrato, solo el `Free` está en alcance") describe una **decisión de enrutado futuro, no una diferencia de mecanismo hoy**: hoy es la misma función, escrita con otro nombre. No invalida la conclusión de B, pero sí la premisa con la que se sostenía.

### 1.2 El discriminador que el contrato ignora hoy

Valores medidos en `Opus/lib/qwindows.h:1739-1744`:

```
GHND            = GMEM_MOVEABLE | GMEM_ZEROINIT
GMEM_SHARE      = 0x2000
GMEM_DDESHARE   = 0x2000     /* mismo bit */
GMEM_LOWER      = 0x1000
GMEM_NOT_BANKED = 0x1000     /* mismo bit */
```

Los sitios de asignación que **deben seguir siendo memoria real de Win16/Wine** llevan bit `0x2000` (compartible entre procesos) o `0x1000` (colocación en memoria baja / no bancada):

| Sitio | Flags | Bit |
|---|---|---|
| `eldde.c:177`, `eldde.c:265`, `ddeclnt.c:1552` | `GMEM_DDE` | `0x2000` |
| `filecvt.c:491,495,499,503,961,962` | `gmemLibShare` | `0x2000` |
| `help.c:265` (`hHlp`), `help.c:2110` (`hText`, muerto) | `GMEM_SHARE\|GMEM_NOT_BANKED` | `0x2000` + `0x1000` |
| `eldde.c:1263` (`hPlaybackHook`) | `GMEM_FIXED\|GMEM_LOWER` | `0x1000` |

**`OpusMemFlagsFromWin16()` (`OpusShellMemory.h:80-86`) descarta ambos bits en silencio** — solo traduce `GMEM_ZEROINIT`. Es correcto según su comentario ("se aceptan y se ignoran"), pero significa que hoy nada impide que un sitio B se migre por error y quede convertido en un `malloc` privado sin ningún diagnóstico. Es un riesgo latente independiente de qué se decida abajo, y la mitigación más barata (rechazo explícito o `Assert`) no depende de ninguna de las opciones que siguen.

**No discriminables por flag** (asignan con `GMEM_MOVEABLE` o `GHND` planos, su naturaleza de cruce está solo en el uso posterior): `spelcore.c` (9), `etcmd.c` (7), `raremsg.c` (8) — 24 sitios.

---

## 2. La opción transversal: *passthrough* con registro de handles vivos

Aparece como opción en A, B2, B3 y `res.c`, así que se describe una vez.

**Mecánica propuesta.** `OpusShellMemory.cpp` ya mantiene un registro `puntero → handle` (`PtrRegistry()`). Añadir un `std::unordered_set<OpusHandle>` de handles vivos permite responder *"¿este handle es mío?"* **sin desreferenciarlo** — que es exactamente lo que hoy hace imposible mezclar handles: un `HGLOBAL` de Wine reinterpretado como `OpusHandleImpl*` es memoria ajena leída como struct. Con la pertenencia resuelta por búsqueda en el conjunto:

```
OpusMemLock/Unlock/Realloc/Size/Free(h):
    si h ∈ registro_vivo  -> comportamiento actual
    si no                 -> delegar en ops.Lock/Unlock/... (tabla instalada por el shell)
```

**La dependencia de Win32 no entra en `src/core/`.** La delegación va por una tabla de punteros a función que el shell instala en el arranque:

```c
typedef struct OpusMemPassthroughOps {
    void         *(*Lock)(void *h);
    void          (*Unlock)(void *h);
    void          (*Free)(void *h);
    unsigned long (*Size)(void *h);
    void         *(*Alloc)(unsigned long cb, unsigned win16Flags);
} OpusMemPassthroughOps;

void OpusMemSetPassthrough(const OpusMemPassthroughOps *ops); /* NULL = desactivado */
```

`src/core/` sigue sin incluir un solo header de Win32; la implementación de `ops` vive en `src/port/`, que ya es la capa de compatibilidad. Se respeta la regla de frontera de `01-frontera-nucleo-shell.md`.

**Qué compra:** el orden de migración deja de importar. Categoría A (productor sin migrar, consumidor migrado) se disuelve; `res.c` deja de ser el último dominó; B2/B3 pueden migrarse sintácticamente sin cambiar el comportamiento en tiempo de ejecución.

**Qué cuesta, sin adornos:**
- El contrato pasa de *reemplazo* a *capa de convivencia*. La prueba de que el núcleo puede vivir sin `Global*` se aplaza hasta que se retire el passthrough.
- Sin `OpusMemSetPassthrough` instalado (build de núcleo puro, tests), un handle ajeno cae en la rama `NULL`: hay que decidir si eso es `abort()` ruidoso o retorno controlado. Recomendable ruidoso, coherente con `OpusMemFree` (`OpusShellMemory.cpp:146-154`).
- Un sitio migrado que en realidad debía quedar excluido **deja de fallar de forma visible** — funciona por delegación. Se pierde la señal. Mitigación: contar delegaciones y exigir que el conteo sea 0 en los sitios que se declararon "propios".
- Superficie pública del contrato: +1 tipo, +1 función. No es enorme, pero es lo contrario del criterio YAGNI con el que se rechazó exponer `lockCount` para el `Assert` de `catalog.c` (§7 del spec anterior). Si se adopta, conviene reabrir esa decisión con el mismo criterio.

---

## 3. Categoría A — familia de handle compartida entre TU (29 sitios)

Problema (resumen; detalle en spec anterior §10): `hevt`/`lphevtHead`/`phevt` y `lphrgbKeyState` son globales que `eldde.c` asigna y que `elsubs2.c` (ya migrado) y `quit.c` bloquean/liberan. Migrar una TU sin la otra mezcla representaciones.

**A-1 — Lote atómico `eldde.c`(subconjunto A) + `quit.c`.**
29 sitios en un commit. Contradice la granularidad de §6 del spec anterior (máx. 5 sitios por commit sin verificación individual), y obliga a una migración **parcial de TU**: `eldde.c` quedaría con 25 sitios migrados y 23 sin migrar (B1 11, D 5, `hKeys` 7) de forma permanente o semi-permanente. Ventaja: no cambia el contrato, se puede ejecutar mañana. Desventaja: `eldde.c` con dos representaciones de memoria conviviendo en el mismo archivo es exactamente el estado que hace difícil razonar sobre el siguiente bug.

**A-2 — Passthrough (§2).** La categoría desaparece: `elsubs2.c` ya migrado puede bloquear un handle que `eldde.c` asignó con `GlobalAlloc` real, porque el `Lock` delega. Permite migrar TU por TU en cualquier orden y respetar §6. Coste: el de §2.

**A-3 — Excluir la familia `hevt` del contrato de memoria y llevarla al contrato de eventos/macros.** `hevt` es el buffer de grabación de eventos de teclado/macro; conceptualmente es estado del *spine* (`OpusShellSpine.h`), no memoria genérica. Saca 29 sitios del alcance de #3 y los reasigna a un contrato que todavía no existe. Ventaja: es la clasificación conceptualmente correcta. Desventaja: `elsubs2.c` ya está migrado bajo el contrato de memoria — habría que revertir `3c7c0db` o aceptar una inconsistencia declarada.

**Recomendación (mía, no decisión): A-2 si se adopta el passthrough; A-1 en caso contrario**, documentando la excepción a §6 en el propio mensaje de commit. A-3 solo si la decisión de B empuja igualmente hacia contratos por subsistema.

---

## 4. Categoría B — memoria que cruza la frontera del proceso (73 sitios)

### B1 — DDE vía mensajes de Windows (16 sitios: `eldde.c` 11, `ddeclnt.c` 5)

**B1-a — Exclusión permanente**, con el mismo estatus que `GlobalLockClip`. −16 sitios del alcance de #3. Honesto y barato. El argumento es sólido: `GMEM_DDESHARE` es una petición al gestor de memoria de Win16 de un bloque que otro proceso podrá mapear; un `malloc` privado no puede cumplirla por construcción.

**B1-b — Passthrough (§2) + `OpusMemAlloc` sensible al bit `0x2000`.** Los sitios se migran sintácticamente; el bloque sigue siendo memoria real. Fidelidad intacta, 0 sitios excluidos. Requiere que `OpusMemAlloc` acepte los flags Win16 crudos o que `OpusMemFlagsFromWin16()` propague el bit en vez de descartarlo (hoy lo descarta, §1.2).

**B1-c — Contrato `OpusShellIpc` separado.** DDE no tiene futuro en la rama Qt: es IPC de Win16 que se reemplazará entero, no se portará. Migrar su memoria es trabajo que se tira. Mueve 16 sitios a una issue futura por subsistema, no por símbolo.

**Recomendación: B1-c, con B1-a como formulación mínima si no se quiere abrir otra issue todavía.** Ambas dan el mismo número; B1-c deja registro de que el trabajo existe y a quién le toca. B1-b es técnicamente correcta pero paga la complejidad del passthrough por 16 sitios que se van a borrar.

### B2 — DLL externa vía `CallOtherStack`/`WCallOtherStack` (38 sitios: `filecvt.c` 22, `spelcore.c` 9, `etcmd.c` 7)

Asimetría importante y verificada: `filecvt.c` marca sus 22 sitios con `gmemLibShare` (bit `0x2000`) — **discriminable por máquina**. `spelcore.c` y `etcmd.c` asignan con `GMEM_MOVEABLE` plano; su cruce solo es visible leyendo el uso (`MyGetAlternates`/`LMyLookUpWord`/`WMyETLookup`).

**B2-a — Exclusión permanente de las tres TU.** −38 sitios. Argumento adicional a favor: las DLL destino (conversores de formato, corrector, ET) son binarios Win16 que este port no puede cargar de todas formas; el subsistema entero está muerto en la práctica hasta que se reemplace. Riesgo: es el bloque más grande de exclusión, deja el 26% del issue fuera de un plumazo.

**B2-b — Passthrough (§2).** Los 38 sitios se migran; el handle real llega a la DLL. Correcto si algún día la DLL se carga. Coste: la complejidad de §2 pagada por un subsistema que hoy no se ejecuta.

**B2-c — Partir por discriminabilidad: excluir `filecvt.c` (22, marcado por flag, exclusión verificable en CI con un `grep`) y migrar `spelcore.c` + `etcmd.c` (16) con passthrough.** Peor de los dos mundos en mi lectura: paga la complejidad del passthrough *y* mantiene una exclusión, para ganar 16 sitios de un subsistema inerte.

**Recomendación: B2-a.** Es el bloque donde la exclusión tiene el argumento más fuerte (el consumidor no existe en este port) y donde el passthrough compra menos.

### B3 — WinHelp y portapapeles del sistema (19 sitios: `help.c` 11, `raremsg.c` 8)

Son dos problemas distintos metidos en la misma etiqueta:

- **`raremsg.c` (8): transferencia de propiedad al portapapeles.** `SetClipboardData` cede el bloque al sistema (no se libera), `GetClipboardData` devuelve un bloque ajeno. Es literalmente el mismo fenómeno por el que `GlobalLockClip` ya está excluido y asignado al contrato de portapapeles Qt-6. **Debería ir con esa decisión ya tomada, no tratarse como pregunta abierta.**
- **`help.c` (11): `hHlp` (9) enviado a `WINHELP.EXE`, `hsz` (2) en funciones exportadas para que WinHelp las llame.** WinHelp es otro proceso que este port no arranca. Mismo carácter que B1-c: subsistema a reemplazar, no a portar.

**B3-a — Reasignar `raremsg.c` al contrato de portapapeles Qt-6 (decisión ya existente) y `help.c` a una issue de subsistema de ayuda.** −19 del alcance de #3, cero decisiones nuevas de contrato.
**B3-b — Passthrough** para los 19. Mismo coste/beneficio que B1-b.

**Recomendación: B3-a.** Es la opción que menos inventa: usa dos decisiones que el proyecto ya tomó o va a tener que tomar igual.

---

## 5. Categoría D — segmento/selector Win16 (11 sitios)

`eldde.c` `hCode` 4 + `hPlaybackHook` 1, `quit.c` `hPlaybackHook` 1, `idle.c` `hfontPhy` 5. (`initwin.c:781` es D y además muerto; contado en *muerto*.)

Verificado directamente: `idle.c:777-792` entrelaza `GlobalFlags`/`GMEM_LOCKCOUNT`/`GlobalWire` con `GlobalReAlloc`/`GlobalSize`/`GlobalUnlock` sobre `hfontPhy` **en el mismo condicional** — los sitios en alcance no son separables de los ya excluidos. `hCode` viene de `GetCodeHandle()`, no de un asignador. `hPlaybackHook` pasa por `AllocSelector`/`PrestoChangoSelector`/`FreeSelector`.

**D-1 — Issue separada, fuera del alcance de `OpusShellMemory`** (sugerencia previa). Deja 11 sitios en un limbo con etiqueta.

**D-2 — Exclusión definitiva, sin issue, reasignando cada bloque a la corriente de trabajo que sí lo va a tocar:** `idle.c`/`hfontPhy` → `OpusShellFontMetrics` (es gestión de descartabilidad de fuente física de GDI); `eldde.c`/`quit.c` `hPlaybackHook` + `hCode` → `OpusShellSpine` (hook de journal-playback del bucle de mensajes). Es más fuerte que D-1 porque aquí **no hay nada que portar**: no existe la operación equivalente en un heap nativo, el código tiene que reescribirse o desaparecer cuando esos contratos lleguen.

**Recomendación: D-2.** "Issue separada" sugiere trabajo pendiente sobre estos sitios; la verdad es que ninguno sobrevive al rediseño de su subsistema. Añadir `GetCodeHandle`, `AllocSelector`, `PrestoChangoSelector`, `FreeSelector` y `GMEM_LOWER`/`GMEM_NOT_BANKED` a la lista de exclusión permanente de `00-inventario-win32.md`, junto a `GlobalWire`/`GlobalFlags`/`GlobalCompact`.

---

## 6. `res.c` — el hub (4 sitios, 12 TU consumidoras)

Verificado: 12 TU llaman `OurGlobalAlloc`/`OurGlobalReAlloc` (`ddeclnt.c`, `debug/debug1.c`, `debug/debug2.c`, `eldde.c`, `etcmd.c`, `filecvt.c`, `help.c`, `raremsg.c`, `rcinit.c`, `res.c`, `rtfout2.c`, `spelcore.c`). Los 4 sitios reales son `GlobalAlloc` ×2 dentro de `GlobalAlloc2` (`res.c:1867,1870`) y `GlobalReAlloc` ×2 dentro de `OurGlobalReAlloc` (`res.c:1900,1903`).

Dos comportamientos de `res.c` que hay que decidir explícitamente, no por omisión:
- **El reintento con `ShrinkSwapArea()`/`GrowSwapArea()`** (`res.c:1867-1873`) no significa nada sobre `malloc`: si `malloc` falla, encoger un área de intercambio de Win16 no lo arregla. Migrar deja esa rama viva pero inerte. Misma clase de pérdida documentada que el `Assert` de `catalog.c` (§7 del spec anterior).
- **El rechazo de `dwBytes > 0x00010000L`** (`OurGlobalAlloc`/`OurGlobalReAlloc`) es artificial en un heap de 64 bits, pero **debe conservarse literalmente**: quitarlo cambia qué asignaciones fallan y, por esa vía, qué documentos se paginan igual. Restricción de fidelidad, no de estilo.

**R-1 — Migrar al final,** después de las 12 consumidoras. Coherente con §6. Deja `res.c` como último paso obligatorio y mantiene la asimetría productor/consumidor durante toda la migración (deuda que §13 del spec anterior ya señaló).

**R-2 — Lote grande: `res.c` + las 12 TU consumidoras a la vez.** El lote menos aislado posible de los 147 sitios. Contradice §6 frontalmente.

**R-3 — Exclusión permanente si la mayoría de sus consumidores queda excluida.** Bajo la recomendación de §4 (B1-c + B2-a + B3-a), de las 12 consumidoras quedan excluidas o muertas 9 (`ddeclnt`, `debug1`, `debug2`, `etcmd`, `filecvt`, `help`, `raremsg`, `rcinit`, `spelcore`); viva y con sitios en alcance queda una sola, `eldde.c` (parcial: `hevt` + `hKeys`), más el propio `res.c`; `rtfout2.c` consume el asignador pero no tiene ninguno de los 147 sitios. Es una opción **defendible bajo esa combinación**, no en general.

**R-4 — Migrar `res.c` primero, sensible a flags.** `GlobalAlloc2`/`OurGlobalReAlloc` delegan en `OpusMemAlloc`/`OpusMemRealloc` **solo** cuando `win16Flags & (GMEM_DDESHARE|GMEM_LOWER) == 0`; en otro caso llaman la API real sin cambio. Combinado con el passthrough de §2 (que resuelve `Lock`/`Unlock`/`Free` por pertenencia al registro, sin mirar flags), esto **invierte el problema**: `res.c` deja de ser el último dominó y pasa a ser el primero, y cada TU consumidora queda migrable en cualquier orden e independientemente. Coste: es el escenario que más depende del passthrough; si el passthrough se rechaza, R-4 no es viable.

**Recomendación: R-4 si se adopta el passthrough; R-3 si se adoptan las exclusiones de §4 sin passthrough.** R-1 solo tiene sentido en un escenario donde casi nada se excluye y casi nada cambia en el contrato — es decir, el escenario más largo y con más deuda intermedia.

---

## 7. Impacto por combinación de decisiones

"Resuelto" = migrado al contrato **o** excluido con justificación cerrada y reasignado a otro contrato. Un sitio en limbo no cuenta como resuelto.

| # | Combinación | Migrados | Excluidos con destino | En limbo | Migrado / 147 | Resuelto / 147 |
|---|---|---|---|---|---|---|
| 0 | **Statu quo** (nada se decide) | 6 | 0 | 124 | **4%** | **4%** |
| 1 | **Conservadora**: A-1, B1-a, B2-a, B3-a, D-2, R-1 | 46 | 84 | 0 | **31%** | **100%** |
| 2 | **Por subsistema** (mi recomendación sin passthrough): A-1, B1-c, B2-a, B3-a, D-2, R-3 | 42 | 88 | 0 | **29%** | **100%** |
| 3 | **Passthrough completo**: §2 + A-2, B1-b, B2-b, B3-b, D-2, R-4 | 119 | 11 | 0 | **81%** | **100%** |
| 4 | **Intermedia**: §2 + A-2, B1-c, B2-b, B3-a, D-2, R-4 | 84 | 46 | 0 | **57%** | **100%** |

Detalle de los conteos (categorías de §1): migrado 6 · A 29 · B1 16 · B2 38 · B3 19 · D 11 · `res.c` 4 · `hKeys` 7 · muerto 17.

- Fila 1: 6 + A 29 + `hKeys` 7 + `res.c` 4 = 46 migrados; excluidos B 73 + D 11 = 84.
- Fila 2: igual que la 1 pero `res.c` excluido con R-3 → 42 migrados, 88 excluidos.
- Fila 3: todo menos D y muerto → 6+29+16+38+19+4+7 = 119.
- Fila 4: 6 + 29 + 38 + 7 + 4 = 84; excluidos B1 16 + B3 19 + D 11 = 46.

Los 17 sitios muertos/fuera de build quedan resueltos en todas las filas por la vía ya documentada (§12 del spec anterior): no se migran y no se cuentan como pendientes.

**Lectura de la tabla:** las cuatro combinaciones cierran el issue #3. La diferencia no es "cuánto queda por hacer" sino **cuánto código pasa por el contrato nuevo** — 29% frente a 81%. La pregunta real que Pablo tiene que responder no es "¿qué categorías desbloqueamos?" sino:

> *¿El contrato `OpusShellMemory` es un reemplazo (y entonces lo que no encaja se excluye por subsistema, filas 1-2), o una capa de convivencia durante la transición (y entonces el passthrough se paga una vez y casi todo entra, filas 3-4)?*

Mi recomendación, marcada como tal: **fila 2** si la prioridad es cerrar #3 rápido y sin ampliar el contrato; **fila 4** si la prioridad es que el núcleo Qt controle de verdad la memoria del motor antes de invertir el bucle de mensajes. Fila 3 paga el passthrough por subsistemas (DDE, WinHelp) que se van a reemplazar enteros; fila 1 difiere de la 2 en 4 sitios y en dejar `res.c` como cuello de botella de orden durante toda la migración.

---

## 8. Independiente de lo que se decida

Un solo punto, del §1.2: `OpusMemFlagsFromWin16()` descarta `GMEM_DDESHARE`/`GMEM_LOWER` en silencio. Bajo cualquiera de las cuatro filas, un sitio B o D migrado por error se convierte en un `malloc` privado sin diagnóstico. Rechazo explícito (o `Assert`) en `OpusMemAlloc` cuando llegan esos bits es barato, no compromete ninguna de las opciones de arriba, y convierte la exclusión de B/D en algo verificable en tiempo de ejecución en vez de una convención en un documento.

Como no altera el contrato en su forma pública ni toca `src/Opus/`, se deja anotado aquí y no se aplica: la decisión sobre `OpusShellMemory.h`/`.cpp` es del mantenedor.
