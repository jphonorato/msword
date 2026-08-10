/*
 * Verificación de round-trip de handle real (Qt-2, B3, Fase 3).
 * docs/port-qt/01-frontera-nucleo-shell.md, sección de verificación B3.
 *
 * A diferencia de docs/port-qt/scripts/link-check/ (que probó paso de
 * valores por copia), esto cruza un handle/puntero real por la frontera:
 * pide el handle, lo fija, escribe un patrón conocido, lo suelta, lo
 * vuelve a fijar y confirma que el patrón sigue -- no solo que el
 * puntero es no nulo -- y por último confirma que el mal uso después de
 * liberar falla de forma controlada, no con corrupción silenciosa.
 *
 * Compilado y enlazado con wineg++ (el toolchain real de producción de
 * WORD1, no winegcc puro): la fuente sigue siendo C, como cualquier
 * archivo real de Opus/, pero el driver de enlace es el C++ que WORD1 ya
 * usa (target_compile_features(WORD1 PRIVATE cxx_std_20)), así que trae
 * libstdc++ por diseño sin el ajuste -lstdc++ manual que hizo falta con
 * winegcc puro en link-check/ -- confirmado ahí mismo para el caso de
 * configuración; se reconfirma acá para el caso de memoria.
 *
 * Dos modos:
 *   sin argumentos       -- round trip completo (alloc/lock/escribir/
 *                            unlock/lock/verificar/free/lock-tras-free).
 *   --double-free        -- solo alloc + free + free, para verificar que
 *                            la segunda liberación aborta de forma
 *                            controlada. Se invoca como proceso aparte
 *                            porque el resultado esperado es que el
 *                            proceso termine anormalmente (abort), no que
 *                            continúe.
 */

#include "OpusShellMemory.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define PATTERN "OpusMemHandleRoundTrip-2026"
#define PATTERN_LEN (sizeof(PATTERN))

static int RunRoundTrip(void) {
    OpusHandle h;
    char *p;
    OpusHandle hBack;

    /* 1) Alloc: obtener un handle desde el lado nativo. */
    h = OpusMemAlloc(PATTERN_LEN, 0);
    if (h == NULL) {
        fprintf(stderr, "OpusMemAlloc devolvió NULL\n");
        return 1;
    }
    printf("OpusMemAlloc: handle=%p, tamaño pedido=%zu\n", (void *)h,
           PATTERN_LEN);

    /* 2) Lock: puntero válido a partir del handle. */
    p = (char *)OpusMemLock(h);
    if (p == NULL) {
        fprintf(stderr, "OpusMemLock devolvió NULL sobre handle recién "
                        "asignado\n");
        return 1;
    }
    printf("OpusMemLock: puntero=%p\n", (void *)p);

    /* 3) Escribir un patrón conocido a través del puntero. */
    memcpy(p, PATTERN, PATTERN_LEN);
    printf("Escrito patrón de %zu bytes.\n", PATTERN_LEN);

    /* 4) Unlock, volver a hacer lock, confirmar que el patrón sigue. */
    OpusMemUnlock(h);
    p = NULL;
    p = (char *)OpusMemLock(h);
    if (p == NULL) {
        fprintf(stderr, "OpusMemLock (segunda vez) devolvió NULL\n");
        return 1;
    }
    if (memcmp(p, PATTERN, PATTERN_LEN) != 0) {
        fprintf(stderr,
                "EL PATRÓN NO SOBREVIVIÓ unlock+lock: leído \"%.*s\"\n",
                (int)PATTERN_LEN, p);
        return 1;
    }
    printf("Patrón intacto tras Unlock + Lock: \"%s\"\n", p);

    /* GlobalHandle equivalente: recuperar el handle a partir del puntero
       ya fijado, y confirmar que es el mismo handle. */
    hBack = OpusMemHandle(p);
    if (hBack != h) {
        fprintf(stderr, "OpusMemHandle devolvió %p, esperado %p\n",
                (void *)hBack, (void *)h);
        return 1;
    }
    printf("OpusMemHandle: recuperó el mismo handle a partir del puntero.\n");

    OpusMemUnlock(h);

    /* 5a) Free, luego Lock posterior: debe fallar controlado (NULL), no
       devolver un puntero colgante. */
    OpusMemFree(h);
    p = (char *)OpusMemLock(h);
    if (p != NULL) {
        fprintf(stderr,
                "FALLO DE CONTRATO: OpusMemLock tras Free devolvió %p, "
                "no NULL -- esto sería un puntero colgante silencioso.\n",
                (void *)p);
        return 1;
    }
    printf("OpusMemLock tras Free: NULL, como corresponde (fallo "
           "controlado, no puntero colgante).\n");

    printf("handle_check: ROUND-TRIP DE HANDLE REAL OK\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--double-free") == 0) {
        OpusHandle h = OpusMemAlloc(16, 0);
        if (h == NULL) {
            fprintf(stderr, "OpusMemAlloc devolvió NULL\n");
            return 1;
        }
        printf("Liberando handle %p una vez (correcto)...\n", (void *)h);
        OpusMemFree(h);
        printf("Liberando el mismo handle una segunda vez (debe abortar "
               "de forma controlada, no continuar)...\n");
        fflush(stdout);
        OpusMemFree(h); /* se espera que esto llame abort() */
        /* Si llegamos acá, el contrato falló: la doble liberación no fue
           detectada. */
        fprintf(stderr, "FALLO DE CONTRATO: la doble liberación no "
                        "abortó.\n");
        return 1;
    }
    return RunRoundTrip();
}
