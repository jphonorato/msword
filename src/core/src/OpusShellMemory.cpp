/*
 * Implementación del contrato de memoria Win16
 * (src/core/include/OpusShellMemory.h, docs/port-qt/01-core-shell-boundary.md
 * §B3).
 *
 * Heap nativo (malloc/realloc/free) detrás de un handle opaco con contador
 * de fijación. La disciplina Lock/Unlock del código original se conserva
 * a propósito -- no se colapsa a punteros crudos -- para poder detectar
 * mal uso (double free, lock tras free) en vez de corromper memoria en
 * silencio si algún día se introduce compactación real.
 *
 * OpusMemHandle (equivalente de GlobalHandle) necesita ir de puntero a
 * handle; se resuelve con un registro global puntero->handle, no con
 * aritmética de punteros, porque el puntero que malloc/realloc devuelve
 * no tiene relación fija con la dirección del OpusHandleImpl que lo posee.
 */

#include "OpusShellMemory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <unordered_map>
#include <unordered_set>

struct OpusHandleImpl {
    void *ptr;
    unsigned long cb;
    int lockCount;
    bool freed;
};

namespace {

/* Registro puntero-vivo -> handle, para OpusMemHandle. Se actualiza en
   cada alloc/realloc/free del camino propio. No hace falta
   sincronización: igual que el resto de este contrato, un OpusHandle es
   de un solo hilo lógico, tal como lo era GlobalAlloc en Win16. Un
   puntero devuelto por gOps.Lock (camino ajeno) nunca se registra aquí --
   pregunta abierta documentada en el diseño de passthrough §2.4, no
   necesaria hoy porque ningún sitio censado llama OpusMemHandle sobre un
   bloque ajeno. */
std::unordered_map<void *, OpusHandle> &PtrRegistry() {
    static std::unordered_map<void *, OpusHandle> registry;
    return registry;
}

void RegisterPtr(void *ptr, OpusHandle h) {
    if (ptr != nullptr) {
        PtrRegistry()[ptr] = h;
    }
}

void UnregisterPtr(void *ptr) {
    if (ptr != nullptr) {
        PtrRegistry().erase(ptr);
    }
}

/* Handles vivos asignados por este contrato (camino privado de
   OpusMemAlloc/OpusMemRealloc). Membership -- nunca desreferencia -- es
   lo que permite distinguir un OpusHandle propio de un HGLOBAL ajeno sin
   leer memoria que no nos pertenece: comparar valores de puntero no es
   UB aunque h apunte a memoria arbitraria. Alta en alloc propio, baja en
   free. Diseño: 2026-08-11-opus-memory-passthrough-design.md §2.2. */
std::unordered_set<OpusHandle> &OwnHandles() {
    static std::unordered_set<OpusHandle> live;
    return live;
}

bool IsOwn(OpusHandle h) {
    return h != nullptr && OwnHandles().count(h) != 0;
}

/* Tabla de passthrough instalada por el shell. NULL = sin instalar,
   estado por defecto -- todo handle ajeno falla controladamente en vez
   de intentar reenviar a una tabla que no existe. */
const OpusMemPassthroughOps *gOps = nullptr;

}  // namespace

extern "C" void OpusMemSetPassthrough(const OpusMemPassthroughOps *ops) {
    gOps = ops;
}

extern "C" OpusHandle OpusMemAlloc(unsigned long cb, unsigned flags) {
    if (flags & OPUS_MEM_FOREIGN) {
        /* Bloque sin equivalente en el heap privado (DDE compartido entre
           procesos, o memoria "baja" para un hook Win16) -- reenvía a la
           tabla real en vez de malloc. Sin ops instalada: fallo
           controlado, NO cae al heap privado -- un handle que el
           productor espera compartir/de memoria baja tratado como
           puntero malloc corriente sería el bug silencioso que este
           mecanismo existe para evitar. */
        if (gOps == nullptr || gOps->Alloc == nullptr) {
            return nullptr;
        }
        /* OPUS_MEM_ZEROINIT viaja junto a los bits de enrutado: no decide
           qué tabla asigna, pero sí es semántica del bloque, y el camino
           ajeno tiene que honrarlo igual que el propio (línea 115). Sin
           esto, un sitio foreign nacido de GHND (= GMEM_MOVEABLE |
           GMEM_ZEROINIT) perdía el cero en silencio -- primer caso real:
           raremsg.c, hdata para el portapapeles, GHND|OPUS_MEM_ESCAPES. */
        return gOps->Alloc(cb, flags & (OPUS_MEM_FOREIGN | OPUS_MEM_ZEROINIT));
    }

    OpusHandleImpl *h = new (std::nothrow) OpusHandleImpl();
    if (h == nullptr) {
        return nullptr;
    }
    /* cb == 0 sigue siendo un handle válido en Win16 (GlobalAlloc(flags, 0)
       es legal); malloc(0) puede devolver NULL sin ser un fallo, así que
       se pide al menos 1 byte para no confundir "sin memoria" con
       "bloque de tamaño cero". */
    h->ptr = std::malloc(cb ? cb : 1);
    if (h->ptr == nullptr) {
        delete h;
        return nullptr;
    }
    if (flags & OPUS_MEM_ZEROINIT) {
        std::memset(h->ptr, 0, cb ? cb : 1);
    }
    h->cb = cb;
    h->lockCount = 0;
    h->freed = false;
    RegisterPtr(h->ptr, h);
    OwnHandles().insert(h);
    return h;
}

