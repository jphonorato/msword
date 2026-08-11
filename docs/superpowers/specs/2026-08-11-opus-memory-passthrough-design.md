# Passthrough de `OpusShellMemory` hacia Win32/Wine: diseño

**Fecha:** 2026-08-11
**Estado:** propuesta para revisión — **ninguna línea de `OpusShellMemory.h`/`.cpp` tocada aquí**. Solo diseño.
**Decisión que implementa:** `docs/superpowers/specs/2026-08-11-opus-memory-blocked-categories-design.md` §0 — fila 3 ("Passthrough completo"), combinación A-2, B1-b, B2-b, B3-b, D-2, R-4.
**Contrato base:** `src/core/include/OpusShellMemory.h`, `src/core/src/OpusShellMemory.cpp`, diseño de frontera en `docs/port-qt/01-frontera-nucleo-shell.md` §B3.

Objetivo de este documento: especificar el mecanismo con precisión suficiente para que la implementación sea mecánica, y dejar explícitos los riesgos nuevos que introduce — no motivar la decisión (eso ya está en el documento de propuesta) ni implementarlo.

---

## 1. El problema exacto

Hoy `OpusHandle` es `struct OpusHandleImpl *` — un puntero opaco que **solo tiene sentido si fue devuelto por `OpusMemAlloc`/`OpusMemRealloc`**. Cualquier función del contrato que reciba un `OpusHandle` lo desreferencia sin comprobar procedencia:

```c
extern "C" void *OpusMemLock(OpusHandle h) {
    if (h == nullptr || h->freed) {   /* <- desreferencia aquí */
```

Bajo passthrough, un sitio migrado en categoría A/B1/B2/B3 puede recibir un handle que **no** vino de `OpusMemAlloc` — vino de un `GlobalAlloc` real (propio, de otro proceso vía DDE, o del cargador de una DLL externa) y llega disfrazado de `OpusHandle` porque el sitio de llamada hace el mismo cast que ya hacía con `HANDLE`. Desreferenciar `h->freed` sobre esa memoria es lectura de struct ajena — comportamiento indefinido, no un fallo controlado.

**Esto no es hipotético: ya ocurre hoy en uno de los 6 sitios migrados.** `Opus/elsubs2.c:330,337` (migrado en `3c7c0db`) llama `OpusMemLock((OpusHandle)*lphevtHead)` / `OpusMemUnlock(...)`, pero `*lphevtHead` es una familia de categoría A cuyo **productor** (`eldde.c`) sigue sin migrar — sigue asignando con `GlobalAlloc` real. Verificado: `catalog.c` (el otro migrado) siempre pasa por `OpusMemAlloc` primero, así que sus handles son propios de punta a punta; `elsubs2.c` es el caso donde el productor no coincide con el consumidor migrado. Este es exactamente el defecto de diseño que el passthrough tiene que cerrar, no solo el habilitador de A/B2/B3 nuevos.

---

## 2. Mecanismo de distinción propio / ajeno

### 2.1 Opciones consideradas

| Opción | Mecánica | Por qué se descarta o se acepta |
|---|---|---|
| **Bit de tag en el puntero** | Robar un bit bajo de `OpusHandleImpl*` (asumiendo alineación ≥2) para marcar "propio". | Descartada: el handle ajeno es un `HGLOBAL` real de Wine, cuya alineación no es una garantía de este contrato ni está documentada por Wine para esa API. Envenenar el puntero antes de pasarlo a `ops.Lock`/`ops.Free` reales exige des-envenenarlo primero — una fuente extra de bugs por un ahorro de una comparación de hash. |
| **Rango de direcciones reservado** | Reservar un arena/mmap propio para `OpusHandleImpl`, comprobar pertenencia por rango. | Descartada: obliga a sustituir `new OpusHandleImpl()` por un allocator de arena de tamaño fijo o creciente, cambia la gestión de memoria del contrato entero por un beneficio (una comparación de rango en vez de un lookup de hash) que no es medible en este proyecto — Amdahl no aplica, esto no es un hot path. |
| **Tabla de lookup con fallback (elegida)** | `std::unordered_set<OpusHandle>` de handles vivos propios. Membership test antes de desreferenciar. Si no está: ajeno, delega en `ops`. | Elegida. No requiere invariantes sobre el layout de Wine, no cambia cómo se asignan los `OpusHandleImpl`, y la comparación de membership **nunca desreferencia `h`** — es segura incluso si `h` apunta a memoria arbitraria ajena, porque comparar valores de puntero (no leerlos) no es UB. |

