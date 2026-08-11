# P8 — `struct GHD.ghsz` de 32 bits en `splshare.h` frente a `HANDLE` de 64 bits en `etcmd.c`

**Fecha:** 2026-08-11
**Estado:** decisión de diseño. No toca `src/Opus/` — el fix de código queda diferido al trabajo
autorizado de migración P2 (`spelcore.c`/`etcmd.c`), no se aplica aquí.
**Responde a:** P8 de `2026-08-11-opus-memory-passthrough-checklist-audit.md` §7.

---

## 1. Las dos definiciones, verificadas

Hay dos definiciones de `struct GHD` en el árbol, sin relación de inclusión entre ellas — cada
translation unit ve una u otra según qué cabecera incluya:

**`etcmd.c:160-164`** (definición local a este `.c`, no exportada en ningún `.h`):

```c
struct GHD {
	HANDLE ghsz;
	int ichMac;
	int ichMax;
};
```

`HANDLE` en este build es de 64 bits (`opus_x64_runtime_test`, sizeof(HANDLE)==8, ya medido en trabajo
previo — cabecera Win32 de Wine bajo `OPUS_X64`). Sin guarda `OPUS_X64`/`__GNUC__`: el campo ya nace del
ancho correcto porque usa el tipo `HANDLE`, no un ancho fijo.

**`src/Opus/splshare.h:64-68`** (definición pública, incluida por `spelcore.c:59` y `SPELL.C:59`):

```c
struct GHD {
	unsigned ghsz;
	int ichMac;
	int ichMax;
	};
```

`unsigned` es de 32 bits en este build (GCC/LP64 vía winegcc, igual que en MSVC/Win32). Sin guarda de
ningún tipo — el mismo texto sirve para el build MSVC de 32 bits original y para el build Winelib actual,
y en el segundo trunca.

Estas son dos definiciones *distintas* de un tag con el mismo nombre en TUs distintas — válido en C
(cada TU resuelve `struct GHD` contra la declaración visible en su propio árbol de includes), pero
significa que el mismo concepto ("un handle envuelto en un descriptor con longitud") tiene dos anchos de
almacenamiento incompatibles según qué archivo lo usa. `etcmd.c` nunca incluye `splshare.h` (confirmado:
sus `#include` van de `word.h` a `sdm.h`, sin `splshare.h`), así que dentro de `etcmd.c` todo es
consistente en 64 bits. El problema está enteramente del lado de `spelcore.c`/`SPELL.C`.

## 2. Sitios consumidores verificados

`spelcore.c:59` y `SPELL.C:59` incluyen `splshare.h` — ambos ven la versión de 32 bits.

- **`spelcore.c:681`** — `FCopySzToGhd()`, primera alocación:
  ```c
  if ((pghd->ghsz = OurGlobalAlloc( GMEM_MOVEABLE, (DWORD)cch )) == NULL)
  ```
- **`spelcore.c:687`** — mismo función, rama de realocación:
  ```c
  if (OurGlobalReAlloc(pghd->ghsz, (DWORD)cch, GMEM_MOVEABLE) == NULL)
  ```
- **`spelcore.c:656,666`** — `FCopyGhdToSz()`, `GlobalLockClip(pghd->ghsz)` / `GlobalUnlock(pghd->ghsz)`.
- **`spelcore.c:699,702`** — segundo `GlobalLock`/`GlobalUnlock` dentro de `FCopySzToGhd()`.
- **`SPELL.C:1300`** — `FreeGhd()`:
  ```c
  GlobalFree( pghd->ghsz );
  ```

`OurGlobalAlloc` está declarado `HANDLE OurGlobalAlloc(WORD, DWORD);` en `wordwin.h:836` — devuelve un
`HANDLE` de 64 bits que el compilador trunca implícitamente al asignarlo a `pghd->ghsz` (`unsigned`, 32
bits) en `spelcore.c:681` y `:687`.

Los `pghd` que llegan a estas funciones son `&vpspl->ghdWord`, `&vpspl->ghdReplaceWord`, `&vpspl->ghdT`
(`SPELL.H:19-21`, dentro de `struct SPL`), y `pghd` (parámetro genérico) — todos declarados con
`struct GHD` a través de `SPELL.H`, que no incluye `splshare.h` directamente pero comparte la misma
definición porque cualquier TU que vea ambas cabeceras resuelve el tag a la última declaración visible;
en la práctica todos los consumidores de esta familia (`spelcore.c`, `SPELL.C`) ven la versión de 32
bits por la vía de `splshare.h:59`.

## 3. Por qué hoy es inerte y con la migración P2 deja de serlo

