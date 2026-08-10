/* Lado Qt: obtiene avances en unidades de diseño vía QRawFont y aplica la
   aritmética de redondeo que propone B2, en dos variantes:
     (a) escala con el tamaño em sin redondear: dpi*pt/72
     (b) escala con el tamaño em ya redondeado a entero, que es lo que GDI
         seleccionó realmente (MulDiv(pt, dpi, 72))
   Imprime ambas para poder comparar contra la salida de GDI. */
#include <QGuiApplication>
#include <QRawFont>
#include <QFile>
#include <QVector>
#include <cstdio>
#include <cmath>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QString path = argc > 1 ? argv[1] : "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf";
    int pt  = argc > 2 ? atoi(argv[2]) : 10;
    int dpi = argc > 3 ? atoi(argv[3]) : 96;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { printf("ERR open\n"); return 1; }
    QByteArray data = f.readAll();

    /* Sin hinting: el escalado por unidades de diseño supone avances lineales. */
    QRawFont probe(data, 1000.0, QFont::PreferNoHinting);
    if (!probe.isValid()) { printf("ERR invalid\n"); return 1; }
    qreal upem = probe.unitsPerEm();

    /* pixelSize = unitsPerEm  =>  los avances salen en unidades de diseño. */
    QRawFont du(data, upem, QFont::PreferNoHinting);

    double emUnrounded = (double)dpi * pt / 72.0;
    int    emRounded   = (int)((double)pt * dpi / 72.0 + 0.5);

    printf("QT  file=%s pt=%d dpi=%d upem=%.0f emUnrounded=%.4f emRounded=%d\n",
           qPrintable(path), pt, dpi, upem, emUnrounded, emRounded);

    for (int ch = 32; ch < 127; ch++) {
        QString s = QString(QChar(ch));
        QVector<quint32> idx(1);
        int n = 1;
        if (!du.glyphIndexesForChars(s.data(), 1, idx.data(), &n)) continue;
        QVector<QPointF> adv(1);
        if (!du.advancesForGlyphIndexes(idx.data(), adv.data(), 1)) continue;
        double designAdv = adv[0].x();
        double a = designAdv / upem * emUnrounded;
        double b = designAdv / upem * emRounded;
        printf("QT  adv %d design=%.2f a=%d b=%d\n", ch, designAdv,
               (int)std::floor(a + 0.5), (int)std::floor(b + 0.5));
    }
    return 0;
}