### 2.2 Mecánica exacta

Nuevo estado privado en `OpusShellMemory.cpp`, junto al `PtrRegistry()` existente:

```c
namespace {

/* Handles vivos asignados por este contrato (OpusMemAlloc con ruta
   privada). Membership -- no dereference -- es lo que permite distinguir
   un OpusHandle propio de un HGLOBAL ajeno sin leer memoria que no nos
   pertenece. Se actualiza en el mismo punto que PtrRegistry(): alta en
   alloc, baja en free. Una realloc que cambia de handle (no ocurre hoy en
   el camino propio -- ver OpusMemRealloc -- pero sí puede ocurrir en el
   camino ajeno, ver §2.4) no toca este conjunto. */
std::unordered_set<OpusHandle> &OwnHandles() {
    static std::unordered_set<OpusHandle> live;
    return live;
}

bool IsOwn(OpusHandle h) {
    return h != nullptr && OwnHandles().count(h) != 0;
}

}  // namespace
```

Cada función pública se reescribe con la misma forma:

```c
extern "C" void *OpusMemLock(OpusHandle h) {
    if (IsOwn(h)) {
        OpusHandleImpl *impl = h;              /* desreferencia segura: confirmado propio */
        if (impl->freed) return nullptr;
        ++impl->lockCount;
        return impl->ptr;
    }
    if (h != nullptr && gOps.Lock != nullptr) {
        return gOps.Lock(reinterpret_cast<void *>(h));
    }
    return nullptr;   /* h no es propio y no hay passthrough instalado: fallo controlado, ver §5.2 */
}
```

Mismo patrón en `OpusMemUnlock`, `OpusMemFree`, `OpusMemSize`, `OpusMemRealloc`. `OpusMemAlloc` no lo necesita como entrada (no recibe handle), pero sí como salida: decide con los flags si el bloque nace propio o ajeno (§4) y, si nace ajeno, **no** lo inserta en `OwnHandles()` — queda automáticamente clasificado como ajeno en la próxima llamada sin tabla adicional.

### 2.3 Riesgo de colisión de valores de puntero (ABA)

`OwnHandles()` compara **valores** de puntero. Si Wine reutiliza una dirección que antes perteneció a un `OpusHandleImpl*` ya liberado y eliminado del set, y ese `OpusHandleImpl` original también fue reciclado por `new` para una asignación nueva con la misma dirección, no hay colisión real — el set siempre refleja el estado vivo actual, no historial. El riesgo real es el opuesto: que un `HGLOBAL` real y un `OpusHandleImpl*` vivo **simultáneamente** compartan valor numérico. Bajo Winelib ambos son direcciones del mismo espacio de proceso (malloc nativo de un lado, el heap de Wine del otro) — no hay partición de address space que lo garantice. Probabilidad práctica: extremadamente baja (dos allocators independientes rara vez convergen en el mismo puntero vivo a la vez), pero no es cero por construcción. Se documenta como riesgo aceptado en §5.1, no se mitiga con más mecanismo — mitigarlo bien exigiría exactamente la opción de rango reservado que se descartó en §2.1.

### 2.4 `OpusMemHandle` (equivalente `GlobalHandle`) sobre punteros ajenos

`OpusMemHandle(ptr)` hoy busca `ptr` en `PtrRegistry()`, que solo se llena en el camino propio (`RegisterPtr` se llama desde `OpusMemAlloc`/`OpusMemRealloc` propios). Un puntero devuelto por `ops.Lock` sobre un handle ajeno **no** está en `PtrRegistry()` — `OpusMemHandle` devolvería `NULL` para él, tal como ya hace hoy para cualquier puntero que no reconoce.

