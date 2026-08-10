#pragma once

/*
 * Contrato de memoria Win16 entre el núcleo Qt y el shell.
 * Diseño: docs/port-qt/01-frontera-nucleo-shell.md, §B3.
 *
 * Reemplaza GlobalAlloc/GlobalLock/GlobalUnlock/GlobalReAlloc/GlobalSize/
 * GlobalFree. El empaquetado de punteros de 64 bits en campos de 16 bits
 * ya está resuelto por src/port/original/opus_x64_compat.h (LOWORDX,
 * HIWORDX, MAKELONGX) y este contrato no lo repite.
 *
 * Regla que no se relaja: los handles son de tiempo de ejecución y no se
 * serializan nunca. Opus/lib/qwindows.h:630 declara `typedef WORD HANDLE`
 * -- un entero de 16 bits -- y struct FTI guarda punteros junto a campos
 * de esa anchura en estructuras que el código persiste. Todo campo de
 * estructura serializada con tipo handle necesita indirección (índice de
 * tabla), no este tipo opaco. Ver §B3.2 para el inventario pendiente de
 * esos campos, que Qt-2 hace antes de tocar formato de archivo.
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
 * Solo declaraciones. Sin implementación en este header.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Handle opaco de ancho completo. Nunca se empaqueta en 16 bits, nunca se
   escribe a disco, nunca se compara contra un literal. */
typedef struct OpusHandleImpl *OpusHandle;

OpusHandle    OpusMemAlloc(unsigned long cb, unsigned flags);
void         *OpusMemLock(OpusHandle h);     /* fija y devuelve puntero */
void          OpusMemUnlock(OpusHandle h);   /* libera la fijación */
OpusHandle    OpusMemRealloc(OpusHandle h, unsigned long cb, unsigned flags);
unsigned long OpusMemSize(OpusHandle h);
void          OpusMemFree(OpusHandle h);

#ifdef __cplusplus
}
#endif
