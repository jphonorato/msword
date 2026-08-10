# Sondas de fidelidad de métricas de texto (Fase Qt-1, §B2.3)

Programas de un solo uso que resolvieron empíricamente si la restricción de
fidelidad byte a byte es alcanzable con Qt. No forman parte del build.

| Archivo | Lado | Qué hace |
|---|---|---|
| `gdi_metrics.c` | oráculo | Reproduce la forma de `Opus/dispspec.c:787,796`: `GetTextExtentPoint32A(cch=1) - tmOverhang` para los 95 caracteres ASCII imprimibles |
| `gdi_synth.c` | oráculo | Mide `tmOverhang` en negrita y cursiva sintetizadas, y la sustitución de los nombres de fuente de época |
| `qt_metrics.cpp` | Qt | Avances en unidades de diseño más la aritmética de conversión, en variante con y sin redondeo previo del tamaño em |
| `qt_hint.cpp` | Qt | El mismo glifo bajo los cuatro modos de hinting, para aislar grid-fitting de aritmética |
| `qt_full.cpp` | Qt | Estrategia que rige: ppem entero más `QFont::PreferFullHinting` |
| `compare.sh` | — | Matriz 3 fuentes × N tamaños × 95 caracteres |

Construcción:

```sh
winegcc -o gdi_metrics.exe gdi_metrics.c -lgdi32 -luser32
g++ -fPIC -o qt_full qt_full.cpp $(pkg-config --cflags --libs Qt6Gui Qt6Core)
```

Ejecución (`qt_*` necesita `QT_QPA_PLATFORM=offscreen` sin servidor gráfico):

```sh
WINEDEBUG=-all ./gdi_metrics.exe "Liberation Serif" 14
QT_QPA_PLATFORM=offscreen ./qt_full /usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf 14 96
```

Medido con Wine 10.0 y Qt 6.8.2 sobre Debian trixie. El resultado depende de
que ambos lados rastericen con FreeType; ver la advertencia de §B2.3 sobre no
extrapolar a la GDI de Microsoft.
