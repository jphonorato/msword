/*
 * Prueba de round-trip del camino foreign de OpusShellMemory (P6 de
 * 2026-08-11-opus-memory-passthrough-checklist-audit.md §7, diseño §4.3-
 * §4.5 de 2026-08-11-opus-memory-passthrough-design.md).
 *
 * Vive en src/port/tests/, no en src/core/: necesita windows.h real de
 * Wine, y src/core/ no puede depender de eso por construcción (frontera
 * núcleo/shell, docs/port-qt/01-frontera-nucleo-shell.md). Se compila con
 * wineg++ (toolchain por omisión del proyecto bajo OPUS_WINELIB_BUILD),
 * no con el g++ nativo del ExternalProject de src/core/.
 *
 * Dos tablas de ops, no una (diseño §4.5):
 *   - opsWine: envoltorio real de GlobalAlloc/Lock/Unlock/ReAlloc/Size/
 *     Free + contadores. Da el round-trip de datos real contra Wine.
 *   - opsGuard: sintética, handles en página mmap(PROT_NONE). Su valor no
 *     es la fidelidad sino convertir una desreferencia indebida de un
 *     OpusHandle ajeno (tratarlo como OpusHandleImpl*) en un fallo duro y
 *     determinista -- una implementación que desreferenciara handles
 *     ajenos pasaría los casos A/B sin inmutarse (medido, checklist-audit
 *     §4.2 hallazgo B), pero no pasaría opsGuard.
 *
 * Convención Check()/g_failures de OpusShellConfig_test.cpp, adaptada:
 * aquí además se reporta el código de salida en vez de abortar en el
 * primer fallo, porque el caso D1 depende de que el proceso llegue vivo
 * hasta el final para poder afirmar "no hubo SIGSEGV".
 */

#include "OpusShellMemory.h"

#include <windows.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FALLO: %s\n", what);
        ++g_failures;
    }
}

/* ---- opsWine: envoltorio real + instrumentación (§4.5) ---- */

struct WineCounters {
    int alloc = 0;
    int lock = 0;
    int unlock = 0;
    int realloc = 0;
    int size = 0;
    int free = 0;
};

WineCounters g_wineCounters;

/* win16Flags aquí ya son los bits reducidos OPUS_MEM_DDESHARE/
   OPUS_MEM_LOWER (ver OpusShellMemory.h) -- la reconstrucción hacia
   GMEM_* reales es responsabilidad de esta implementación, no del
   contrato. GMEM_MOVEABLE siempre se añade: es la única forma en que
   GlobalReAlloc puede tener éxito bajo Wine (medido, checklist-audit
   §2.3) y es lo que sostiene la invariante de handle estable del diseño
   §4.2. */
unsigned ReconstructWin16Flags(unsigned reduced) {
    unsigned f = GMEM_MOVEABLE;
    if (reduced & OPUS_MEM_DDESHARE) f |= GMEM_DDESHARE;
    if (reduced & OPUS_MEM_LOWER) f |= GMEM_LOWER;
    return f;
}

OpusHandle WineAlloc(unsigned long cb, unsigned win16Flags) {
    ++g_wineCounters.alloc;
    HGLOBAL h = GlobalAlloc(ReconstructWin16Flags(win16Flags), (DWORD)cb);
    return reinterpret_cast<OpusHandle>(h);
}

void *WineLock(OpusHandle h) {
    ++g_wineCounters.lock;
    return GlobalLock(reinterpret_cast<HGLOBAL>(h));
}

void WineUnlock(OpusHandle h) {
    ++g_wineCounters.unlock;
    GlobalUnlock(reinterpret_cast<HGLOBAL>(h));
}

OpusHandle WineRealloc(OpusHandle h, unsigned long cb, unsigned win16Flags) {
    ++g_wineCounters.realloc;
    HGLOBAL nh = GlobalReAlloc(reinterpret_cast<HGLOBAL>(h), (DWORD)cb,
                                ReconstructWin16Flags(win16Flags));
    return reinterpret_cast<OpusHandle>(nh);
}

unsigned long WineSize(OpusHandle h) {
    ++g_wineCounters.size;
    return (unsigned long)GlobalSize(reinterpret_cast<HGLOBAL>(h));
}

