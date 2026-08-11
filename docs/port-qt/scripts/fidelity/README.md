# Sondas de fidelidad de métricas de texto (Fase Qt-1, §B2.3)

Programas de un solo uso que resolvieron empíricamente si la restricción de
fidelidad byte a byte es alcanzable con Qt. No forman parte del build.

| Archivo | Lado | Qué hace |
|---|---|---|
| `gdi_metrics.c` | oráculo | Reproduce la forma de `Opus/dispspec.c:787,796`: `GetTextExtentPoint32A(cch=1) - tmOverhang` para los 95 caracteres ASCII imprimibles |
| `gdi_synth.c` | oráculo | Mide `tmOverhang` en negrita y cursiva sintetizadas, y la sustitución de los nombres de fuente de época |
| `font_substitution.c` | oráculo | Resuelve los 4 nombres de época por defecto (`Tms Rmn`, `Symbol`, `Helv`, `Courier`) a familia real vía `GetTextFaceA`, para §B2.6 |
| `qt_metrics.cpp` | Qt | Avances en unidades de diseño más la aritmética de conversión, en variante con y sin redondeo previo del tamaño em |
| `qt_hint.cpp` | Qt | El mismo glifo bajo los cuatro modos de hinting, para aislar grid-fitting de aritmética |
| `qt_full.cpp` | Qt | Estrategia que rige: ppem entero más `QFont::PreferFullHinting` |
| `compare.sh` | — | Matriz 3 fuentes × N tamaños × 95 caracteres |
| `capture.py` | — | Regenera `src/core/src/opus_shell_font_metrics_oracle_table.h` (2026-08-11, ver `01-frontera-nucleo-shell.md` paso 5) -- a diferencia de las demás, esta sí alimenta un artefacto del build: la tabla de regresión de `opus_shell_font_metrics_fidelity_test` |

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

Medido con Wine 10.0 y Qt 6.8.2 sobre Debian trixie. La equivalencia se apoya
en que ambos lados rasterizan con FreeType, lo cual es la definición del
objetivo —la restricción del proyecto es fidelidad contra el oráculo Winelib,
no contra Windows real— y no una salvedad del resultado. Ver §B2.3 del
documento de frontera.
