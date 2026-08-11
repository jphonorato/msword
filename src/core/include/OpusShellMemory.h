#pragma once

/*
 * Contrato de memoria Win16 entre el núcleo Qt y el shell.
 * Diseño: docs/port-qt/01-frontera-nucleo-shell.md, §B3.
 *
 * Reemplaza GlobalAlloc/GlobalFree/GlobalLock/GlobalUnlock/GlobalReAlloc/
 * GlobalSize/GlobalHandle y sus equivalentes Local* -- bajo este port ambos
 * heaps son el mismo heap nativo, así que un solo contrato cubre las dos
 * familias; no hay una distinción Local/Global que preservar en 2026. El
 * empaquetado de punteros de 64 bits en campos de 16 bits ya está resuelto
 * por src/port/original/opus_x64_compat.h (LOWORDX, HIWORDX, MAKELONGX) y
 * este contrato no lo repite.
 *
 * Corrección sobre una revisión anterior de este header: `HANDLE` NO mide
 * 16 bits en este build. `Opus/lib/qwindows.h:630` (`typedef WORD HANDLE`)
 * es el SDK Win16 vendorizado, pero esa rama nunca se compila bajo
 * OPUS_X64 -- `word.h` incluye `opus_x64_compat.h` en su lugar, que arrastra
 * el `windows.h` real de Wine (`winnt.h: typedef void *HANDLE`). Medido:
 * `sizeof(HANDLE) == 8` en este build. El contrato de este header no existe
 * por un problema de ancho de bits -- ya está resuelto de fábrica -- sino
 * porque un handle sigue siendo una indirección de tiempo de ejecución que
 * el núcleo no debe fijar por diseño (independencia del shell) ni
 * serializar (ver la regla siguiente).
 *
 * Regla que no se relaja: los handles son de tiempo de ejecución y no se
 * serializan nunca. Verificado por inventario, no asumido: Qt-2 Fase 1
 * (docs/port-qt/01-frontera-nucleo-shell.md §B3) recorrió toda estructura
 * con campo tipo HANDLE/GLOBALHANDLE/HGLOBAL/LOCALHANDLE en `Opus/` y
 * confirmó que ninguna se escribe a disco -- todas son estado de sesión
 * (DDE, registro de teclas, carga de DLL de conversión, caché de fuente,
 * handle de archivo abierto). El formato de documento en disco (FIB, DOP,
 * STSH, PAP, CHP, SEP, BKF, FFN, STTB) no tiene un solo campo handle.
 *
 * El modelo Win16 permite que el gestor mueva un bloque desbloqueado:
 * GlobalLock/GlobalUnlock forman pares que el código respeta hoy. La
 * implementación del shell puede fijar todo permanentemente, pero los
 * pares Lock/Unlock deben conservarse en las llamadas del núcleo -- no son
 * no-operaciones seguras si más adelante se introduce compactación.
 *
 * GlobalLockClip (bloqueo de portapapeles) no es parte de este contrato:
 * va al contrato de portapapeles de Qt-6.
 *
 * Solo declaraciones de la API pública. La implementación
 * (src/core/src/OpusShellMemory.cpp) es la primera de los tres contratos
 * restantes en tener código real -- ver la verificación de round-trip de
 * handle en el documento de diseño.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Handle opaco de ancho completo. Nunca se empaqueta en 16 bits, nunca se
   escribe a disco, nunca se compara contra un literal. */
typedef struct OpusHandleImpl *OpusHandle;

/* Único flag con semántica propia hoy: equivalente a GMEM_ZEROINIT/
   LMEM_ZEROINIT. El resto de los flags de Win16 (GMEM_MOVEABLE,
   GMEM_FIXED, GMEM_SHARE, ...) distinguían estrategias del gestor de
   heap segmentado de 16 bits; no tienen contraparte en un heap nativo de
   64 bits y no se admiten -- se aceptan y se ignoran, no es un error
   pasarlos, pero tampoco cambian el comportamiento. */
#define OPUS_MEM_ZEROINIT 0x0001u

/* Bits de enrutado passthrough (docs/superpowers/specs/
   2026-08-11-opus-memory-passthrough-design.md §4). Un bloque cuyos flags
   Win16 llevan GMEM_SHARE|GMEM_DDESHARE o GMEM_LOWER|GMEM_NOT_BANKED no
   tiene equivalente en el heap privado de este contrato -- necesita ser
   un HGLOBAL real (compartible entre procesos, o en memoria "baja" para
   un hook de journal-playback). OpusMemAlloc delega esos bloques en la
   tabla de ops instalada por el shell (OpusMemSetPassthrough) en vez de
   malloc; si no hay ops instalada, falla controladamente (NULL), no cae
   al heap privado. */
#define OPUS_MEM_DDESHARE 0x0002u  /* GMEM_SHARE | GMEM_DDESHARE, Win16 0x2000 */
#define OPUS_MEM_LOWER    0x0004u  /* GMEM_LOWER | GMEM_NOT_BANKED, Win16 0x1000 */

/* No corresponde a ningún bit Win16 -- se pasa explícitamente en el call
   site migrado, no se deriva de OpusMemFlagsFromWin16(). Cubre los
   sitios donde el enrutado por flag de arriba se equivoca: el bloque se
   aloca con flags planos (GMEM_MOVEABLE/GHND, sin DDESHARE ni LOWER) pero
   el handle cruza la frontera de este contrato de todos modos --
   entregado a una DLL Win16 externa vía CallOtherStack
   (spelcore.c/etcmd.c), o cedido al portapapeles del sistema
   (SetClipboardData en raremsg.c). Un puntero de malloc entregado ahí es
   el modo de fallo que el passthrough existe para evitar; los flags
   Win16 originales no lo señalizan porque en Win16 todo handle GMEM_*
   servía indistintamente para ambos usos -- la distinción "se queda en
   este proceso" vs "sale de este contrato" no existía como bit, existía
   como conocimiento del programador sobre qué hacía cada función.
   Decisión y censo de sitios: docs/superpowers/specs/
   2026-08-11-opus-memory-p2-boundary-crossing-decision.md. */
