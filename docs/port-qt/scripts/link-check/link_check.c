/*
 * Verificación de enlace cross-toolchain (Qt-2, previo a B3).
 * docs/port-qt/01-frontera-nucleo-shell.md, sección de verificación de
 * frontera física.
 *
 * Programa mínimo en C, compilado con el MISMO compilador que usa
 * opus_original_engine para sus fuentes .c (winegcc), que llama a una
 * función real ya implementada del contrato de configuración
 * (OpusShellProfileString, src/core/src/OpusShellConfig.cpp) y enlaza
 * contra la biblioteca nativa opus_shell_config (gcc/g++ nativo + Qt6).
 *
 * No es parte del build de opus_original_engine ni de ningún target de
 * CMake: es una sonda de un solo uso, igual que
 * docs/port-qt/scripts/fidelity/.
 */

#include "OpusShellConfig.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    char out[64];
    int cch;
    int wrote;

    wrote = OpusShellProfileWrite("LinkCheck", "Saludo", "hola desde winegcc");
    if (wrote != 0) {
        fprintf(stderr, "OpusShellProfileWrite devolvió error: %d\n", wrote);
        return 1;
    }

    cch = OpusShellProfileString("LinkCheck", "Saludo", "", out, sizeof(out));
    printf("OpusShellProfileString devolvió cch=%d valor=\"%s\"\n", cch, out);

    if (strcmp(out, "hola desde winegcc") != 0) {
        fprintf(stderr, "Valor inesperado.\n");
        return 1;
    }

    printf("link_check: ENLACE Y LLAMADA REAL OK\n");
    return 0;
}