Esto es correcto para B1/B2/B3 (DDE, DLL externa, WinHelp no llaman `GlobalHandle` sobre estos bloques en el árbol — no verificado exhaustivamente, ver checklist §7) pero **queda como pregunta abierta, no resuelta en este diseño**: si algún sitio de las categorías A/B1/B2/B3 sí necesita `OpusMemHandle` sobre un puntero ajeno, la opción es registrar también los punteros ajenos en `PtrRegistry()` en el momento del primer `ops.Lock` — no se especifica aquí porque ningún sitio censado hoy lo requiere; añadirlo especulativamente sería lo contrario de YAGNI.

---

## 3. Interfaz de passthrough

### 3.1 Tabla de function pointers

`src/core/` no incluye Win32 — la tabla la rellena `src/port/`, que ya es la capa de compatibilidad, y la instala en el arranque del shell. Firma completa:

```c
/* OpusShellMemory.h -- se añade a la API pública existente */

typedef struct OpusMemPassthroughOps {
    /* win16Flags: los bits GMEM_* crudos relevantes para enrutado, no la
       forma reducida de OpusMemFlagsFromWin16 -- ver §4. Devuelve un
       HGLOBAL real (u otro handle ajeno) reinterpretado como OpusHandle;
       el núcleo nunca lo desreferencia como OpusHandleImpl*. */
    OpusHandle    (*Alloc)(unsigned long cb, unsigned win16Flags);
    void         *(*Lock)(OpusHandle h);
    void          (*Unlock)(OpusHandle h);
    OpusHandle    (*Realloc)(OpusHandle h, unsigned long cb, unsigned win16Flags);
    unsigned long (*Size)(OpusHandle h);
    void          (*Free)(OpusHandle h);
} OpusMemPassthroughOps;

/* ops == NULL desinstala el passthrough (estado por defecto: sin
   passthrough, todo handle no-propio falla controladamente -- ver §5.2).
   No hay ownership de *ops: el llamador (shell) mantiene el struct vivo
   mientras esté instalado; un puntero a static/global basta y es el
   patrón esperado. No es thread-safe -- mismo supuesto de hilo único que
   el resto del contrato. */
void OpusMemSetPassthrough(const OpusMemPassthroughOps *ops);
```

`OpusHandle` sigue siendo `struct OpusHandleImpl *` de nombre, pero un valor devuelto por `ops.Alloc` **nunca apunta a un `OpusHandleImpl` real** — es un `HGLOBAL` reinterpretado. Esto es tipográficamente incómodo (un mismo tipo con dos representaciones incompatibles) pero es el mismo patrón que ya usa el resto del contrato para "handle opaco de ancho completo" — el comentario en el header debe decir explícitamente: *"un OpusHandle nunca se desreferencia fuera de OpusShellMemory.cpp, y dentro de .cpp nunca sin pasar `IsOwn()` primero."*

### 3.2 Punto de instalación

El shell (capa `src/port/`, no `src/core/`) implementa `OpusMemPassthroughOps` sobre las API Win32 reales de Wine — `GlobalAlloc`/`GlobalLock`/`GlobalUnlock`/`GlobalReAlloc`/`GlobalSize`/`GlobalFree` — y llama `OpusMemSetPassthrough(&ops)` una vez, en la inicialización del shell, antes de que cualquier TU de `Opus/` pueda invocar `OpusMem*`. Ubicación exacta (no decidida aquí, es implementación): candidato natural es el mismo punto donde hoy se inicializa cualquier otro estado de `src/port/` compartido entre TUs — a determinar contra el orden de arranque real cuando se implemente.

### 3.3 Enrutado en cada función del contrato (A-2, B1-b, B2-b, B3-b)

No hay diferencia de mecanismo entre las cuatro categorías — todas comparten la misma tabla de ops y el mismo `IsOwn()`. Lo que las distingue es **qué flags de asignación** hacen que `OpusMemAlloc` produzca un handle ajeno en primer lugar (§4), no un mecanismo de enrutado distinto por categoría. Concretamente:

