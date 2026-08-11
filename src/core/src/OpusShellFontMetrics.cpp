/*
 * Implementación del contrato de medición de texto
 * (src/core/include/OpusShellFontMetrics.h, docs/port-qt/
 * 01-frontera-nucleo-shell.md §B2). Estrategia validada empíricamente en
 * §B2.3: ppem entero + QFont::PreferFullHinting sobre QRawFont construido
 * directamente contra el archivo físico -- no QFontMetricsF, no QFont
 * (ver §B2.4 para por qué esas dos quedan descartadas). Reproduce, no
 * aproxima, los enteros que produce Opus/LOADFONT.C:187 C_LoadFcid() bajo
 * el oráculo Winelib (mismo rasterizador FreeType a ambos lados).
 *
 * Limitaciones de este primer corte, explícitas, no silenciosas:
 *
 * 1. **`ftc` -> nombre de época: tabla fija de 4 entradas, hardcodeada
 *    aquí.** El contrato declara que "el núcleo traduce" (§B2.2), pero
 *    hoy no hay ningún llamador real en Opus/ -- este primer corte asume
 *    la tabla de arranque verificada (Opus/initwin.c:1541-1583,
 *    vhsttbFont, orden Tms Rmn/Symbol/Helv/Courier = ftc 0-3) porque es
 *    la única que existe y es la misma que ya cubre
 *    OpusShellFontSubstitution. `ftc` fuera de [0,3] falla controlado.
 *    Ver 01-frontera-nucleo-shell.md, pregunta abierta #3, último párrafo,
 *    para la decisión pendiente sobre dónde debe vivir esta tabla a
 *    largo plazo.
 * 2. **`catr` (negrita/cursiva) no soportado -- falla controlado, no se
 *    ignora en silencio.** QRawFont rasteriza un archivo físico tal cual;
 *    no sintetiza negrita/cursiva como hace GDI sobre estas fuentes
 *    (§B2.5: "negrita y cursiva se sintetizan sobre el mismo archivo").
 *    Reproducir esa síntesis (transformación oblicua para cursiva,
 *    engrosado de trazo para negrita) es trabajo de fidelidad aparte, no
 *    hecho aquí. `catr != 0` devuelve error en vez de devolver métricas
 *    de peso regular calladamente.
 * 3. **Resolución de pantalla fija a 96 ppp.** Es la resolución con la
 *    que se hizo toda la medición de §B2.3/§B2.6. El contrato declara
 *    dxpInch/dypInch como salida (el shell decide, el núcleo no pide un
 *    DPI concreto) -- soporte de impresora, con su propio DPI real, es
 *    trabajo futuro, no de este corte.
 * 4. **`dxpOverhang` fijo a 0.** Medido en §B2.5 para los 8 casos de
 *    estilo probados (los 4 nombres × negrita/cursiva sintetizada):
 *    tmOverhang = 0 en todos. Coherente con la limitación 2 -- sin
 *    síntesis de estilo, el caso que podría producir overhang != 0 no se
 *    alcanza de todas formas.
 */

#include "OpusShellFontMetrics.h"

#include "OpusShellFontSubstitution.h"

#include <QFont>
#include <QRawFont>
#include <QString>

