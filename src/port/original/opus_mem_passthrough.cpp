/*
 * Tabla real de passthrough de OpusShellMemory hacia la API Win32 de Wine.
 *
 * Diseño: docs/superpowers/specs/2026-08-11-opus-memory-passthrough-design.md
 * §3.2 (punto de instalación), §4 (enrutado por flags) y §4.2 (invariante de
 * handle estable en Realloc); auditoría del checklist en
 * docs/superpowers/specs/2026-08-11-opus-memory-passthrough-checklist-audit.md
 * §3 (verificación del punto de instalación) y §4 (prueba de round-trip).
 *
 * Qué resuelve: src/core/ no incluye Win32 por construcción (frontera
 * núcleo/shell, docs/port-qt/01-frontera-nucleo-shell.md), así que
 * OpusShellMemory.cpp no puede llamar GlobalAlloc/GlobalLock/... por sí
 * mismo. Un bloque marcado OPUS_MEM_FOREIGN -- compartible entre procesos
 * (DDE), en memoria "baja" (hook de journal-playback), o que simplemente
 * sale del contrato hacia una DLL Win16 o el portapapeles del sistema --
 * no tiene equivalente en el heap privado de malloc del contrato: tiene
 * que ser un HGLOBAL real. El contrato delega esos bloques en esta tabla;
 * sin ella instalada falla controladamente (NULL) en vez de degradar a
 * malloc, que es el modo de fallo silencioso que el mecanismo existe para
 * evitar (diseño §6, riesgo 2).
 *
 * Este es el gemelo de producción de kOpsWine en
 * src/port/tests/opus_shell_memory_foreign_test.cpp (que además lleva
 * contadores de instrumentación que aquí no hacen falta).
 *
 * El guard __GNUC__/!_MSC_VER es redundante con la condición
 * OPUS_WINELIB_BUILD que envuelve este archivo en src/CMakeLists.txt, pero
 * se conserva por la misma razón que en catalog.c/elsubs2.c: bajo MSVC
 * opus_shell_memory ni siquiera se declara como target, así que cualquier
 * referencia a OpusMemSetPassthrough sería un símbolo sin resolver.
 */

#if defined(__GNUC__) && !defined(_MSC_VER)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

#include "OpusShellMemory.h"