`OurGlobalAlloc` en el árbol actual —previo a cualquier migración a `OpusShellMemory`— es un wrapper que
en la práctica nunca ha visto una ejecución real bajo Wine en esta sesión: nada en `src/Opus/` corre
todavía como binario enlazado contra el contrato de memoria Winelib real donde `GlobalAlloc` devuelva un
`HGLOBAL` de Wine (`docs/superpowers/specs/2026-08-11-opus-memory-p2-boundary-crossing-decision.md` §1,
mismo argumento). El valor que hoy cae en `pghd->ghsz` puede truncarse en los bits altos, pero esos bits
altos hoy son cero o irrelevantes porque el camino no ha sido ejercitado con handles reales de 64 bits
con payload significativo por encima de 32 bits — la aserción "trunca en la práctica sin romper nada
todavía" depende de que nada real llegue aún por ese camino, no de que el código esté bien.

La migración P2 (`…-p2-boundary-crossing-decision.md` §3) especifica exactamente esto:

```c
/* spelcore.c:681, tras migrar -- antes: OurGlobalAlloc(GMEM_MOVEABLE, cb) */
pghd->ghsz = (HANDLE)OpusMemAlloc(cb, OPUS_MEM_ESCAPES);
```

`OPUS_MEM_ESCAPES` fuerza el camino *foreign* del passthrough (`OpusShellMemory.cpp`) precisamente porque
`spelcore.c:681` entrega el handle a `CallOtherStack`/`WCallOtherStack` hacia el DLL de diccionario
(`spelcore.c:1306,1315,1325,1335,1345,1357`) — es decir, cruza al asignador nativo real de Wine
(`GlobalAlloc` de verdad, no un stub). Ese es el punto exacto en que `pghd->ghsz` empieza a contener un
`HGLOBAL` de Wine genuino de 64 bits, y en que truncarlo a `unsigned` deja de ser un no-op silencioso.

## 4. Escenario de fallo concreto

Con la migración P2 aplicada pero sin corregir `splshare.h`:

1. `spelcore.c:681` (post-migración) ejecuta `pghd->ghsz = (HANDLE)OpusMemAlloc(cb, OPUS_MEM_ESCAPES)`.
   `OpusMemAlloc` en camino foreign delega a `gOps->Alloc`, que bajo Winelib real es `GlobalAlloc` de
   Wine — un `HGLOBAL` que en Wine/x86-64 es un puntero de proceso real, típicamente con bits
   significativos por encima del bit 31 (heap de 64 bits, no garantiza direcciones bajas).
2. La asignación a `pghd->ghsz` — campo `unsigned` de 32 bits porque `spelcore.c` ve la definición de
   `splshare.h:65`, no la de `etcmd.c:161` — descarta silenciosamente los 32 bits altos del handle. No
   hay warning de truncamiento visible en el flujo de build actual (conversión implícita `HANDLE`→
   `unsigned` en asignación simple, no en llamada de función con prototipo verificado en el punto de
   corte).
3. Cualquier lectura posterior de `pghd->ghsz` — `spelcore.c:656` (`GlobalLockClip`), `spelcore.c:687`
   (`OurGlobalReAlloc`), o **`SPELL.C:1300`** (`GlobalFree( pghd->ghsz )`, en `FreeGhd()`, llamado desde
   `SPELL.C:1283-1285` sobre `vpspl->ghdWord/ghdReplaceWord/ghdT` al cerrar el corrector) — pasa el
   handle truncado a una API de Wine que espera el `HGLOBAL` completo de 64 bits.
4. Resultado: `GlobalLockClip`/`GlobalFree` reciben un valor que ya no coincide con ningún handle vivo
   del heap de Wine — no es "el mismo handle con menos precisión", es un valor distinto que apunta a otra
   entrada de la tabla de handles o a ninguna. El resultado observable depende de qué haya en esa
   posición truncada: desde un `GlobalFree` que falla silenciosamente sobre un handle ajeno o inexistente
   (best case, fuga de memoria) hasta un `GlobalLockClip` que devuelve un puntero a memoria no relacionada
   con el bloque real (worst case, corrupción o lectura fuera de bounds al copiar `ichMac` bytes desde
   `lpch` en `FCopyGhdToSz`, `spelcore.c:664-665`).

No hace falta un input adversarial: basta con que el corrector ortográfico se invoque una vez con el
diccionario tras la migración P2 — cualquier `FCopySzToGhd`/`FCopyGhdToSz` en el ciclo normal de "enviar
palabra al DLL, leer alternativas de vuelta" ejercita la asignación en `:681` y la lectura en `:656` o
`SPELL.C:1300`.

## 5. Precedente en el árbol para el fix

