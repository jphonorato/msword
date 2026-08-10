#pragma once

/*
 * Contrato de medición de texto entre el núcleo Qt y el shell.
 * Diseño: docs/port-qt/01-frontera-nucleo-shell.md, §B2.
 *
 * El núcleo nunca adquiere un HDC ni selecciona fuentes: pide la tabla de
 * avances una vez por fuente y hace su propia aritmética entera sobre
 * struct FTI (Opus/wordtech/format.h), exactamente como hoy. El shell es
 * responsable de reproducir el redondeo entero de GDI bajo el oráculo
 * Winelib -- estrategia validada empíricamente en §B2.3: ppem entero más
 * QFont::PreferFullHinting sobre QRawFont. Ninguna de las dos funciones de
 * este contrato expone un tipo de Qt ni de Win32.
 *
 * Solo declaraciones. Sin implementación en este header.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Identidad de fuente tal como el núcleo la conoce: los mismos campos que
 * FTI ya guarda (Opus/wordtech/format.h:379-410).
 */
typedef struct OpusFontKey {
    int ftc;    /* código de tipografía */
    int ps;     /* tamaño en medios puntos, como en FTI */
    int catr;   /* atributos: negrita, cursiva, ... */
} OpusFontKey;

typedef struct OpusFontMetrics {
    int dypAscent;
    int dypDescent;
    int dxpOverhang;    /* equivalente de TEXTMETRIC.tmOverhang */
    int dxpInch;        /* resolución del dispositivo destino */
    int dypInch;
    int dxuFixed;        /* != 0 si la fuente es de paso fijo */
} OpusFontMetrics;

/*
 * Métricas de fuente agregadas (ascenso, descenso, overhang, resolución).
 * Devuelve 0 en éxito. El shell no conoce struct FTI; el núcleo traduce.
 */
int OpusShellFontMetrics(const OpusFontKey *key, OpusFontMetrics *out);

/*
 * Rellena rgdxu[0..cch-1] con el avance entero de cada carácter del rango
 * [chFirst, chFirst + cch), en las mismas unidades que la tabla apuntada
 * por FTI.bmpchdxu. Devuelve 0 en éxito.
 */
int OpusShellCharWidths(const OpusFontKey *key, int chFirst, int cch,
                         unsigned short *rgdxu);

#ifdef __cplusplus
}
#endif
