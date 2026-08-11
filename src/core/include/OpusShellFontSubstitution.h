/* src/core/include/OpusShellFontSubstitution.h
 *
 * Contrato de sustitución de fuentes de época entre el núcleo Qt y el
 * shell. Diseño: docs/port-qt/01-frontera-nucleo-shell.md, §B2.6.
 *
 * Cubre únicamente los 4 nombres que Opus/initwin.c carga en la tabla
 * maestra de fuentes al arrancar (Tms Rmn, Symbol, Helv, Courier). La ruta
 * devuelta es fija, medida contra el oráculo Winelib -- no se recalcula en
 * tiempo de ejecución ni depende de qué fuentes estén instaladas en la
 * máquina que corre el shell. Solo resuelve el peso regular: negrita y
 * cursiva se sintetizan sobre el mismo archivo (§B2.5: tmOverhang = 0
 * medido en los 8 casos de estilo bajo TrueType/Wine).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Devuelve la ruta absoluta al archivo de fuente física que sustituye al
 * nombre de época dado, o NULL si eraName es NULL o no es uno de los 4
 * nombres cubiertos (Tms Rmn, Symbol, Helv, Courier). El puntero devuelto
 * es propiedad de la biblioteca (cadena estática) -- el caller no lo libera
 * ni lo modifica.
 */
const char *OpusShellSubstituteFontFile(const char *eraName);

#ifdef __cplusplus
}
#endif