El patrón para anchos de campo dependientes de plataforma en este codebase ya existe y es exactamente el
que aplica aquí — `wordwin.h:452-474`, `union` dentro de `struct _kme` (Key Map Entry):

```c
union {
#if defined(__GNUC__) && !defined(_MSC_VER)
	/* First member was int, which matched pointer width on Win32.  [...]
	   long is pointer-width here [...] (same layout rule
	   as the Win32 "int == pointer width" assumption).  MSVC keeps int. */
	long w;		/* generic */
#else
	int w;		/* generic */
#endif
	BCM bcm;	/* ktMacro */
	...
```

Mismo problema de forma: un campo cuyo ancho coincidía con el de otro tipo relevante bajo Win32/MSVC
(`int` == ancho de puntero) deja de coincidir bajo GCC/LP64 y necesita ensancharse solo en esa rama,
preservando el ancho original para el build MSVC retenido (`CLAUDE.md`: "Do not let a Linux change break
the MSVC build path").

## 6. Recomendación

Aplicar el mismo patrón a `splshare.h:64-68`:

```c
struct GHD {
#if defined(__GNUC__) && !defined(_MSC_VER)
	HANDLE ghsz;	/* HANDLE is 64-bit under OPUS_X64/Wine; unsigned truncates it.
			   Matches etcmd.c's local struct GHD (etcmd.c:161), which never
			   had this bug because it uses HANDLE directly. */
#else
	unsigned ghsz;
#endif
	int ichMac;
	int ichMax;
	};
```

Alternativa descartada: cambiar `unsigned` a `HANDLE` incondicionalmente. Se descarta porque
`splshare.h` no tiene guarda hoy y sirve tanto al build MSVC retenido (`x64-debug`/`x64-release`,
`CLAUDE.md`) como al Winelib — bajo MSVC de 32 bits, `HANDLE` también es de 32 bits, así que el cambio
sería un no-op ahí, pero cambiar el tipo del campo sin guarda cuando ya existe precedente de guardarlo
(`wordwin.h`) sería inconsistente con la convención ya establecida en el árbol para este tipo exacto de
problema, y perdería la documentación explícita de *por qué* el ancho cambia que la guarda aporta.

**Momento de aplicar el fix: junto con la migración P2 de `spelcore.c`, no antes ni mucho después.**
Antes de P2, el campo `ghsz` de `splshare.h` nunca contiene un `HGLOBAL` real de Wine (§3) — el fix
sería correcto pero no verificable (no hay forma de probar que corrige algo si nada ejercita el ancho
truncado). El diseño en
`…-p2-boundary-crossing-decision.md` §3 ya deja escrito el flag exacto (`OPUS_MEM_ESCAPES`) que
`spelcore.c:681`/`etcmd.c:1125,1252` deben llevar al migrarse — este documento añade el segundo
prerrequisito de esa misma migración: sin el ensanche de `splshare.h:65`, la migración introduciría
activamente el bug que hoy es inerte. Aplicar el fix de ancho en un commit separado *mucho después* de
P2 deja una ventana en la que el binario Winelib real ejercita la ruta con datos ya truncados de forma
silenciosa; aplicarlo *antes* de P2 no tiene forma de verificarse porque nada real pasa por ahí todavía.

## 7. ¿Issue aparte o dentro del issue de migración P2?

**Recomendación: bundled dentro del issue de migración P2, no un issue aparte.** Razones:

- El fix de `splshare.h:65` no tiene sentido de scope independiente — no es "una corrección de bug
  aislada", es un prerrequisito estructural de que P2 sea correcta post-migración (§3, §6). Un issue
  aparte invita a que se cierre o priorice independientemente de P2, con el riesgo concreto descrito en
  §6 (aplicarlo antes, sin poder verificarlo; o aplicarlo después, dejando la ventana activa).
- Ambos tocan exactamente los mismos archivos restringidos (`src/Opus/spelcore.c`, `src/Opus/etcmd.c`,
  y ahora también `src/Opus/splshare.h`) bajo la misma autorización — no hay ahorro de coordinación en
  separarlos, solo el riesgo de que se autoricen y ejecuten en momentos distintos.
- El diff es pequeño (una guarda de 6 líneas en una cabecera) comparado con el de la migración de los 16
  sitios de ciclo de vida (`…-p2-boundary-crossing-decision.md` §2.1-2.2) — no justifica el overhead de
  gestión de un segundo issue para algo que se revisa en el mismo PR de todos modos.

Este documento dejalisto el patrón exacto (§6) para que quien ejecute la migración P2 autorizada lo
aplique como parte del mismo cambio, sin volver a analizar el porqué.