void WineFree(OpusHandle h) {
    ++g_wineCounters.free;
    /* No se emula "abortar en doble free": medido en Wine 10.0
       (checklist-audit §4.2 hallazgo A) que una segunda GlobalFree no
       aborta -- devuelve el propio handle (fallo Win32) con
       GetLastError()==0 y el proceso sigue vivo. Reenviar siempre es el
       contrato del camino foreign (diseño §4.2, P7). */
    GlobalFree(reinterpret_cast<HGLOBAL>(h));
}

const OpusMemPassthroughOps kOpsWine = {
    WineAlloc, WineLock, WineUnlock, WineRealloc, WineSize, WineFree,
};

/* ---- opsGuard: página PROT_NONE, nunca desreferencia (caso D1) ---- */

constexpr int kGuardSlots = 8;
constexpr unsigned long kGuardStride = 64;

struct GuardSlot {
    bool used = false;
    void *realPtr = nullptr;
    unsigned long cb = 0;
};

void *g_guardPage = nullptr;
GuardSlot g_guardSlots[kGuardSlots];

/* El OpusHandle "ajeno" que se entrega es una dirección dentro de la
   página PROT_NONE -- nunca se lee ni se escribe. El bloque real vive
   aparte (malloc normal) y se busca por índice, no por desreferencia del
   handle. Una implementación de OpusShellMemory que tratara este handle
   como OpusHandleImpl* haría SIGSEGV en la primera lectura (p. ej.
   h->freed) porque la página no tiene permisos -- ver main() para la
   aserción de "el proceso sigue vivo". */
int SlotIndexFromHandle(OpusHandle h) {
    auto addr = reinterpret_cast<unsigned char *>(h);
    auto base = reinterpret_cast<unsigned char *>(g_guardPage);
    if (addr < base) return -1;
    unsigned long off = (unsigned long)(addr - base);
    if (off % kGuardStride != 0) return -1;
    unsigned long idx = off / kGuardStride;
    if (idx >= (unsigned long)kGuardSlots) return -1;
    return (int)idx;
}

OpusHandle GuardAlloc(unsigned long cb, unsigned /*win16Flags*/) {
    for (int i = 0; i < kGuardSlots; ++i) {
        if (!g_guardSlots[i].used) {
            g_guardSlots[i].used = true;
            g_guardSlots[i].realPtr = std::malloc(cb ? cb : 1);
            g_guardSlots[i].cb = cb;
            auto base = reinterpret_cast<unsigned char *>(g_guardPage);
            return reinterpret_cast<OpusHandle>(base + i * kGuardStride);
        }
    }
    return nullptr;
}

void *GuardLock(OpusHandle h) {
    int i = SlotIndexFromHandle(h);
    if (i < 0 || !g_guardSlots[i].used) return nullptr;
    return g_guardSlots[i].realPtr;
}

void GuardUnlock(OpusHandle) {}

OpusHandle GuardRealloc(OpusHandle h, unsigned long cb, unsigned) {
    int i = SlotIndexFromHandle(h);
    if (i < 0 || !g_guardSlots[i].used) return nullptr;
    g_guardSlots[i].realPtr = std::realloc(g_guardSlots[i].realPtr, cb ? cb : 1);
    g_guardSlots[i].cb = cb;
    return h; /* identidad estable, mismo invariante que el diseño §4.2 */
}

unsigned long GuardSize(OpusHandle h) {
    int i = SlotIndexFromHandle(h);
    if (i < 0 || !g_guardSlots[i].used) return 0;
    return g_guardSlots[i].cb;
}

void GuardFree(OpusHandle h) {
    int i = SlotIndexFromHandle(h);
    if (i < 0 || !g_guardSlots[i].used) return;
    std::free(g_guardSlots[i].realPtr);
    g_guardSlots[i].used = false;
    g_guardSlots[i].realPtr = nullptr;
}

const OpusMemPassthroughOps kOpsGuard = {
    GuardAlloc, GuardLock, GuardUnlock, GuardRealloc, GuardSize, GuardFree,
};

const char kPattern[] = "opus-shell-memory-foreign-roundtrip";