namespace {

/* Reconstrucción de los GMEM_* reales a partir de los bits ya reducidos
 * que entrega el contrato. Valores verificados contra los headers de Wine
 * de esta máquina, no de memoria
 * (/usr/include/wine/wine/windows/winbase.h:473-487):
 *
 *   GMEM_MOVEABLE   0x0002
 *   GMEM_ZEROINIT   0x0040
 *   GMEM_NOT_BANKED 0x1000
 *   GMEM_LOWER      = GMEM_NOT_BANKED
 *   GMEM_SHARE      0x2000
 *   GMEM_DDESHARE   0x2000   (mismo valor que GMEM_SHARE)
 *
 * Por eso OPUS_MEM_DDESHARE -> GMEM_DDESHARE cubre también GMEM_SHARE, y
 * OPUS_MEM_LOWER -> GMEM_LOWER cubre también GMEM_NOT_BANKED: en Win32 (y
 * ya en Win16) son alias, no dos bits distintos. Se escriben las macros de
 * Wine, no literales, para que el día que cambien se rompa la compilación
 * en vez del comportamiento.
 *
 * GMEM_MOVEABLE siempre presente: es lo que sostiene la invariante de
 * handle estable del diseño §4.2 -- sin él GlobalReAlloc falla siempre
 * (err=8, medido en la auditoría §2.3), nunca reubica en silencio -- y es
 * el flag que ya llevan todos los sitios censados (GMEM_SENDKEYS =
 * GMEM_LOWER|GMEM_MOVEABLE, gmemLibShare = GMEM_SHARE|GMEM_MOVEABLE,
 * GMEM_MOVEABLE plano).
 *
 * OPUS_MEM_ESCAPES no aporta ningún bit Win16: por definición marca
 * bloques cuyos flags originales son planos (GMEM_MOVEABLE/GHND) pero cuyo
 * handle cruza la frontera del contrato. GMEM_MOVEABLE, que ya se añade
 * siempre, es exactamente lo que pedían esos sitios: spelcore.c:705,
 * etcmd.c:1157 y etcmd.c:1317 sustituyen literalmente un
 * `OurGlobalAlloc(GMEM_MOVEABLE, cch)` (la rama #else de cada uno lo
 * conserva a la vista). Que el handle sea un HGLOBAL de verdad no es
 * cosmético ahí: SPELL.C:1297 (FreeGhd) libera ese mismo handle con un
 * GlobalFree crudo de Wine, sin pasar por el contrato. Con esta tabla
 * instalada eso funciona; sin ella OpusMemAlloc devolvería NULL y esos
 * sitios se irían por su rama de falta de memoria.
 *
 * OPUS_MEM_ZEROINIT: se traduce aquí, pero hoy NO PUEDE llegar --
 * OpusShellMemory.cpp reduce los flags con `flags & OPUS_MEM_FOREIGN`
 * antes de llamar Alloc/Realloc (src/core/src/OpusShellMemory.cpp:99,192)
 * y ZEROINIT no está en esa máscara. La traducción está escrita para que
 * la tabla sea correcta por sí misma y no dependa de esa decisión del
 * núcleo.
 *
 * Y ya hay un sitio que combina ambos, contra lo que suponía el diseño:
 * Opus/raremsg.c:1382 pide
 * `OpusMemFlagsFromWin16(GHND) | OPUS_MEM_ESCAPES`, es decir
 * OPUS_MEM_ZEROINIT|OPUS_MEM_ESCAPES, reproduciendo el GHND del
 * GlobalAlloc original. Por la máscara del núcleo, este bloque de
 * portapapeles se asigna sin GMEM_ZEROINIT: el passthrough NO puede
 * arreglarlo desde aquí. Es inocuo en ese sitio concreto -- verificado
 * leyéndolo: el bucle de copia llena los cch bytes y escribe el
 * terminador a mano (`*lpch = 0`, raremsg.c:1408), así que no depende del
 * relleno a cero -- pero es una divergencia real respecto del original y
 * el próximo sitio foreign+GHND que sí dependa del cero fallará en
 * silencio. Arreglarlo es una línea en el contrato (ampliar la máscara a
 * OPUS_MEM_FOREIGN|OPUS_MEM_ZEROINIT), no aquí; queda reportado, no
 * parcheado unilateralmente. Zeroinicializar todo bloque foreign desde
 * esta tabla sería taparlo, no arreglarlo. */
unsigned ReconstructWin16Flags(unsigned reduced) {
    unsigned flags = GMEM_MOVEABLE;
    if (reduced & OPUS_MEM_DDESHARE) {
        flags |= GMEM_DDESHARE;  /* == GMEM_SHARE */
    }
    if (reduced & OPUS_MEM_LOWER) {
        flags |= GMEM_LOWER;     /* == GMEM_NOT_BANKED */
    }
    if (reduced & OPUS_MEM_ZEROINIT) {
        flags |= GMEM_ZEROINIT;
    }
    return flags;
}

/* cb se pasa tal cual, sin el `cb ? cb : 1` del camino propio del
   contrato: GlobalAlloc(flags, 0) es legal en Win32 igual que lo era en
   Win16, y el passthrough tiene que ser fiel a lo que el sitio original
   pedía, no "corregirlo". */
OpusHandle WineMemAlloc(unsigned long cb, unsigned win16Flags) {
    HGLOBAL h = GlobalAlloc(ReconstructWin16Flags(win16Flags),
                            static_cast<SIZE_T>(cb));
    return reinterpret_cast<OpusHandle>(h);
}

void *WineMemLock(OpusHandle h) {
    return GlobalLock(reinterpret_cast<HGLOBAL>(h));
}

void WineMemUnlock(OpusHandle h) {
    GlobalUnlock(reinterpret_cast<HGLOBAL>(h));
}

/* Invariante del diseño §4.2: esta función nunca devuelve un OpusHandle
   distinto del que recibió. Bajo Wine con GMEM_MOVEABLE se cumple de
   hecho -- medido en la auditoría §2.3 (Wine 10.0): con 0x0002, 0x0042,
   0x2002 (GMEM_DDE) y 0x1002 (GMEM_SENDKEYS) el HGLOBAL es estable
   incluso creciendo de 8 B a 1 MB, y en un estrés de 400 reallocs con 256
   handles vivos cambió 0 veces; sin GMEM_MOVEABLE la llamada directamente
   falla (err=8), no reubica. Pero eso es una propiedad de implementación
   de Wine, no de la API, así que aquí se convierte en garantía explícita:
   de los 18 sitios de realloc en código compilado (censo corregido,
   auditoría §2.1 -- el diseño §9.2 solo lista 9), la mayoría descarta el
   valor de retorno y sigue usando su handle original, y dos
   (FCopySzToGhsz/FCopyStToGhsz, filecvt.c) reciben el handle por valor y
   devuelven BOOL, sin forma sintáctica de propagar uno nuevo. Los 5
   sitios que sí reasignan (CLIPBRD2.C:444,2076, DDESRVR.C:1006,
   rtfout2.c:897, auditoría §2.2) hacen rollback al handle original cuando
   el retorno es NULL, así que también quedan correctos bajo esta regla.

   Por eso el handle reubicado se rechaza (NULL) en vez de propagarse: NULL
   es un fallo que todos los llamadores ya comprueban. El bloque nuevo no
   se libera aquí a propósito -- el original ya no existe tras un
   GlobalReAlloc exitoso y liberar el nuevo destruiría datos vivos; se
   prefiere una fuga ruidosa y diagnosticada a una corrupción silenciosa.
   Rama inalcanzable con la semántica documentada de Win32 para bloques
   moveables; el fprintf existe para que, si alguna vez se alcanza, se vea. */
OpusHandle WineMemRealloc(OpusHandle h, unsigned long cb,
                          unsigned win16Flags) {
    HGLOBAL previous = reinterpret_cast<HGLOBAL>(h);
    HGLOBAL current = GlobalReAlloc(previous, static_cast<SIZE_T>(cb),
                                    ReconstructWin16Flags(win16Flags));
    if (current == nullptr) {
        return nullptr;  /* fallo real de Wine; el llamador conserva h */
    }
    if (current != previous) {
        std::fprintf(stderr,
                     "OpusMemRealloc (passthrough): GlobalReAlloc reubico el "
                     "handle (%p -> %p); se rechaza para no violar la "
                     "invariante de handle estable\n",
                     static_cast<void *>(previous),
                     static_cast<void *>(current));
        return nullptr;
    }
    return h;
}

unsigned long WineMemSize(OpusHandle h) {
    return static_cast<unsigned long>(GlobalSize(reinterpret_cast<HGLOBAL>(h)));
}

/* Sin emulación de "abortar en doble free", a diferencia del camino propio
   del contrato: medido (auditoría §4.2, hallazgo A) que una segunda
   GlobalFree bajo Wine 10.0 no aborta -- devuelve el propio handle y el
   proceso sigue vivo. Reenviar siempre es el contrato del camino foreign
   (diseño §4.2, P7); es además exactamente lo que hacía el código
   pre-migración, que llamaba GlobalFree directo. */
void WineMemFree(OpusHandle h) {
    GlobalFree(reinterpret_cast<HGLOBAL>(h));
}

/* Duración estática: el contrato no toma ownership de la tabla (ver
   OpusMemSetPassthrough en OpusShellMemory.h), el shell debe mantenerla
   viva mientras esté instalada. Un const global la mantiene viva durante
   todo el proceso y sin inicialización dinámica -- importante porque la
   demostración de que nada corre antes de wWinMain (auditoría §3.2) se
   apoya en que ninguna TU del grafo de WORD1 añada .init_array nuevo. */
const OpusMemPassthroughOps kWineMemOps = {
    WineMemAlloc, WineMemLock,  WineMemUnlock,
    WineMemRealloc, WineMemSize, WineMemFree,
};

}  // namespace

/* Llamado una sola vez desde wWinMain (src/port/original/
   opus_original_startup_probe.cpp), antes de que se ejecute cualquier
   línea de Opus/. Idempotente: reinstalar la misma tabla es un store a un
   puntero global. */
extern "C" void OpusInstallMemPassthrough(void) {
    OpusMemSetPassthrough(&kWineMemOps);
}

#endif  /* __GNUC__ && !_MSC_VER */