extern "C" void *OpusMemLock(OpusHandle h) {
    if (IsOwn(h)) {
        /* Desreferencia segura: confirmado propio por membership, no por
           inspección de contenido. */
        if (h->freed) {
            /* Fallo controlado: NULL, no un puntero colgante. Lock tras
               Free es exactamente el caso que este contrato tiene que
               rechazar sin corromper memoria -- ver la sonda de
               round-trip. */
            return nullptr;
        }
        ++h->lockCount;
        return h->ptr;
    }
    if (h != nullptr && gOps != nullptr && gOps->Lock != nullptr) {
        return gOps->Lock(h);
    }
    return nullptr;  /* ajeno sin ops instalada: fallo controlado */
}

extern "C" void OpusMemUnlock(OpusHandle h) {
    if (IsOwn(h)) {
        if (h->freed) {
            return;
        }
        if (h->lockCount > 0) {
            --h->lockCount;
        }
        return;
    }
    if (h != nullptr && gOps != nullptr && gOps->Unlock != nullptr) {
        gOps->Unlock(h);
    }
}

extern "C" OpusHandle OpusMemRealloc(OpusHandle h, unsigned long cb,
                                      unsigned flags) {
    if (IsOwn(h)) {
        if (h->freed) {
            return nullptr;
        }
        void *oldPtr = h->ptr;
        void *newPtr = std::realloc(oldPtr, cb ? cb : 1);
        if (newPtr == nullptr) {
            return nullptr; /* h sigue siendo válido con su bloque original */
        }
        if ((flags & OPUS_MEM_ZEROINIT) && cb > h->cb) {
            std::memset(static_cast<char *>(newPtr) + h->cb, 0, cb - h->cb);
        }
        if (newPtr != oldPtr) {
            UnregisterPtr(oldPtr);
            RegisterPtr(newPtr, h);
        }
        h->ptr = newPtr;
        h->cb = cb;
        return h;
    }
    /* Enruta por IsOwn(h), nunca por flags -- el handle ya existe y su
       procedencia se decidió en Alloc, no aquí. Ver diseño §4.1: un
       bloque nacido OPUS_MEM_DDESHARE que se realloca con flags planos
       (filecvt.c: nace gmemLibShare, realloca GMEM_MOVEABLE) enrutado por
       flags de Realloc iría al heap privado por error. Invariante de
       contrato (diseño §4.2): esta función nunca devuelve un OpusHandle
       distinto del recibido, salvo fallo (NULL) -- responsabilidad de
       gOps->Realloc mantenerlo también en el camino ajeno. */
    if (h != nullptr && gOps != nullptr && gOps->Realloc != nullptr) {
        return gOps->Realloc(h, cb,
                             flags & (OPUS_MEM_FOREIGN | OPUS_MEM_ZEROINIT));
    }
    return nullptr;
}

extern "C" unsigned long OpusMemSize(OpusHandle h) {
    if (IsOwn(h)) {
        if (h->freed) {
            return 0;
        }
        return h->cb;
    }
    if (h != nullptr && gOps != nullptr && gOps->Size != nullptr) {
        return gOps->Size(h);
    }
    return 0;
}

extern "C" OpusHandle OpusMemHandle(void *ptr) {
    if (ptr == nullptr) {
        return nullptr;
    }
    auto it = PtrRegistry().find(ptr);
    if (it == PtrRegistry().end()) {
        return nullptr;
    }
    return it->second;
}

extern "C" void OpusMemFree(OpusHandle h) {
    if (IsOwn(h)) {
        if (h->freed) {
            /* Doble liberación sobre handle propio: falla de forma
               ruidosa y controlada. La alternativa -- silenciarlo -- es
               exactamente el modo de corrupción silenciosa que este
               contrato existe para evitar. */
            std::fprintf(
                stderr,
                "OpusMemFree: liberacion doble detectada (handle %p)\n",
                static_cast<void *>(h));
            std::abort();
        }
        UnregisterPtr(h->ptr);
        OwnHandles().erase(h);
        std::free(h->ptr);
        h->ptr = nullptr;
        h->freed = true;
        /* OpusHandleImpl en sí no se libera: mantenerlo vivo (con
           freed=true) es lo que permite detectar la doble liberación
           arriba en vez de leer memoria ya reciclada por el allocator del
           proceso. Es el mismo patrón de "tombstone" que un detector de
           use-after-free. */
        return;
    }
    /* Ajeno: no hay tombstone posible (no es nuestro struct), así que no
       hay detección de doble-free en este camino -- documentado como tal
       en el diseño §4.2/checklist P7: Wine no aborta en doble
       GlobalFree, medido. Reenviar siempre, sin intento de emular la
       aserción del camino propio. */
    if (h != nullptr && gOps != nullptr && gOps->Free != nullptr) {
        gOps->Free(h);
    }
}
