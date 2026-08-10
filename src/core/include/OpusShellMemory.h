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