/* ---- Caso A: productor ajeno (simula elsubs2.c/eldde.c sin migrar) ---- */
void CaseA_ForeignProducer() {
    g_wineCounters = WineCounters();
    OpusMemSetPassthrough(&kOpsWine);

    HGLOBAL h = GlobalAlloc(GMEM_LOWER | GMEM_MOVEABLE, sizeof(kPattern));
    Check(h != nullptr, "CaseA: GlobalAlloc(GMEM_LOWER|MOVEABLE) devolvio NULL");
    Check(GlobalSize(h) == sizeof(kPattern), "CaseA: GlobalSize inicial incorrecto");

    void *p0 = GlobalLock(h);
    Check(p0 != nullptr, "CaseA: GlobalLock inicial devolvio NULL");
    std::memcpy(p0, kPattern, sizeof(kPattern));
    GlobalUnlock(h);
    Check((GlobalFlags(h) & 0xFF) == 0, "CaseA: lock count no volvio a 0 tras Unlock externo");

    OpusHandle oh = reinterpret_cast<OpusHandle>(h);
    void *p1 = OpusMemLock(oh);
    Check(p1 == p0, "CaseA: OpusMemLock no devolvio el mismo puntero que GlobalLock");
    Check(std::memcmp(p1, kPattern, sizeof(kPattern)) == 0,
          "CaseA: patron no sobrevivio el cruce a OpusMemLock");
    Check((GlobalFlags(h) & 0xFF) == 1, "CaseA: OpusMemLock no incremento el lock count real");
    Check(g_wineCounters.lock == 1, "CaseA: contador de lock de opsWine no se movio");

    OpusMemUnlock(oh);
    Check((GlobalFlags(h) & 0xFF) == 0, "CaseA: OpusMemUnlock no bajo el lock count real");
    Check(g_wineCounters.unlock == 1, "CaseA: contador de unlock de opsWine no se movio");

    Check(OpusMemSize(oh) == sizeof(kPattern), "CaseA: OpusMemSize incorrecto");
    Check(OpusMemHandle(p1) == nullptr,
          "CaseA: OpusMemHandle sobre puntero ajeno debe ser NULL (diseno Sec2.4)");

    OpusMemFree(oh);
    Check(g_wineCounters.free == 1, "CaseA: contador de free de opsWine no se movio");
    Check((GlobalFlags(h) & 0xFFFF) == GMEM_INVALID_HANDLE,
          "CaseA: bloque sigue vivo tras OpusMemFree");
    Check(GlobalSize(h) == 0, "CaseA: GlobalSize != 0 tras OpusMemFree");

    Check(OpusMemLock(oh) == nullptr, "CaseA: OpusMemLock tras free debe fallar controlado");

    OpusMemSetPassthrough(nullptr);
}

/* ---- Caso B: OpusMemAlloc con flags foreign ---- */
void CaseB_OwnForeignAlloc(unsigned foreignFlag, const char *label) {
    g_wineCounters = WineCounters();
    OpusMemSetPassthrough(&kOpsWine);

    OpusHandle h = OpusMemAlloc(64, foreignFlag);
    Check(h != nullptr, label);
    Check(g_wineCounters.alloc == 1, label);

    HGLOBAL wh = reinterpret_cast<HGLOBAL>(h);
    Check(GlobalSize(wh) == 64,
          "CaseB: GlobalSize sobre handle de OpusMemAlloc(foreign) debe ser 64 "
          "(un OpusHandleImpl* daria 0 -- discriminador de checklist-audit Sec4.2)");
    Check(GlobalLock(wh) != (void *)h,
          "CaseB: GlobalLock debe devolver puntero != handle (moveable real, "
          "un OpusHandleImpl* daria igualdad)");
    GlobalUnlock(wh);

    void *p = OpusMemLock(h);
    Check(p != nullptr, label);
    std::memcpy(p, kPattern, sizeof(kPattern));

    void *pw = GlobalLock(wh);
    Check(pw == p, "CaseB: verificacion cruzada, GlobalLock externo != OpusMemLock");
    Check(std::memcmp(pw, kPattern, sizeof(kPattern)) == 0, "CaseB: patron no sobrevivio");
    Check((GlobalFlags(wh) & 0xFF) == 2,
          "CaseB: lock count debe ser 2 (OpusMemLock + GlobalLock externo)");
    GlobalUnlock(wh); /* deshace el lock de verificacion cruzada */

    OpusMemUnlock(h);
    Check(OpusMemSize(h) == 64, label);

    OpusMemFree(h);
    Check((GlobalFlags(wh) & 0xFFFF) == GMEM_INVALID_HANDLE, label);

    /* Control negativo en la misma corrida: OPUS_MEM_ZEROINIT es propio,
       no debe tocar opsWine. */
    OpusHandle hp = OpusMemAlloc(64, OPUS_MEM_ZEROINIT);
    Check(hp != nullptr, "CaseB: control negativo (propio) fallo alloc");
    Check(g_wineCounters.alloc == 1,
          "CaseB: alloc propio no debe incrementar el contador de opsWine");
    void *pp = OpusMemLock(hp);
    Check(pp != nullptr, "CaseB: control negativo, OpusMemLock devolvio NULL");
    unsigned char zero[64] = {0};
    Check(std::memcmp(pp, zero, 64) == 0, "CaseB: control negativo, ZEROINIT no aplico");
    OpusMemUnlock(hp);
    OpusMemFree(hp);

    OpusMemSetPassthrough(nullptr);
}