- **A** (`hevt`/`lphevtHead`/`lphrgbKeyState`, `eldde.c` productor sin migrar): el productor sigue llamando `GlobalAlloc` real directamente (no pasa por `OpusMemAlloc` — no está migrado). El consumidor migrado (`elsubs2.c`) recibe ese handle, lo pasa a `OpusMemLock`, `IsOwn()` da falso, delega en `ops.Lock`. Cuando `eldde.c` se migre más adelante (fuera de alcance de este documento), pasará a asignar vía `OpusMemAlloc` con flags planos (sin `GMEM_DDESHARE`/`GMEM_LOWER`) y el handle se volverá propio de punta a punta — la mecánica de passthrough no cambia, solo el flujo converge.
- **B1/B2/B3** (DDE, DLL externa, WinHelp/portapapeles): el sitio migrado llama `OpusMemAlloc` con los flags Win16 originales intactos (`GMEM_DDESHARE`, `gmemLibShare`, `GMEM_SHARE|GMEM_NOT_BANKED`). `OpusMemAlloc` ve el bit de enrutado (§4), llama `ops.Alloc` en vez de `malloc`, y el handle resultante nunca entra en `OwnHandles()`. Todas las llamadas posteriores (`Lock`/`Unlock`/`Free`/etc.) sobre ese handle van a `ops` automáticamente porque `IsOwn()` da falso.

---

## 4. Flags: de "se ignoran" a "deciden enrutado"

`OpusMemFlagsFromWin16()` hoy descarta `GMEM_SHARE`/`GMEM_DDESHARE` (`0x2000`) y `GMEM_LOWER`/`GMEM_NOT_BANKED` (`0x1000`) — el hallazgo de §1.2/§8 del documento de propuesta. Bajo passthrough dejan de descartarse: son la señal de enrutado.

```c
/* OpusShellMemory.h */
#define OPUS_MEM_ZEROINIT 0x0001u
#define OPUS_MEM_DDESHARE 0x0002u  /* GMEM_SHARE | GMEM_DDESHARE, Win16 0x2000 */
#define OPUS_MEM_LOWER    0x0004u  /* GMEM_LOWER | GMEM_NOT_BANKED, Win16 0x1000 */
#define OPUS_MEM_FOREIGN  (OPUS_MEM_DDESHARE | OPUS_MEM_LOWER)

static inline unsigned OpusMemFlagsFromWin16(unsigned win16Flags)
{
    unsigned flags = 0;
    if (win16Flags & 0x0040u) flags |= OPUS_MEM_ZEROINIT;
    if (win16Flags & 0x2000u) flags |= OPUS_MEM_DDESHARE;
    if (win16Flags & 0x1000u) flags |= OPUS_MEM_LOWER;
    return flags;
}
```

`OpusMemAlloc`/`OpusMemRealloc`:

```c
extern "C" OpusHandle OpusMemAlloc(unsigned long cb, unsigned flags) {
    if (flags & OPUS_MEM_FOREIGN) {
        if (gOps.Alloc == nullptr) {
            return nullptr;   /* fallo controlado -- ver §5.2, NO cae a malloc privado */
        }
        return gOps.Alloc(cb, flags & OPUS_MEM_FOREIGN /* reduce a los bits que ops entiende */);
    }
    /* camino propio existente, sin cambios */
    ...
    OwnHandles().insert(h);
    return h;
}
```

`gOps.Alloc` en `src/port/` traduce `OPUS_MEM_DDESHARE`/`OPUS_MEM_LOWER` de vuelta a `GMEM_DDESHARE`/`GMEM_LOWER` reales para la llamada a `GlobalAlloc` de Wine — la reconstrucción exacta de flags (p. ej. si además hace falta `GMEM_MOVEABLE`) es detalle de esa implementación, no de este contrato.

**Extensión aditiva, no rotura de ABI de la firma:** `OpusMemFlagsFromWin16` sigue devolviendo `unsigned`, dos bits nuevos en un espacio que solo usaba uno. Ningún call site existente que no pase `GMEM_DDESHARE`/`GMEM_SHARE`/`GMEM_LOWER`/`GMEM_NOT_BANKED` ve `flags` cambiar de valor.

---

## 5. Impacto en los 6 sitios ya migrados

Verificado contra el árbol (`git grep OpusMem`):