#define OPUS_MEM_ESCAPES  0x0008u

#define OPUS_MEM_FOREIGN  (OPUS_MEM_DDESHARE | OPUS_MEM_LOWER | OPUS_MEM_ESCAPES)

/* Traduce los flags Win16 de una llamada GlobalAlloc/GlobalReAlloc a los
   de este contrato. GMEM_MOVEABLE y GMEM_FIXED distinguían estrategias
   del heap segmentado de 16 bits sin contraparte aquí -- se ignoran, no
   es error pasarlos. GMEM_ZEROINIT es el único bit con semántica propia
   sobre el heap privado, e implementada de verdad (no solo traducida sin
   efecto): OpusMemAlloc (OpusShellMemory.cpp) hace memset(0) sobre el
   bloque cuando el flag está presente, y OpusMemRealloc lo hace sobre el
   tramo nuevo al crecer. Primer sitio real que ejerce esta rama:
   Opus/catalog.c HGrabFarMem, vía GHND (= GMEM_MOVEABLE|GMEM_ZEROINIT) --
   docs/superpowers/specs/2026-08-10-opus-memory-migration-design.md §4,
   lote 2. OPUS_MEM_DDESHARE/OPUS_MEM_LOWER no cambian el bloque en sí --
   deciden qué tabla lo asigna, ver arriba. Literales en vez de las
   macros GMEM_* del SDK Win16: este header no incluye
   Opus/lib/qwindows.h (lado núcleo, no depende del SDK vendorizado, ver
   01-frontera-nucleo-shell.md). Valores confirmados en
   Opus/lib/qwindows.h:1736 (GMEM_ZEROINIT), Opus/dde.h:28 (GMEM_DDE
   contiene 0x2000), Opus/dde.h:29 (GMEM_SENDKEYS contiene 0x1000). */
static inline unsigned OpusMemFlagsFromWin16(unsigned win16Flags)
{
    unsigned flags = 0;
    if (win16Flags & 0x0040u /* GMEM_ZEROINIT */)
        flags |= OPUS_MEM_ZEROINIT;
    if (win16Flags & 0x2000u /* GMEM_SHARE | GMEM_DDESHARE */)
        flags |= OPUS_MEM_DDESHARE;
    if (win16Flags & 0x1000u /* GMEM_LOWER | GMEM_NOT_BANKED */)
        flags |= OPUS_MEM_LOWER;
    return flags;
}

/* Tabla de function pointers que el shell (src/port/) instala para
   reenviar handles ajenos a la API Win32 real de Wine. src/core/ no
   incluye Win32 -- ver docs/port-qt/01-frontera-nucleo-shell.md -- así
   que esta tabla es el único punto donde el núcleo toca algo que no es
   malloc/realloc/free, y lo hace por indirección, sin linkear contra
   windows.h. win16Flags en Alloc/Realloc son los bits OPUS_MEM_DDESHARE/
   OPUS_MEM_LOWER ya reducidos -- no los flags Win16 crudos -- la
   reconstrucción exacta hacia GMEM_* reales es responsabilidad de la
   implementación en src/port/. h en Lock/Unlock/Realloc/Size/Free es un
   OpusHandle que en realidad es un HGLOBAL reinterpretado; este contrato
   nunca lo desreferencia como OpusHandleImpl*, solo lo pasa de largo. */
typedef struct OpusMemPassthroughOps {
    OpusHandle    (*Alloc)(unsigned long cb, unsigned win16Flags);
    void         *(*Lock)(OpusHandle h);
    void          (*Unlock)(OpusHandle h);
    OpusHandle    (*Realloc)(OpusHandle h, unsigned long cb, unsigned win16Flags);
    unsigned long (*Size)(OpusHandle h);
    void          (*Free)(OpusHandle h);
} OpusMemPassthroughOps;

/* ops == NULL desinstala el passthrough (estado por defecto: todo handle
   no-propio falla controladamente, ver OpusMemLock/etc. más abajo). No
   hay ownership de *ops -- el llamador (shell) mantiene el struct vivo
   mientras esté instalado; un puntero a static/global basta. No es
   thread-safe -- mismo supuesto de hilo único que el resto del
   contrato. */
void OpusMemSetPassthrough(const OpusMemPassthroughOps *ops);

OpusHandle    OpusMemAlloc(unsigned long cb, unsigned flags);
void         *OpusMemLock(OpusHandle h);     /* fija y devuelve puntero */
void          OpusMemUnlock(OpusHandle h);   /* libera la fijación */
OpusHandle    OpusMemRealloc(OpusHandle h, unsigned long cb, unsigned flags);
unsigned long OpusMemSize(OpusHandle h);

/* Equivalente de GlobalHandle: recupera el handle dueño de un puntero ya
   obtenido con OpusMemLock. Devuelve NULL si ptr no proviene de este
   allocator o si el handle ya fue liberado. */
OpusHandle    OpusMemHandle(void *ptr);

/* Libera el bloque. Una segunda llamada sobre el mismo handle, o un
   OpusMemLock posterior, deben fallar de forma controlada -- ver la
   verificación de round-trip en el documento de diseño -- no corromper
   memoria en silencio. */
void          OpusMemFree(OpusHandle h);

#ifdef __cplusplus
}
#endif
