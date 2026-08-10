/* ¿La discrepancia es aritmética o de grid-fitting? Pide a Qt el avance
   directamente al tamaño en píxeles que GDI seleccionó, bajo cada modo de
   hinting, en lugar de escalar unidades de diseño. */
#include <QGuiApplication>
#include <QRawFont>
#include <QFile>
#include <cstdio>
#include <cmath>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QString path = argv[1];
    int pt = atoi(argv[2]), dpi = atoi(argv[3]);
    int px = (int)((double)pt * dpi / 72.0 + 0.5);
    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();

    struct { const char *name; QFont::HintingPreference h; } modes[] = {
        {"NoHinting",       QFont::PreferNoHinting},
        {"VerticalHinting", QFont::PreferVerticalHinting},
        {"FullHinting",     QFont::PreferFullHinting},
        {"Default",         QFont::PreferDefaultHinting},
    };
    printf("px=%d\n", px);
    for (auto &m : modes) {
        QRawFont rf(data, (qreal)px, m.h);
        printf("%-16s", m.name);
        for (int ch : {64, 65, 87, 105, 109}) {
            QString s = QString(QChar(ch));
            quint32 idx; int n = 1;
            rf.glyphIndexesForChars(s.data(), 1, &idx, &n);
            QPointF adv;
            rf.advancesForGlyphIndexes(&idx, &adv, 1);
            printf("  %c=%.3f(%d)", ch, adv.x(), (int)std::floor(adv.x() + 0.5));
        }
        printf("\n");
    }
    return 0;
}