namespace {

constexpr int kScreenDpi = 96;

const char *EraNameFromFtc(int ftc) {
    switch (ftc) {
        case 0: return "Tms Rmn";
        case 1: return "Symbol";
        case 2: return "Helv";
        case 3: return "Courier";
        default: return nullptr;
    }
}

/* MulDiv de Win32: round(a*b/c), redondeo al entero más cercano. Mismo
   cálculo que Opus/LOADFONT.C usa para pasar de puntos a píxeles
   (lfHeight = -MulDiv(pt, dpiY, 72), §B2.3). Sin la protección de
   overflow de 64 bits del MulDiv real: los rangos de este contrato (ps
   en medios puntos, dpi de pantalla) nunca se acercan a desbordar un
   long, no hace falta reproducirla. */
int MulDivRound(int a, int b, int c) {
    return static_cast<int>(
        (static_cast<long long>(a) * b + c / 2) / c);
}

/* Construye el QRawFont validado en §B2.3/§B2.4 para (ftc, ps). Devuelve
   un QRawFont inválido (isValid() == false) si ftc/catr no están
   soportados o el archivo de sustitución no existe -- el llamador
   traduce eso a "falla controlado", no lo desreferencia. */
QRawFont RawFontFor(const OpusFontKey *key, int *pxOut) {
    QRawFont invalid;
    if (key == nullptr || key->catr != 0) {
        return invalid;  /* limitación 2 */
    }
    const char *eraName = EraNameFromFtc(key->ftc);
    if (eraName == nullptr) {
        return invalid;  /* limitación 1 */
    }
    const char *file = OpusShellSubstituteFontFile(eraName);
    if (file == nullptr) {
        return invalid;
    }
    int px = MulDivRound(key->ps / 2, kScreenDpi, 72);
    if (px <= 0) {
        return invalid;
    }
    if (pxOut != nullptr) {
        *pxOut = px;
    }
    return QRawFont(QString::fromUtf8(file), static_cast<qreal>(px),
                     QFont::PreferFullHinting);
}

}  // namespace

extern "C" int OpusShellFontMetrics(const OpusFontKey *key,
                                     OpusFontMetrics *out) {
    if (out == nullptr) {
        return -1;
    }
    QRawFont rf = RawFontFor(key, nullptr);
    if (!rf.isValid()) {
        return -1;
    }

    out->dypAscent = qRound(rf.ascent());
    out->dypDescent = qRound(rf.descent());
    out->dxpOverhang = 0;             /* limitación 4 */
    out->dxpInch = kScreenDpi;
    out->dypInch = kScreenDpi;
    /* Único de los 4 nombres de época con paso fijo, verificado contra
       Opus/initwin.c:1578 (FF_MODERN|FIXED_PITCH, único prq=FIXED_PITCH
       de la tabla) -- no se deriva de QRawFont (no expone paso fijo
       directamente sin medir dos anchos y compararlos, y para estos 4
       nombres ya se sabe la respuesta sin medir). */
    out->dxuFixed = (key->ftc == 3) ? 1 : 0;
    return 0;
}

extern "C" int OpusShellCharWidths(const OpusFontKey *key, int chFirst,
                                    int cch, unsigned short *rgdxu) {
    if (rgdxu == nullptr || chFirst < 0 || cch <= 0 ||
        chFirst + cch > 256) {
        return -1;
    }
    QRawFont rf = RawFontFor(key, nullptr);
    if (!rf.isValid()) {
        return -1;
    }

    for (int i = 0; i < cch; ++i) {
        /* QChar::fromLatin1: los 256 códigos de FCE.hqrgdxp son valores
           de byte crudos (ANSI_CHARSET para los 4 nombres cubiertos,
           Opus/initwin.c:1547 etc.) -- Latin-1 es el mapeo correcto para
           0-255, no UTF-8 multibyte. */
        QChar ch = QChar::fromLatin1(static_cast<char>(chFirst + i));
        QVector<quint32> glyphs = rf.glyphIndexesForString(QString(ch));
        if (glyphs.isEmpty() || glyphs.first() == 0) {
            /* Sin glifo para este byte en esta fuente: ancho 0, igual
               que OurGetCharWidth devuelve 0 cuando GetTextExtentPoint32A
               falla (LOADFONT.C:990). No es un error del contrato. */
            rgdxu[i] = 0;
            continue;
        }
        QVector<QPointF> advances = rf.advancesForGlyphIndexes(glyphs);
        qreal advance = advances.isEmpty() ? 0.0 : advances.first().x();
        rgdxu[i] = static_cast<unsigned short>(qRound(advance));
    }
    return 0;
}
