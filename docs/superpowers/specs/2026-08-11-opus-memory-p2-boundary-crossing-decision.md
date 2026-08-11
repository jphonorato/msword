# P2 — sitios que el enrutado por flag clasifica mal (frontera de proceso/DLL/portapapeles)

**Fecha:** 2026-08-11
**Estado:** decisión de diseño. Añade `OPUS_MEM_ESCAPES` a `src/core/include/OpusShellMemory.h` (código, ya aplicado). No toca `src/Opus/` — los call sites listados abajo se marcan como "requieren este bit al migrarse", no se migran aquí.
**Responde a:** P2 de `2026-08-11-opus-memory-passthrough-checklist-audit.md` §7.

---

## 1. El problema

`OpusMemFlagsFromWin16()` enruta por los bits Win16 reales del sitio de llamada (§4 de `…-passthrough-design.md`). Eso es correcto para A/B1/B3 porque en esos casos el flag Win16 (`GMEM_DDESHARE`, `GMEM_LOWER`) sí codifica "este bloque sale del proceso o va a memoria baja". No es correcto para los sitios de abajo: alocan con flags planos (`GMEM_MOVEABLE`/`GHND` = `0x0042`, sin `DDESHARE` ni `LOWER`) pero el handle **sale del contrato de todos modos**, entregado a una DLL Win16 externa o cedido al portapapeles del sistema. En Win16 no había bit para esto porque cualquier `HANDLE` servía indistintamente para ambos usos; la distinción era conocimiento del programador sobre qué hacía cada función, no un flag. Un enrutado que mira solo el flag no puede reconstruir ese conocimiento.

Migrar estos sitios con el enrutado tal como está especificado entregaría un puntero de `malloc` a una DLL Win16 (`CallOtherStack`) o al portapapeles del sistema (`SetClipboardData`) — exactamente el modo de fallo que el passthrough existe para evitar.

## 2. Sitios

### 2.1 `spelcore.c` — 9 sitios, familia `ghsz`/`pghd->ghsz`

Alocación real: `spelcore.c:681` (`ghsz = OurGlobalAlloc(GMEM_MOVEABLE, …)`, dentro de `pghd->ghsz`). Consumido por `CallOtherStack`/`WCallOtherStack`/`LCallOtherStack` hacia el DLL de diccionario en `spelcore.c:1306,1315,1325,1335,1345,1357` (`&ghszWordReplace`, `&ghszAlts`, `&ghszDictFileName`, `&ghszWord`, `&ghszReplaceWord` — variables locales de tipo `HANDLE` pasadas por dirección al DLL, mismo mecanismo de frontera que `pghd->ghsz`). Resto del ciclo de vida (`:666,699,702`) es local a `spelcore.c`/`SPELL.C`.

### 2.2 `etcmd.c` — 7 sitios, familia `pghd->ghsz` / `vpetlib->ghd*.ghsz`

Alocaciones reales: `etcmd.c:1125` y `:1252` (`ghsz = OurGlobalAlloc(GMEM_MOVEABLE, …)`). Consumido por `WCallOtherStack` hacia DLL externa en `etcmd.c:1186,1202` (`&ghszPath`, `&ghrgpos`). Resto del ciclo de vida (`:417,423,429,1147,1173,1237,1273`) es local a `etcmd.c`/`SPELL.C`.

### 2.3 `raremsg.c` — familia `hdata`, cedida al portapapeles del sistema

Alocación: `raremsg.c:1337` (`hdata = OurGlobalAlloc(GHND, …)`), bloqueado vía `GlobalLockClip` (`:1353`), desbloqueado (`:1363`) y **entregado al portapapeles del sistema** con `SetClipboardData(CF_TEXT, hdata)` (`:1364`). A partir de ahí el handle es propiedad del sistema, no del proceso — cualquier `OpusMemFree`/`OpusMemLock` posterior sobre él (si el código lo reintentara, cosa que no hace hoy) tendría que tratarlo como ajeno.

**Excluido de esta lista, verificado que NO necesita el bit nuevo:** `raremsg.c` `hdata2` (`HFedtStripText`, `:1414-1513`). Procedencia decidida en tiempo de ejecución — devuelve *o* su propio bloque (`OurGlobalAlloc(GHND)`, `:1478`) *o* el `hdata` recibido de `GetClipboardData(CF_TEXT)` (`:1423`), y `return(hdata)` en `:1513` no vuelve a llamar `SetClipboardData`. El mecanismo `IsOwn()` por registro ya resuelve esto correctamente sin ayuda: el handle propio nace registrado, el handle del portapapeles nunca lo estuvo — ningún flag adicional hace falta. Es, como ya señaló la auditoría del checklist, un argumento a favor del mecanismo elegido.

**Total de sitios que requieren el bit nuevo al migrarse: 3 alocaciones reales** (`spelcore.c:681`, `etcmd.c:1125,1252`) que cubren los 16 sitios de ciclo de vida de `spelcore.c`/`etcmd.c`, más 1 alocación (`raremsg.c:1337`) del ciclo de portapapeles — el resto de los 18 sitios censados en estas familias son `Lock`/`Unlock`/`Free` sobre el mismo handle, no alocaciones nuevas, y siguen automáticamente la clasificación decidida en la alocación porque `IsOwn()` es por handle, no por sitio de llamada.

## 3. Decisión

**Opción elegida: marcar estos 4 sitios de alocación con `OPUS_MEM_ESCAPES` explícitamente al migrarlos**, en vez de (a) enrutar por subsistema completo — más invasivo, tocaría la firma de despacho — o (c) aceptar el riesgo apoyándose en que las DLL Win16 no cargan bajo Wine (afirmación no verificable desde el árbol, checklist-audit §6).

```c
/* spelcore.c:681, tras migrar -- antes: OurGlobalAlloc(GMEM_MOVEABLE, cb) */
pghd->ghsz = (HANDLE)OpusMemAlloc(cb, OPUS_MEM_ESCAPES);

/* etcmd.c:1125,1252, mismo patrón */

/* raremsg.c:1337 -- antes: OurGlobalAlloc(GHND, cch+1) */
hdata = (HANDLE)OpusMemAlloc(cch + 1, OpusMemFlagsFromWin16(GHND) | OPUS_MEM_ESCAPES);
```

`OPUS_MEM_ESCAPES` no traduce ningún bit Win16 — es información que el migrador aporta porque conoce el destino del handle (DLL externa vía `CallOtherStack`, o `SetClipboardData`), no algo que `OpusMemFlagsFromWin16()` pueda derivar del flag original. Entra en `OPUS_MEM_FOREIGN` (`OpusShellMemory.h`, ya aplicado), así que el mecanismo de enrutado no cambia — un bit más en la misma máscara.

**Consecuencia sobre el censo de `2026-08-11-opus-memory-census-p1-redone.md`:** estos 16+2 sitios ya estaban contados en B2/B3 con los totales corregidos (41 y 58 respectivamente); no cambia el total, cambia qué flag debe llevar el `OpusMemAlloc` de reemplazo cuando esos call sites se migren — información que antes no estaba escrita en ningún documento y ahora sí.

## 4. Qué queda pendiente

No se migran `spelcore.c`/`etcmd.c`/`raremsg.c` aquí — son `src/Opus/`, árbol restringido, requiere autorización explícita por archivo antes de tocarlos (CLAUDE.md). Este documento dejalisto el flag exacto a usar cuando esa autorización llegue, para que la migración sea mecánica y no vuelva a requerir este análisis.