- **`catalog.c`** (`HGrabFarMem`, `FAllocDMFarMem`, `FreeDMFarMem`): siempre asigna con `OpusMemAlloc(cb, OpusMemFlagsFromWin16(GHND))`. `GHND = GMEM_MOVEABLE|GMEM_ZEROINIT = 0x0042` — ningún bit `0x2000`/`0x1000`. `flags & OPUS_MEM_FOREIGN == 0` siempre. **Sin cambio de comportamiento**: sigue el camino propio, `OwnHandles()` lo contiene, `IsOwn()` da verdadero, cero llamadas a `ops`.
- **`elsubs2.c`** (`OpusMemLock`/`OpusMemUnlock` sobre `*lphevtHead`): productor (`eldde.c`) no migrado, sigue asignando con `GlobalAlloc` real. **Esto es exactamente el bug de §1** — hoy, sin passthrough, es UB latente (desreferencia de memoria ajena). Con passthrough: `IsOwn()` da falso, delega en `ops.Lock`/`ops.Unlock`, que son literalmente `GlobalLock`/`GlobalUnlock` reales — el mismo resultado que tendría el código pre-migración, pero pasando por el contrato en vez de por UB. **Es una corrección, no una regresión ni un no-op.**

No hay tercer caso entre los 6: `catalog.c` cubre 4 sitios migrados, `elsubs2.c` cubre 2 (`3c7c0db`).

---

## 6. Riesgos nuevos introducidos por el mecanismo

1. **Colisión de valores de puntero entre `OwnHandles()` y handles ajenos** (§2.3). Aceptado, no mitigado — mitigarlo requeriría el rango de direcciones reservado descartado en §2.1. Si se observa en la práctica, es la señal de que hay que revisitar esa decisión, no un bug de esta implementación.
2. **`ops` no instalado y se pide un bloque foreign** (builds de solo-núcleo, tests de `src/core` sin shell): `OpusMemAlloc` con `OPUS_MEM_FOREIGN` y `gOps.Alloc == nullptr` **debe fallar** (`return nullptr`), no degradar silenciosamente a `malloc` privado. Degradar sería peor que el estado actual: un bloque que el resto del sistema espera compartible (DDE) o en memoria baja se volvería privado sin ningún diagnóstico — exactamente el modo de fallo silencioso que §8 del documento de propuesta señala como riesgo latente hoy. Este diseño lo convierte en un fallo ruidoso y verificable (`nullptr` que el llamador ya debe comprobar), no lo elimina como clase de bug pero lo hace observable.
3. **`OpusMemHandle` sobre punteros ajenos devuelve `NULL`** (§2.4) — comportamiento definido pero potencialmente sorprendente si algún sitio no censado lo necesita. Queda como pregunta abierta para el checklist §8, no resuelto aquí.
4. **Sin red de doble-liberación para handles ajenos.** El camino propio aborta con `fprintf`+`abort()` en doble free (`OpusShellMemory.cpp:146-154`); el camino `ops` reenvía directo a `GlobalFree` real y depende de lo que Wine haga con una doble liberación — **sin cambio respecto al comportamiento pre-migración**, porque estos sitios llamaban `GlobalFree` directo antes también. No es una regresión, pero si se lee la migración como "ahora todo pasa por un contrato más seguro", hay que ser explícito en que esa mejora **no** alcanza a los sitios foreign — su perfil de riesgo es idéntico al de hoy, ni mejor ni peor.
5. **Reconstrucción de flags en `gOps.Alloc`** (§4): la traducción `OPUS_MEM_DDESHARE`/`OPUS_MEM_LOWER` → `GMEM_DDESHARE`/`GMEM_LOWER` reales vive en `src/port/`, fuera de este contrato. Si esa traducción se equivoca (p. ej. olvida `GMEM_MOVEABLE`), el bug no es visible en `src/core/` — otra razón para que el checklist de implementación incluya una prueba de round-trip cruzada (winegcc/wineg++, como ya se hace para el resto de `OpusShellMemory`).
6. **Threading:** sin cambio — sigue siendo un contrato de un solo hilo lógico, igual que `GlobalAlloc` en Win16. El passthrough no introduce ni resuelve nada aquí.

---

## 7. Qué queda explícitamente fuera de este documento

- Ninguna línea de `OpusShellMemory.h`/`.cpp` se toca aquí — el código de §2-§4 es especificación, no parche.
- La implementación de `OpusMemPassthroughOps` sobre Wine real (`src/port/`) no se diseña aquí más allá del punto de instalación (§3.2).
- Los call sites de A/B1/B2/B3 (119 sitios) no se tocan aquí — ese es trabajo de migración posterior, por lotes, siguiendo la disciplina de commits ya establecida (§6 del spec de migración base).
- Tests: no se especifican en este documento. Cuando se implemente, corresponde TDD sobre `OpusShellMemory.cpp` (round-trip propio existente + nuevo caso ajeno con un `ops` de prueba/mock) antes de tocar ningún call site.

