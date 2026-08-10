/*
 * Implementación del contrato de memoria Win16
 * (src/core/include/OpusShellMemory.h, docs/port-qt/01-frontera-nucleo-shell.md
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

struct OpusHandleImpl {
    void *ptr;
    unsigned long cb;
    int lockCount;
    bool freed;
};

namespace {

/* Registro puntero-vivo -> handle, para OpusMemHandle. Se actualiza en
   cada alloc/realloc/free. No hace falta sincronización: igual que el
   resto de este contrato, un OpusHandle es de un solo hilo lógico, tal
   como lo era GlobalAlloc en Win16. */
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

}  // namespace

extern "C" OpusHandle OpusMemAlloc(unsigned long cb, unsigned flags) {
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
    return h;
}

extern "C" void *OpusMemLock(OpusHandle h) {
    if (h == nullptr || h->freed) {
        /* Fallo controlado: NULL, no un puntero colgante. Lock tras Free
           es exactamente el caso que este contrato tiene que rechazar sin
           corromper memoria -- ver la sonda de round-trip. */
        return nullptr;
    }
    ++h->lockCount;
    return h->ptr;
}

extern "C" void OpusMemUnlock(OpusHandle h) {
    if (h == nullptr || h->freed) {
        return;
    }
    if (h->lockCount > 0) {
        --h->lockCount;
    }
}

extern "C" OpusHandle OpusMemRealloc(OpusHandle h, unsigned long cb,
                                      unsigned flags) {
    if (h == nullptr || h->freed) {
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

extern "C" unsigned long OpusMemSize(OpusHandle h) {
    if (h == nullptr || h->freed) {
        return 0;
    }
    return h->cb;
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
    if (h == nullptr) {
        return;
    }
    if (h->freed) {
        /* Doble liberación: falla de forma ruidosa y controlada. La
           alternativa -- silenciarlo -- es exactamente el modo de
           corrupción silenciosa que este contrato existe para evitar. */
        std::fprintf(stderr,
                     "OpusMemFree: liberacion doble detectada (handle %p)\n",
                     static_cast<void *>(h));
        std::abort();
    }
    UnregisterPtr(h->ptr);
    std::free(h->ptr);
    h->ptr = nullptr;
    h->freed = true;
    /* OpusHandleImpl en sí no se libera: mantenerlo vivo (con freed=true)
       es lo que permite detectar la doble liberación arriba en vez de
       leer memoria ya reciclada por el allocator del proceso. Es el mismo
       patrón de "tombstone" que un detector de use-after-free. */
}