/* ---- Caso C: sin ops instalada, fallo cerrado ---- */
void CaseC_NoOpsInstalled() {
    OpusMemSetPassthrough(nullptr);

    Check(OpusMemAlloc(64, OPUS_MEM_DDESHARE) == nullptr,
          "CaseC: OpusMemAlloc(DDESHARE) sin ops debe fallar controlado");
    Check(OpusMemAlloc(64, OPUS_MEM_LOWER) == nullptr,
          "CaseC: OpusMemAlloc(LOWER) sin ops debe fallar controlado");
    Check(OpusMemAlloc(64, OPUS_MEM_DDESHARE | OPUS_MEM_ZEROINIT) == nullptr,
          "CaseC: el bit foreign manda incluso combinado con ZEROINIT");

    OpusHandle hp = OpusMemAlloc(64, OPUS_MEM_ZEROINIT);
    Check(hp != nullptr, "CaseC: control, alloc propio debe seguir funcionando");
    void *pp = OpusMemLock(hp);
    unsigned char zero[64] = {0};
    Check(pp != nullptr && std::memcmp(pp, zero, 64) == 0,
          "CaseC: control, ZEROINIT propio no aplico");
    OpusMemUnlock(hp);

    /* Consumo de un handle ajeno real sin ops instalada: no debe abortar
       ni intentar GlobalFree -- el bloque real, hecho fuera del contrato
       (control del propio test), debe seguir vivo despues. */
    HGLOBAL wh = GlobalAlloc(GMEM_LOWER | GMEM_MOVEABLE, 16);
    OpusHandle foreignH = reinterpret_cast<OpusHandle>(wh);
    Check(OpusMemLock(foreignH) == nullptr, "CaseC: OpusMemLock ajeno sin ops debe ser NULL");
    Check(OpusMemSize(foreignH) == 0, "CaseC: OpusMemSize ajeno sin ops debe ser 0");
    OpusMemFree(foreignH); /* no debe abortar */
    Check((GlobalFlags(wh) & 0xFFFF) != GMEM_INVALID_HANDLE,
          "CaseC: OpusMemFree sin ops no debe haber liberado el bloque ajeno");
    GlobalFree(wh); /* limpieza real */

    OpusMemFree(hp);
}