## 8. Checklist a verificar antes de implementar

- [ ] Confirmar por censo (no solo `catalog.c`/`elsubs2.c`) que ningún sitio de las categorías A/B1/B2/B3 llama al equivalente de `GlobalHandle` sobre un handle que resultaría foreign bajo este diseño — si alguno lo hace, §2.4 necesita resolverse antes de migrar ese sitio, no después.
- [ ] Confirmar que `OpusMemRealloc` sobre un handle ajeno que Wine reubica (cambia el valor del handle, legal en Win16) se propaga correctamente al llamador — el contrato ya devuelve el nuevo `OpusHandle` como valor de retorno, igual que en el camino propio; verificar que ningún call site de A/B1/B2/B3 asume que el handle no cambia.
- [ ] Punto exacto de instalación de `OpusMemSetPassthrough` en el arranque de `src/port/` (§3.2) — antes de la primera TU de `Opus/` que pueda llamar `OpusMem*`.
- [ ] Prueba de round-trip cruzada (winegcc/wineg++) para el camino foreign, igual que la que ya cubre el camino propio.

---

## 9. Resultado del checklist (2026-08-11, ejecución parcial — items 1-2)

Ejecución de los dos primeros ítems del checklist §8. **Detenida tras el ítem 2** por un
hallazgo que compromete una premisa del diseño (ver §9.2) — items 3 y 4 quedan
pendientes, no ejecutados en esta sesión. Ningún archivo de `src/Opus/` ni
`OpusShellMemory.h`/`.cpp` fue tocado.

### 9.1 Ítem 1 — censo de `GlobalHandle`-equivalente sobre A/B1/B2/B3

```
grep -rn "\bGlobalHandle\b" src/Opus/
```

Resultado: **cero ocurrencias en código compilado.** El único hit real es
`Opus/ripaux.c:52` (`#define MyLock(ps) HIWORD(GlobalHandle(ps))`), y `ripaux.c` no
está en `OPUS_ORIGINAL_ENGINE_SOURCES` — no compila, ya documentado en el spec de
migración base (§0 adenda). El resto de coincidencias son ruido (`.asm`, `.txt`,
`tags`, comentarios de recurso).

**Conclusión: sin hallazgo.** Ningún sitio de A/B1/B2/B3/hub/`hKeys` llama
`GlobalHandle`. La pregunta abierta de §2.4 (registrar punteros ajenos en
`PtrRegistry()`) sigue sin materializarse — confirma lo que el documento ya
adelantaba como no verificado, ahora verificado.

### 9.2 Ítem 2 — semántica de `Realloc` que reubica handle, y hallazgo prioritario

**Censo de sitios `GlobalReAlloc`/`OurGlobalReAlloc` en alcance A/B1/B2/B3/hub** (8
sitios, fuera de `res.c` que ya está cubierto en el spec base §13):

| Sitio | Handle | Flags |
|---|---|---|
| `eldde.c:821` | `hevt` | `GMEM_SENDKEYS` |
| `eldde.c:1176` | `hevt` | `GMEM_SENDKEYS` |
| `eldde.c:1239` | `*lphevtHead` | `GMEM_SENDKEYS` |
| `etcmd.c:1131` | `pghd->ghsz` | `GMEM_MOVEABLE` |
| `etcmd.c:1258` | `pghd->ghsz` | `GMEM_MOVEABLE` |
| `filecvt.c:1336` | `vpexcr->ghBuff` | `GMEM_MOVEABLE` |
| `filecvt.c:1469` | `ghsz` | `GMEM_MOVEABLE` |
| `filecvt.c:1488` | `ghsz` | `GMEM_MOVEABLE` |
| `spelcore.c:687` | `pghd->ghsz` | `GMEM_MOVEABLE` |

