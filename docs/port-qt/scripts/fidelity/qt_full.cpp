#include <QGuiApplication>
#include <QRawFont>
#include <QFile>
#include <cstdio>
#include <cmath>
int main(int argc, char **argv){
    QGuiApplication app(argc, argv);
    QFile f(argv[1]); f.open(QIODevice::ReadOnly);
    QByteArray d=f.readAll();
    int pt=atoi(argv[2]), dpi=atoi(argv[3]);
    int px=(int)((double)pt*dpi/72.0+0.5);
    QRawFont rf(d,(qreal)px,QFont::PreferFullHinting);
    for(int ch=32;ch<127;ch++){
        QString s=QString(QChar(ch)); quint32 i; int n=1;
        if(!rf.glyphIndexesForChars(s.data(),1,&i,&n)) continue;
        QPointF a; rf.advancesForGlyphIndexes(&i,&a,1);
        printf("%d %d\n",ch,(int)std::floor(a.x()+0.5));
    }
    return 0;
}