/* ---- Caso D1: pagina guardia, decisivo (diseno Sec4.4) ---- */
void CaseD1_GuardPage() {
    long pageSize = sysconf(_SC_PAGESIZE);
    g_guardPage = mmap(nullptr, (size_t)pageSize, PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Check(g_guardPage != MAP_FAILED, "CaseD1: mmap(PROT_NONE) fallo");
    if (g_guardPage == MAP_FAILED) return;

    OpusMemSetPassthrough(&kOpsGuard);

    OpusHandle h = OpusMemAlloc(32, OPUS_MEM_LOWER);
    Check(h != nullptr, "CaseD1: OpusMemAlloc via opsGuard devolvio NULL");

    void *p = OpusMemLock(h);
    Check(p != nullptr, "CaseD1: OpusMemLock via opsGuard devolvio NULL");
    std::memcpy(p, kPattern, sizeof(kPattern));
    Check(std::memcmp(p, kPattern, sizeof(kPattern)) == 0, "CaseD1: patron no sobrevivio");
    OpusMemUnlock(h);

    Check(OpusMemSize(h) == 32, "CaseD1: OpusMemSize via opsGuard incorrecto");
    OpusMemFree(h);

    /* Llegar hasta aqui vivos (sin SIGSEGV) ES la asercion: una
       implementacion que desreferenciara el OpusHandle ajeno como
       OpusHandleImpl* habria muerto en la primera lectura (h->freed)
       porque g_guardPage no tiene permisos de lectura. */
    std::fprintf(stderr, "CaseD1: OK, el proceso llego vivo (ninguna funcion "
                          "desreferencio el handle ajeno)\n");

    OpusMemSetPassthrough(nullptr);
    munmap(g_guardPage, (size_t)pageSize);
}

/* ---- Caso D2: centinela en el bloque real, GMEM_FIXED ---- */
void CaseD2_SentinelFixedBlock() {
    OpusMemSetPassthrough(&kOpsWine);

    /* GMEM_FIXED: el handle ES el puntero al payload (medido, diseno
       Sec4.4 D2) -- exactamente la forma de eldde.c:1261. Rellenar antes
       de pasarlo al contrato: si algo lo desreferenciara como
       OpusHandleImpl, los campos serian todos 0xFF sea cual sea el
       layout. */
    HGLOBAL wh = GlobalAlloc(GMEM_FIXED | GMEM_LOWER, 64);
    Check(wh != nullptr, "CaseD2: GlobalAlloc(FIXED|LOWER) devolvio NULL");
    std::memset(wh, 0xFF, 64);

    OpusHandle h = reinterpret_cast<OpusHandle>(wh);
    Check(OpusMemLock(h) == (void *)h, "CaseD2: OpusMemLock debe devolver el mismo puntero (FIXED)");
    Check(OpusMemSize(h) == 64, "CaseD2: OpusMemSize incorrecto");
    OpusMemUnlock(h);
    OpusMemFree(h);

    OpusMemSetPassthrough(nullptr);
}

/* ---- Caso E: conteo exacto de liberaciones, sin abortar (P7) ---- */
void CaseE_NoDoubleFreeAbort() {
    g_wineCounters = WineCounters();
    OpusMemSetPassthrough(&kOpsWine);

    OpusHandle h = OpusMemAlloc(16, OPUS_MEM_LOWER);
    Check(h != nullptr, "CaseE: alloc fallo");

    OpusMemFree(h);
    Check(g_wineCounters.free == 1, "CaseE: primer OpusMemFree debe producir exactamente 1 GlobalFree");
    Check((GlobalFlags(reinterpret_cast<HGLOBAL>(h)) & 0xFFFF) == GMEM_INVALID_HANDLE,
          "CaseE: bloque sigue vivo tras el primer free");

    /* Segundo OpusMemFree sobre handle ajeno ya liberado: el contrato
       decidido es "reenvia siempre" (diseno Sec4.2, P7) -- Wine no
       aborta en doble GlobalFree (medido). No se asevera "aborta": eso
       no ocurre y afirmarlo seria una asercion falsa (checklist-audit
       Sec4.2 hallazgo A). Se asevera lo que si es cierto: el proceso
       sigue vivo y el contador de free vuelve a moverse (reenvio, no
       supresion silenciosa). */
    OpusMemFree(h);
    Check(g_wineCounters.free == 2, "CaseE: segundo OpusMemFree debe reenviarse tambien (no suprimirse)");
    std::fprintf(stderr, "CaseE: OK, doble OpusMemFree ajeno no aborto (contrato P7)\n");

    OpusMemSetPassthrough(nullptr);
}

}  // namespace

int main() {
    CaseA_ForeignProducer();
    CaseB_OwnForeignAlloc(OPUS_MEM_DDESHARE, "CaseB(DDESHARE)");
    CaseB_OwnForeignAlloc(OPUS_MEM_LOWER, "CaseB(LOWER)");
    CaseC_NoOpsInstalled();
    CaseD1_GuardPage();
    CaseD2_SentinelFixedBlock();
    CaseE_NoDoubleFreeAbort();

    if (g_failures > 0) {
        std::fprintf(stderr, "%d fallo(s)\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "OK: todos los casos foreign pasaron\n");
    return 0;
}