**Todos descartan el valor de retorno** — patrón uniforme `if (OurGlobalReAlloc(h, cb,
flags) == NULL) { falla }`, ninguno reasigna `h = OurGlobalReAlloc(...)`. Bajo
semántica real Win32 `GMEM_MOVEABLE`, esto **no es un bug**: el `HGLOBAL` nunca cambia
de valor en un realloc de bloque moveable — solo el puntero subyacente se reubica,
recuperado con un `Lock` posterior (los 9 sitios en efecto vuelven a hacer
`GlobalLock`/`OurGlobalAlloc`+`Lock` después). El ítem 2 del checklist, tal como está
planteado (verificar que ningún call site asuma que el handle no cambia), **no
encuentra bug** en los 9 sitios revisados — todos son seguros bajo el camino `ops`
propuesto, porque `ops.Realloc` termina llamando al mismo `GlobalReAlloc` real que ya
preserva el handle hoy.

**Hallazgo prioritario, no lo que pedía el ítem 2 pero surgido de la misma
inspección — vacío real en el diseño, no pregunta ya prevista:**

```c
// src/Opus/dde.h:29
#define GMEM_SENDKEYS    (GMEM_LOWER|GMEM_MOVEABLE)
```

`GMEM_SENDKEYS` es el flag usado en **los cinco** sitios de alocación/realloc de toda
la familia de Categoría A (`hevt`/`*lphevtHead`/`*lphrgbKeyState`):
`eldde.c:805,821,1176,1240,1321`. Lleva el bit `GMEM_LOWER` (`0x1000`) — el mismo bit
que, bajo §4 de este documento, hace que `OpusMemAlloc` enrute a `OPUS_MEM_FOREIGN`
(`ops.Alloc`) en vez del camino propio:

```c
#define OPUS_MEM_FOREIGN  (OPUS_MEM_DDESHARE | OPUS_MEM_LOWER)
// ...
if (flags & OPUS_MEM_FOREIGN) { /* delega en ops.Alloc, no malloc privado */ }
```

`GMEM_SENDKEYS` es un macro fijo, no un flag que dependa de si `eldde.c` está migrado
o no. Esto contradice directamente §3.3 de este mismo documento:

> "Cuando `eldde.c` se migre más adelante... pasará a asignar vía `OpusMemAlloc` con
> flags planos (sin `GMEM_DDESHARE`/`GMEM_LOWER`) y el handle se volverá propio de
> punta a punta — la mecánica de passthrough no cambia, solo el flujo converge."

Esto es falso específicamente para `hevt`/`lphevtHead`/`lphrgbKeyState`: migrar
`eldde.c` no elimina `GMEM_SENDKEYS` del sitio de llamada — es la semántica original
(memoria baja requerida por el hook de journal-playback, mismo motivo por el que
`hPlaybackHook` ya está clasificado como Categoría D en
`2026-08-11-opus-memory-blocked-categories-design.md` §5). Bajo el enrutado por flag
tal como está especificado en §4, **la Categoría A completa (29 sitios) nunca converge
al camino propio** — queda enrutada a `ops` (passthrough) permanentemente, igual que D,
no de forma transitoria como afirma §3.3.

**Impacto:** no es solo una corrección de comentario. Cambia la lectura de la fila 3
(`Passthrough completo`) del §7 del documento de categorías bloqueadas: los 29 sitios
de A cuentan como "migrados" en el sentido de "pasan por el contrato `OpusShellMemory`
sintácticamente", pero en tiempo de ejecución **siempre** delegan en `ops`, nunca
usan el heap privado del contrato — la misma clase de excepción estructural que ya
motivó D-2 para `hPlaybackHook`/`hfontPhy`/`hCode`. La premisa de que el passthrough
es "temporal hasta que el productor se migre" no aplica a esta familia. Requiere
decisión del mantenedor antes de implementar: o se acepta que A vive permanentemente
en el camino foreign (documentando la corrección a §3.3), o se reclasifica `hevt`
como D (con la misma discusión que ya tuvo `hPlaybackHook`).

### 9.3 Ítems 3 y 4 — no ejecutados

Pendientes para una sesión posterior, por decisión explícita: documentar este hallazgo
y detenerse aquí en vez de continuar con el punto de instalación de
`OpusMemSetPassthrough` (ítem 3) y el diseño del test de round-trip cruzado (ítem 4).
