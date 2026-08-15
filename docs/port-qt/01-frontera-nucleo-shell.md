# Fase Qt-1 — Diseño de la frontera núcleo/shell

**Estado:** diseño cerrado; implementación Qt-2 en curso desde 2026-08-11/12.
La línea original de este campo ("diseño, sin implementar") describía la fase
en que se abrió el documento; se corrige aquí porque el resto del documento
(§B3.5, §B4.4, §B5.1/§B5.2) ya registra implementación real que esa línea
contradecía. Ver "Estado real de hoy" abajo antes que nada.
**Insumo:** `docs/port-qt/00-inventario-win32.md`, en su versión posterior a la
exclusión de comentarios y literales.
**Decisiones de alcance cerradas:** fidelidad de paginación idéntica byte a
byte contra el oráculo Winelib; `Opus/interp/` es núcleo; `OpusEtAl/` por
veredicto individual (54 excluir, 4 diferir); `Opus/debug/` se porta.
**API de frontera:** cuatro headers en `src/core/include/`, los cuatro con
implementación real hoy (ninguno quedó en "solo declaración"):
`OpusShellConfig.h`/`OpusShellMemory.h` (§B5/§B3, verificadas con enlace
cross-toolchain real y call sites migrados en `Opus/`), `OpusShellFontMetrics.h`
(§B2, verificada con 2660 puntos de fidelidad), `OpusShellSpine.h` (§B4.4,
solo los dos fragmentos con firma concreta — `OpusShellReportError`/
`OpusShellAlert` —, no la inversión completa del bucle de mensajes, que sigue
sin empezar).

---

## Estado real de hoy, para quien no va a leer 1900 líneas

Añadido 2026-08-14 a partir de leer el código, no este documento — la
pregunta que lo motivó fue "¿esto es un port a Qt de verdad, o son pruebas de
humo?". Respuesta corta, en dos ejes que no se pueden fundir en un solo
veredicto: como dependencia de runtime que `WORD1` ejecuta de verdad en
cada arranque, sí — ya es Word sobre Qt (ver más abajo). Como arquitectura
—quién es dueño de la ventana y el bucle de eventos—, no todavía: eso
sigue siendo Win32/Wine de punta a punta. Y las pruebas: las de `src/core`
no son de humo, las de arranque/interacción de `WORD1` (etiqueta
`word1_startup_blocked`) sí lo son. Detalle verificable:

### Lo que sí es real y está enlazado en el `WORD1` que se distribuye

No son solo headers ni una biblioteca sin usar. `src/CMakeLists.txt` enlaza
las `.a` de `src/core` directamente en el target `WORD1` (líneas ~214-218,
280+), con call sites reales y comprometidos dentro de `Opus/` (árbol
restringido) que las llaman:

| Contrato | Backend Qt real | Call sites en `Opus/` | Commit |
|---|---|---|---|
| `OpusShellConfig` (§B5) | `QSettings` | 41, `OpusShellProfile*` en 12 archivos (p.ej. `quit.c`) | `e298420` |
| `OpusShellMemory` (§B3) | malloc/realloc/free con contador de fijación | 3 en `catalog.c` (`HGrabFarMem`, `FAllocDMFarMem`, `FreeDMFarMem`); ~198 sitios `Global*` restantes sin migrar | `c4e9ff0`, `2a36b1a` |
| `OpusShellSpine` (§B4.4) | `QMessageBox` / `QApplication::beep()` | 1, `wordtech/error.c:1630`; `editspec.c`/`undo.c` (`OpusShellAlert`) sin conectar | `ea5f908` |
| `OpusShellFontMetrics`/`FontSubstitution` (§B2) | `QRawFont` | 1, `Opus/LOADFONT.C:187 C_LoadFcid`; sin verificación en ejecución contra `WORD1` real (§B2.7) | — |

Las pruebas de `src/core` tampoco son de humo: `OpusShellConfig_test.cpp`
verifica semántica documentada del Profile Win16 punto por punto (no solo
"compila y sale 0"), y `OpusShellFontMetrics_fidelity_test.cpp` compara contra
2660 mediciones capturadas del oráculo Winelib real (§B2.3) — 2660/2660
coinciden.

### Corregido el mismo día: "Word sobre Qt" es cierto en el eje de dependencia de runtime

La primera versión de esta sección verificó código fuente (call sites,
CMake) pero no el binario resultante, y de ahí sacó una conclusión
demasiado plana. Verificado con `ldd bin/WORD1.exe.so`:

```
libQt6Widgets.so.6 => /usr/lib/libQt6Widgets.so.6
libQt6Gui.so.6     => /usr/lib/libQt6Gui.so.6
libQt6Core.so.6    => /usr/lib/libQt6Core.so.6
libQt6DBus.so.6    => /usr/lib/libQt6DBus.so.6
```

No es un detalle cosmético: `WORD1.exe.so` no arranca sin estas
bibliotecas presentes y en la versión correcta — el gotcha ya documentado
en `CLAUDE.md` (Qt 6.11.1 del host vs 6.8.2 del contenedor Debian 13,
`dlopen` fallando con `version 'Qt_6.11' not found`, enmascarado como un
`ShellExecuteEx failed: File not found` engañoso) es la prueba de que esta
dependencia es real y se resuelve en cada arranque, no un artefacto de
prueba. En ese sentido concreto — Qt6 como dependencia de runtime que el
binario ejecuta de verdad, tanto en el camino de arranque como en las
llamadas de la tabla de arriba — **"esto es Word sobre Qt" es correcto, y
la conclusión original de esta sección ("ninguno de los dos es Word
corriendo sobre Qt todavía") sobregeneralizaba.**

### El eje que sigue sin cerrarse: quién controla la ventana y el bucle de eventos

Esto es un eje distinto y no debe colapsarse con el anterior. `WORD1.exe.so`
sigue corriendo el bucle de mensajes Win32 completo (`GetMessage`/
`DispatchMessage` vía Winelib) de punta a punta; ningún `QWidget` es parte
de su interfaz visible; los cuatro contratos son puramente de backend
(persistencia, memoria, medición de texto, un diálogo modal disparado
desde dentro de ese bucle Win32, no al revés). Es el propio código el que
lo dice, no una inferencia de este documento (`src/core/src/
OpusShellSpine.cpp`):

> "WORD1 real corre hoy con el loop de mensajes Win32 (GetMessage/
> DispatchMessage), no con QApplication -- la inversión de control (paso 7
> de la secuencia Qt-2) sigue sin hacerse."

Sí existe un binario que corre bajo `QApplication::exec()` de verdad —
`opus_qt_shell` (`src/core/src/opus_qt_shell_main.cpp`) —, pero su propio
comentario de cabecera es explícito: *"Este NO es Word bajo Qt. No hay motor
de documento, no hay wordtech/ conectado."* Es un molde que demuestra que el
patrón de despacho de §B4.2 (`SendMessage` → llamada directa, `PostMessage` →
`QMetaObject::invokeMethod` con `Qt::QueuedConnection`) funciona contra los
contratos ya cerrados, para reutilizar el día que `wordtech/` se conecte de
verdad — no una segunda implementación de Word.

Y las pruebas que sí son de humo, con ese nombre en el propio proyecto, son
otras: la etiqueta `word1_startup_blocked` (`word1_port_smoke_test` + los 8
`opus_word1_ui_test`) exige que `WORD1` arranque y responda a interacción
real vía Wine/Winelib — hoy siguen en 0/9 pasando de verdad (llegan a lógica
de interacción, fallan con mensajes de nivel app; ver `README.md`, sección
Tests). Esas son las pruebas de humo del proyecto, y no tienen relación con
el trabajo de `src/core`.

### En una frase

Hay dos esfuerzos reales conviviendo en el mismo repo, y son ciertos a la
vez sin contradecirse: el port Winelib sigue siendo, hoy, el único dueño
del bucle de eventos y la ventana de `WORD1` — pero ese mismo `WORD1` ya
**es** Word sobre Qt en el sentido literal de que no arranca ni corre sin
Qt6, y ejecuta código Qt real (`QSettings`, un allocador, `QRawFont`,
`QMessageBox`) en cada una de las llamadas de la tabla de arriba. Lo que
falta para "Word sobre Qt" en el sentido arquitectónico completo —Qt
dueño de la ventana y el bucle, no solo de un backend enlazado por
debajo— es la inversión de control del paso 7, todavía sin tocar
`Opus/wproc.c`. `opus_qt_shell` es un tercer artefacto, un demostrador
aislado sin motor de documento, no una segunda implementación de Word.
Ver "Preguntas abiertas" #4 para si esto cambia el argumento de separar
repositorios.

---

## Cambios en el inventario que este diseño obligó

Al inspeccionar los dos sitios de *GDI texto/fuentes* dentro de
`Opus/wordtech/`, uno resultó falso positivo: `format.c:2165` tenía `TextOut`
dentro de un comentario. Una ocurrencia de `GetTextExtent` en
`dispspec.c:539` estaba dentro de una cadena literal. Dos veces el mismo modo
de falla por caminos independientes, así que el escaneo se corrigió de raíz:
`audit_win32_v2.py` ahora elimina comentarios y literales antes de tokenizar.

Efecto sobre las cifras que este documento usa:

| Magnitud | Antes | Después |
|---|---|---|
| Sitios *GDI texto/fuentes* | 200 | **170** |
| TUs con *GDI texto/fuentes* | 27 | **24** |
| Sitios *espina de mensajes* | 323 | **287** |
| Sitios *memoria Win16* | 206 | **201** |
| Sitios *configuración* | 44 | **43** |
| Sitios ABI resueltos por Winelib | 2407 | **2058** |
| `Opus/wordtech/` portables | 21/40 | **22/40** |
| TUs portables / frontera / presentación | 58 / 43 / 86 | **60 / 44 / 83** |

`wordtech/format.c` pasó de *presentación* a *portable*, y con eso
**`Opus/wordtech/` tiene una sola TU que toca medición de texto:
`layoutap.c`.** El detalle del barrido está en la sección correspondiente del
inventario.

---

## B1 — Clasificación núcleo Qt / shell

Las 83 TUs de *presentación* son shell por defecto y no se analizan una por
una, salvo las que están en región de núcleo (§B1.3). Se clasifican las 60
*portables* y las 44 de *frontera*.

### B1.1 Las 60 portables → núcleo, sin excepción

Solo tocan tipos primitivos, convenciones ABI ya neutralizadas por Winelib, o
constantes Win16. Entran al núcleo con la capa de typedefs, sin reescritura.
Distribución: `Opus/` raíz 27, `Opus/wordtech/` 22, `Opus/interp/` 3,
`Opus/debug/` 6, `port/original/` 2.

### B1.2 Las 44 de frontera → 32 núcleo, 10 shell, 2 diagnóstico

Su acoplamiento es solo de tipos handle, memoria Win16, geometría o
configuración: nada que exija reescritura. Pero el veredicto técnico no decide
sola la ubicación — el rol del archivo también cuenta. Un archivo de diálogo
cuyo único acoplamiento es `HWND` sigue siendo diálogo.

**Núcleo (32).** Lógica de documento, detrás de la API de frontera:

| Región | TUs |
|---|---|
| `Opus/wordtech/` (4) | `clsplc.c`, `curskeys.c`, `ihdd.c`, `outline.c` |
| `Opus/interp/` (4) | `exp.c`, `main.c`, `sym.c`, `to.c` |
| `Opus/` raíz (24) | `catalog.c`, `cmd2.c`, `cmdcore.c`, `compare.c`, `customiz.c`, `docman1.c`, `elfile.c`, `elsubs2.c`, `elsubs3.c`, `etcmd.c`, `fieldclc.c`, `fieldpic.c`, `fieldsc2.c`, `fieldspc.c`, `filecvt.c`, `glsy.c`, `hddwin.c`, `index1.c`, `replace.c`, `sort.c`, `spelcore.c`, `style.c`, `stylesub.c`, `toc.c` |

`curskeys.c` merece nota: traduce teclas de cursor a movimiento de documento.
Se queda en el núcleo y recibe eventos ya traducidos por el shell; no lee
estado de teclado.

**Shell (10).** Diálogos, ventanas y ayuda de menú, cuyo rol es presentación
aunque su acoplamiento medido sea leve: `curswin.c`, `dialog3.c`,
`dlglook1.c`, `dlglook2.c`, `dlgmisc.c`, `dlgopen.c`, `dlgrec.c`,
`dlgtable.c`, `filewin.c`, `menuhelp.c`. Se reescriben en Qt-5, no se portan.

**Diagnóstico (2).** `Opus/debug/debugcmd.c` y `debugdde.c`: por decisión de
proyecto `Opus/debug/` se porta, pero es instrumentación, no documento. Van a
un componente de diagnóstico del shell, sin entrar al núcleo ni bloquear
ninguna fase.

### B1.3 Las 14 TUs que exigen extracción antes de decidir

Están en región de núcleo (`wordtech/`) pero clasificadas *presentación*:
contienen lógica de documento mezclada con presentación. No son «shell por
defecto»; hay que separarlas. Es el trabajo más delicado de Qt-2 y la razón de
que Qt-1 no pueda cerrar la clasificación al 100 %.

| TU | Acoplamiento que la ancla | Naturaleza de la mezcla |
|---|---|---|
| `layoutap.c` | GDI texto + dibujo | Layout de *autotext* que además pinta. Contiene el único `TextOut` del núcleo |
| `pagevw.c` | mensajes + GDI dibujo + entrada | Vista de página: paginación (núcleo) y presentación de esa vista |
| `disp3.c` | mensajes + GDI dibujo | Cálculo de display y pintado en el mismo archivo |
| `scroll.c` | mensajes + GDI dibujo | Desplazamiento: qué es visible (núcleo) contra cómo se repinta |
| `disp2.c`, `disptbl.c`, `printsub.c` | GDI dibujo | Recorrido de estructura de documento con emisión de dibujo intercalada |
| `block.c` | GDI dibujo + entrada | Operaciones de bloque con realimentación visual |
| `insert.c`, `select.c`, `selecttb.c` | entrada/cursor | Edición y selección que consultan estado de cursor |
| `editspec.c`, `undo.c` | mensajes | Notificación de cambio vía mensajes; se sustituye por callbacks |
| `error.c` | mensajes | `MessageBox` real en la línea 1618, más `Yield` ×2 |

Estrategia recomendada: separar por función, no por archivo. Cada una se
parte en un `*_core.c` (sin Win32) y un `*_shell.cpp` (Qt), conservando el
nombre original como prefijo para que el historial siga siendo legible. No se
tocan hasta Qt-2, con la restricción de `CONTRIBUTING.md` sobre `src/Opus/`
vigente.

---

## B2 — Contrato de medición de texto (prioridad máxima)

### B2.1 Por qué la restricción se concentra en un punto

El riesgo declarado del proyecto suponía que la medición de texto estaba
entrelazada con la lógica de documento y no era abstraíble trivialmente. La
evidencia dice otra cosa: **el motor de layout ya trabaja contra una caché de
métricas, no contra GDI.**

`struct FTI` (`Opus/wordtech/format.h:379-410`) es esa caché:

```c
struct FTI {
    int  dxu;              /* ancho fijo si !fPS && fHeap */
    int  chFirst;
    int  cch;              /* chLast + 1 - chFirst */
    struct FONTREC far * far *qqftr;
    long bmpchdxu;         /* desplazamiento de la tabla de anchos en fontrec */
    uns  dxuFrac;          /* fracción de 16 bits mantenida entre caracteres */
    uns  wNumer, wDenom;   /* escalado */
    int  dypAscent, dypDescent, ftc, catr, ps;
};
```

El núcleo hace aritmética entera sobre esos campos, con acumulador de fracción
explícito (`dxuFrac`) y su propia función de multiplicación y división:
`NMultDiv`, usada en `Opus/wordtech/layout.h:467-469` y
`Opus/wordwin.h:252-255`. `NMultDiv` es del proyecto, no la `MulDiv` de GDI —
esa solo aparece en `port/original/opus_sdm_runtime.cpp` y
`opus_win95_chrome.cpp`, que son capa del port.

Quien llama a GDI para **llenar** la caché es el nivel de display:
`Opus/dispspec.c:787` y `:796`, con la forma
`GetTextExtent(hdc, &ch, 1) - tm.tmOverhang` carácter por carácter, más
`GetTextMetrics`.

Si el shell rellena la tabla de anchos con los mismos enteros que producía
GDI, toda la aritmética posterior del núcleo es entera y reproduce la
paginación byte a byte por construcción. La restricción no se distribuye por
170 sitios: se concentra en un único punto de llenado.

### B2.2 Contrato

Declarado en `src/core/include/OpusShellFontMetrics.h`. Interfaz en C,
implementada por el shell, consumida por el núcleo:

```c
/* Identidad de fuente tal como el núcleo la conoce: los mismos campos que
   FTI ya guarda. No se expone ningún tipo de Qt ni de Win32. */
typedef struct OpusFontKey {
    int ftc;    /* código de tipografía */
    int ps;     /* tamaño en medios puntos, como en FTI */
    int catr;   /* atributos: negrita, cursiva, … */
} OpusFontKey;

typedef struct OpusFontMetrics {
    int dypAscent, dypDescent;
    int dxpOverhang;      /* equivalente de TEXTMETRIC.tmOverhang */
    int dxpInch, dypInch; /* resolución del dispositivo destino */
    int dxuFixed;         /* != 0 si la fuente es de paso fijo */
} OpusFontMetrics;

/* Devuelve 0 en éxito. El shell no conoce FTI; el núcleo traduce. */
int OpusShellFontMetrics(const OpusFontKey *key, OpusFontMetrics *out);

/* Rellena rgdxu[0..cch-1] con el avance entero de cada carácter del rango,
   en las mismas unidades que la tabla apuntada por FTI.bmpchdxu. */
int OpusShellCharWidths(const OpusFontKey *key, int chFirst, int cch,
                        unsigned short *rgdxu);
```

Dos funciones. El núcleo no adquiere un `HDC`, no selecciona fuentes, no
pregunta por extensión de cadenas: pide la tabla una vez por fuente y hace su
propia aritmética, exactamente como hoy. **Este contrato queda validado por el
experimento de §B2.3 y no cambia.** Lo que cambió es la estrategia de
implementación detrás de él.

### B2.3 Cómo se reproduce el redondeo: resuelto empíricamente

Una versión anterior de este documento proponía un envoltorio aritmético que
partiera de unidades de diseño y aplicara la fórmula de conversión de
Microsoft
(`DeviceUnits = DesignUnits/unitsPerEm * PointSize/72 * DeviceResolution`),
y a la vez dejaba abierto si la fidelidad estricta era alcanzable con otro
rasterizador. Eran dos respuestas contradictorias a la misma pregunta. Se
resolvió midiendo.

**Montaje.** Lado oráculo: programa Winelib que reproduce la forma exacta de
`dispspec.c` — `CreateDCA("DISPLAY")`, `CreateFontIndirectA` con
`lfHeight = -MulDiv(pt, dpiY, 72)`, `GetTextMetricsA`, y
`GetTextExtentPoint32A` con `cch = 1` menos `tmOverhang`, para los 95
caracteres imprimibles ASCII. Lado Qt: `QRawFont` sobre el mismo archivo de
fuente físico, para aislar el redondeo de la sustitución de fuentes. Fuentes:
Liberation Serif, Sans y Mono, a 8, 10, 12, 14, 18, 24 y 36 puntos, 96 ppp.

**Resultado.** La aritmética sobre unidades de diseño **no reproduce GDI**:

| Estrategia | Coincidencia |
|---|---|
| Unidades de diseño × `dpi·pt/72` sin redondear (la fórmula propuesta) | de 0/95 a 95/95 según fuente y tamaño; inconsistente |
| Unidades de diseño × tamaño em redondeado a entero | 95/95 en 14 de 15 casos; falla en Liberation Serif 14 pt |
| **Avances enteros pedidos al rasterizador al mismo ppem entero, con `QFont::PreferFullHinting`** | **95/95 en 21 de 21 casos (1995 comparaciones, todas exactas)** |

El fallo aislado de la segunda estrategia explica por qué la primera no podía
funcionar. En Liberation Serif a 14 pt (19 px), el carácter `@` tiene 1886
unidades de diseño sobre 2048 por em: 1886/2048 × 19 = 17,497, que redondea a
17. GDI devuelve 18. Midiendo el mismo glifo por modo de hinting:

```
NoHinting         @=17.484 -> 17
VerticalHinting   @=17.484 -> 17
FullHinting       @=18.000 -> 18      <- coincide con GDI
Default           @=17.484 -> 17
```

La diferencia es *grid-fitting*, no redondeo: el programa de hints de la
fuente ajusta el avance a la rejilla de píxeles. Ninguna aritmética de
envoltorio puede cerrar un desplazamiento de medio píxel que proviene de las
instrucciones de hinting de la propia fuente.

**Por qué la coincidencia exacta sí es alcanzable, y por qué eso confirma el
alcance en lugar de limitarlo.** Porque el oráculo del proyecto es la GDI de
**Wine**, y Wine rasteriza con FreeType, igual que Qt. Pedir el mismo modo de
hinting al mismo ppem entero produce los mismos enteros porque debajo corre el
mismo motor.

Conviene ser explícito en cómo se lee esto. La restricción fijada al abrir la
rama fue «paginación idéntica byte a byte **respecto del oráculo Winelib**», no
respecto de Windows real. Que la equivalencia se apoye en que ambos lados
rasterizan con FreeType no es una salvedad sobre el resultado: es la definición
del objetivo, tal como quedó decidida antes de este experimento. El
experimento confirma que ese objetivo es alcanzable y con qué estrategia
concreta.

De ahí se sigue, y queda asentado como alcance y no como pendiente: la
equivalencia vale contra el binario Winelib. No se afirma nada sobre la GDI de
Microsoft sobre Windows, cuyo rasterizador es otro, porque reproducir esa no
es —ni fue— un objetivo de esta rama. Si alguna vez se quisiera, sería un
cambio de alcance con su propia decisión, no un defecto de este diseño.

**Estrategia que rige.** El shell obtiene los avances así:

```
px = MulDiv(ps/2, dypInch, 72)                    /* mismo redondeo entero que GDI */
QRawFont rf(fontData, (qreal)px, QFont::PreferFullHinting);
rf.advancesForGlyphIndexes(...)                   /* ya vienen ajustados a la rejilla */
```

Dos requisitos, ambos necesarios: redondear el tamaño en píxeles a entero
**antes** de construir la fuente, y pedir `PreferFullHinting`. Omitir
cualquiera reintroduce la discrepancia.

La tabla capturada del oráculo deja de ser mecanismo y queda solo como prueba
de regresión: se captura una vez para el conjunto de fuentes soportadas y se
compara en cada cambio del shell.

### B2.4 Qué API de Qt queda detrás

**`QRawFont`**, construido con tamaño en píxeles entero y
`QFont::PreferFullHinting`. Da acceso a una instancia física de fuente y
devuelve avances ya ajustados a la rejilla, que es exactamente lo que GDI
entrega.

**Descartada: `QFontMetricsF`.** Devuelve `qreal` con la conversión y el
redondeo de Qt aplicados; no permite fijar ppem entero ni modo de hinting con
la precisión que la equivalencia exige.

**Descartada para el camino de documento: `QTextLayout`.** Hace su propio
shaping, salto de línea y posicionamiento de cursor conforme a Unicode. Word
1.1a tiene su propio algoritmo de salto de línea dentro de `wordtech/`, y esa
es precisamente la lógica que la rama Qt quiere preservar. Usarlo pondría dos
motores de layout a competir y rompería la fidelidad por diseño. Qt queda
reducido a rasterizador de glifos y proveedor de avances.

### B2.5 Riesgo residual, medido

- **`tmOverhang`: descartado como riesgo.** Se midió a 14 pt para Liberation
  Serif en normal, negrita, cursiva y negrita cursiva, y para los nombres de
  época `Helv`, `Tms Rmn`, `Script` y `Modern`, incluyendo negrita y cursiva
  sintetizadas. `tmOverhang = 0` en los ocho casos: con fuentes TrueType bajo
  Wine el concepto no interviene. Era el candidato más probable a discrepancia
  y no lo es.
- **Sustitución de fuentes: el riesgo real, y es concreto.** El oráculo
  sustituye los nombres de época, y no de forma intuitiva. Medido:
  `Helv`, `Tms Rmn`, `Script` y `Modern` resuelven **todos a Liberation Sans**
  —incluido `Tms Rmn`, que es un nombre serif—. Como los avances dependen del
  archivo físico, el shell debe reproducir **la misma tabla de sustitución que
  aplica Wine**, no simplemente tener fuentes disponibles. Si el shell resuelve
  `Tms Rmn` a un serif y el oráculo a Liberation Sans, todos los avances
  difieren y la fidelidad se pierde por completo, sin que la aritmética tenga
  nada que ver.
- **Fidelidad de píxeles frente a fidelidad de paginación.** Los avances
  enteros coinciden, lo que basta para saltos de línea y de página idénticos.
  Igualdad píxel a píxel de la forma pintada no está verificada y no es lo que
  la restricción exige.
- **Compilación condicional.** El inventario no evalúa `#if`, así que algún
  sitio contado puede estar desactivado en la configuración de build real.
  Afecta el dimensionamiento, no el contrato.

### B2.6 Tabla de sustitución de fuentes

Medido con la sonda `docs/port-qt/scripts/fidelity/font_substitution.c`
(mismo entorno que §B2.3: Wine 10.0, Debian trixie) para los 4 nombres de
época que `Opus/initwin.c` carga en la tabla maestra de arranque
(`vhsttbFont`, `ftc` 0-3):

```
Tms Rmn    -> Liberation Serif         charset=0 overhang=0
Symbol     -> Liberation Sans          charset=0 overhang=0
Helv       -> Liberation Sans          charset=0 overhang=0
Courier    -> Liberation Mono          charset=0 overhang=0
```

**Corrección respecto a una medición anterior de esta sección:** la primera
versión de la sonda construía el `LOGFONTA` con `ZeroMemory` y nunca fijaba
`lfPitchAndFamily`, quedando en 0 (`DEFAULT_PITCH | FF_DONTCARE`) para los
cuatro nombres — eso hacía que Wine ignorase la familia tipográfica al
resolver y los cuatro colapsaran a `Liberation Sans`. El motor real siempre
fija ese campo: `Opus/LOADFONT.C:864`,
`plf->lfPitchAndFamily = (pffn->ffid & maskFfFfid) | fcid.prq;`, con
`ffid`/`prq` tomados de la tabla maestra de arranque
(`Opus/initwin.c:1543-1583`):

| Nombre de época | `ffid` | `prq` |
|---|---|---|
| `Tms Rmn` | `FF_ROMAN` | `VARIABLE_PITCH` |
| `Symbol` | `FF_DECORATIVE` | `DEFAULT_PITCH` (0) |
| `Helv` | `FF_SWISS` | `VARIABLE_PITCH` |
| `Courier` | `FF_MODERN` | `FIXED_PITCH` |

Con la sonda corregida para fijar `lfPitchAndFamily` a `ffid | prq` por
nombre (igual que hace el motor), **no los cuatro resuelven a la misma
familia**: `Tms Rmn` resuelve a Liberation Serif y `Courier` a Liberation
Mono; solo `Symbol` y `Helv` coinciden en Liberation Sans, porque ambos
piden una familia `sans-serif` (`FF_DECORATIVE` y `FF_SWISS`
respectivamente) bajo Wine/fontconfig en este entorno.

Sobre `Symbol` en particular: lo llamativo no es el charset devuelto —pedir
`ANSI_CHARSET` y recibir `tmCharSet=0` (ANSI) es exactamente lo esperado,
no una contradicción— sino que el *nombre* `Symbol` sugeriría una fuente de
símbolos y en cambio el motor pide, a propósito, `ANSI_CHARSET` para esa
entrada: `Opus/initwin.c:1559` fija
`ChsPffn(pffn) = ANSI_CHARSET;` con el comentario original de Microsoft
("weirdly, this is correct for Postscript; other printers will have to
tell us what they do — enumeration will override these settings if
necessary"), y `Opus/LOADFONT.C:880` (`plf->lfCharSet = ChsPffn(pffn);`)
traslada ese charset sin modificarlo al `LOGFONTA`. La sonda, al pedir
`ANSI_CHARSET` para `Symbol`, reproduce fielmente el comportamiento del
motor — no es un artefacto de la sonda. No hay tratamiento especial que
preservar del lado shell más allá de eso: `Symbol` se sustituye por una
fuente sans-serif normal, no hace falta una fuente de símbolos ni una
tabla de glifos aparte.

Resolución de familia a archivo físico, verificada cruzada (no se acepta la
respuesta de `fc-match` sin confirmar contra la tabla `name` del propio
archivo), para las 3 familias distintas involucradas:

```
$ fc-match -f '%{file}\n' "Liberation Serif"
/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf
$ fc-scan --format '%{family}\n' /usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf
Liberation Serif

$ fc-match -f '%{file}\n' "Liberation Sans"
/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf
$ fc-scan --format '%{family}\n' /usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf
Liberation Sans

$ fc-match -f '%{file}\n' "Liberation Mono"
/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf
$ fc-scan --format '%{family}\n' /usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf
Liberation Mono
```

Coincide en los tres casos: la familia que reporta la propia tabla `name`
de cada archivo es la misma que `fc-match` resolvió. Tabla final:

| Nombre de época | Familia resuelta | Archivo físico |
|---|---|---|
| `Tms Rmn` | Liberation Serif | `/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf` |
| `Symbol` | Liberation Sans | `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf` |
| `Helv` | Liberation Sans | `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf` |
| `Courier` | Liberation Mono | `/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf` |

**Nota de cara al futuro (B2, selección de ppem):** `Opus/LOADFONT.C:829-837`
tiene un "HACK" documentado que, específicamente para `Courier`, mantiene
`lfHeight` positivo en vez de negativo como en el resto de los nombres —
selecciona la fuente por altura de celda, no por altura de carácter, porque
la fuente Courier de Windows tenía píxeles internos molestos en su área de
"leading". Verificado que esto no cambia a qué familia resuelve Courier
aquí (sigue siendo Liberation Mono), pero cualquier trabajo posterior de
selección de ppem (B2) tendrá que reproducir ese signo distinto para
Courier, no solo el mapeo de familia.

Implementado como tabla estática en
`src/core/include/OpusShellFontSubstitution.h` /
`src/core/src/OpusShellFontSubstitution.cpp` — ver ese header para el
contrato. Solo cubre estos 4 nombres; `Script` y `Modern` quedan fuera de
alcance de esta medición (no están en la tabla maestra de arranque, ver
`01-frontera-nucleo-shell.md`, "Secuencia recomendada para Qt-2", paso 2).

### B2.7 Conexión real al primer llamador: `C_LoadFcid`

`Opus/LOADFONT.C:187 C_LoadFcid`, camino de pantalla (`!pfti->fPrinter`),
sin vista previa (`!vfPrvwDisp`), paso variable
(`tm.tmPitchAndFamily & maskFVarPitchTM`), llama a `OpusShellCharWidths`
en vez de `GetCharWidth`/`OurGetCharWidth` para llenar
`FCE.hqrgdxp`/`FTI.rgdxp`, guardado bajo
`#if defined(__GNUC__) && !defined(_MSC_VER)` — MSVC sigue con GDI sin
cambios. Mapeo de campos, verificado contra el sitio real:

- `OpusFontKey.ftc` ← `fcid.ibstFont`. La tabla maestra de arranque
  (`Opus/initwin.c:1541-1583`, ya citada en §B2.6) registra Tms Rmn,
  Symbol, Helv, Courier en ese orden como `ibstFont` 0-3 — el mismo orden
  que `EraNameFromFtc` en `OpusShellFontMetrics.cpp` ya asumía. No hacía
  falta ninguna tabla de traducción nueva: `ibstFont` **es** el `ftc` que
  el contrato espera, para estas 4 entradas.
- `OpusFontKey.ps` ← `fcid.hps` (medios puntos — el mismo campo que
  `C_FGraphicsFcidToPlf`, `Opus/LOADFONT.C:832`, ya usa para construir
  `lfHeight`).
- `OpusFontKey.catr` ← `(fcid.fBold ? 1 : 0) | (fcid.fItalic ? 2 : 0)`.
  Solo importa que sea `0` o no: el contrato no sabe medir negrita ni
  cursiva (limitación 2 de `OpusShellFontMetrics.cpp`), así que cualquier
  atributo activo debe fallar controlado, no aproximarse.

Fallo controlado: si `OpusShellCharWidths` devuelve error (fuente no
soportada, `catr != 0`, o cualquier otro caso fuera del contrato), el
camino nuevo salta a `LSystemFontErr` — el mismo destino que ya usa un
fallo de `CreateFontIndirect` más arriba en la misma función. `fFallback`
queda en `fTrue`, la fuente cae al stock/sistema y, al volver a entrar en
el bloque de ancho variable, la guarda `!fFallback` ya existente lo salta
por completo: se degrada a ancho fijo (`tm.tmAveCharWidth`) igual que
cualquier otro fallo de fuente en este código, sin aproximar con GDI en
silencio. No se implementó ningún camino nuevo de recuperación — se
reutilizó el que ya existía.

Overhang: no se fuerza `dxpOverhang = 0` en el sitio de integración —
`pfce->dxpOverhang` sigue viniendo de `tm.tmOverhang` (GDI real, la misma
llamada a `GetTextMetrics` que ya corría antes de este cambio, sin
tocar). La sustracción de overhang del camino GDI (`LOADFONT.C:482-490`
en el numerado actual) queda intacta pero fuera del camino nuevo, que no
la necesita: §B2.5 midió `tmOverhang = 0` en los 8 casos de estilo bajo
TrueType/Wine, así que en la práctica ambos caminos coinciden en el valor
(0), no por una corrección aplicada dos veces.

**Qué queda verificado end-to-end y qué no.** Los 5 tests de `src/core`
(incluida `opus_shell_font_metrics_fidelity_test`, 2660/2660) siguen en
verde tras este cambio — pero no ejercitan `Opus/LOADFONT.C`, solo la
biblioteca nativa que ese archivo ahora llama. La conexión del lado
Winelib (`Opus/` compilado con `wineg++`/`winegcc` contra el contrato) no
se pudo compilar en esta sesión: `Opus/wordtech/disp.h:248` (`struct DR
rgdr[]` dentro de una `union`) es rechazado por GCC 14 con "flexible
array member in union" — error preexistente, confirmado con `git stash`
contra el mismo commit antes de este cambio, en un archivo que este
trabajo no toca. Bloquea la compilación de `loadfont.c.o` (y de
`opus_x64_layout.c`, y por tanto de `WORD1` entero) en este entorno,
independientemente de §B2.7. Es un bloqueador de entorno/toolchain, no
del contrato ni de esta integración — pero significa que el enlace real
`WORD1` → `opus_shell_font_metrics` (cableado en `src/CMakeLists.txt` en
esta misma sesión, ver el commit de wiring) no se probó compilando de
punta a punta, solo se verificó que el `find_package(Qt6 ... Gui)` y las
declaraciones `IMPORTED` resuelven (`cmake --preset
linux-winelib-debug` configura limpio). Aparte de eso, `WORD1` ya
arranca con heap corruption conocido antes de llegar a un estado usable
(`word1_startup_blocked`, ver `CLAUDE.md`) — así que aunque el bloqueador
de `disp.h` no existiera, este cambio por sí solo no habría podido
verificarse "contra layout real" corriendo el binario.

**Conclusión sobre el criterio de desbloqueo de `scroll.c`/`disp3.c`/
`pagevw.c`:** el código está conectado (§B2.7 cierra la ruta de llamada
que faltaba), pero el criterio del documento — "B2 implementado y
verificado contra layout real" — pide verificación en ejecución, no solo
en compilación de tipos. Esa verificación no ocurrió esta sesión por dos
bloqueadores independientes de este trabajo (compilación `disp.h`/GCC 14,
arranque de `WORD1`). `scroll.c`/`disp3.c`/`pagevw.c` **siguen fuera de
alcance** hasta que alguno de esos dos bloqueadores se resuelva y B2 se
pueda observar produciendo paginación real.

---

## B3 — Contrato de memoria Win16

201 sitios, 21 TUs. De esas, 6 son de frontera en el núcleo (`catalog.c`,
`elfile.c`, `elsubs2.c`, `etcmd.c`, `filecvt.c`, `spelcore.c`), 1 de
diagnóstico y 14 de presentación.

### B3.1 Lo que ya está resuelto, y no se repite

`src/port/original/opus_x64_compat.h` ya resolvió el empaquetado de punteros
para el port Winelib, y este diseño se apoya en eso sin reimplementarlo:

- `LOWORDX` / `HIWORDX` / `MAKELONGX` (líneas 305-312) operan sobre
  `uintptr_t`, no sobre `WORD`, de modo que un puntero de 64 bits no se
  trunca al empaquetarse.
- Las macros de desempaquetado de punto (líneas 319-333) extraen coordenadas
  con extensión de signo desde un valor empaquetado del ancho del puntero.
- El manejo de `dkt`/`dktString` para parámetros tipados está anotado en la
  línea 34.

Qt-2 usa esas macros tal como están. Lo que sigue es lo que **no** está
resuelto.

### B3.2 Corrección de fondo: `HANDLE` no mide 16 bits en este build

Una versión anterior de esta sección afirmaba, citando
`Opus/lib/qwindows.h:630` (`typedef WORD HANDLE`), que "un handle Win16 es
un entero de 16 bits" y que de ahí salía el problema del contrato. Medido
antes de diseñar nada más: **falso para este build.**

```c
#include "word.h"
printf("sizeof(HANDLE)=%zu\n", sizeof(HANDLE));   /* → 8 */
```

`qwindows.h` es el SDK Win16 vendorizado, pero esa rama de `word.h` está
detrás de `#ifdef OPUS_X64 ... #else ... #include "qwindows.h" ... #endif`
—y `OPUS_X64` está definido en este build—. La rama que sí se compila
incluye `opus_x64_compat.h`, que arrastra el `windows.h` real de Wine
(`winnt.h: typedef void *HANDLE`). `qwindows.h:630` es código muerto para
este target; la cita original nunca se verificó contra lo que realmente
compila.

Esto no vuelve trivial el contrato, cambia por qué existe. No hace falta
un allocator opaco porque un handle no entre en un campo de 16 bits —ya
entra, mide lo mismo que un puntero—. Hace falta porque un handle sigue
siendo indirección de tiempo de ejecución que el núcleo no debe fijar por
diseño (independencia del shell) ni serializar nunca (§B3.3).

### B3.3 Fase 1 — Estructuras persistidas con campo handle: ninguna encontrada

Se recorrieron, antes de escribir una sola línea del header, todos los
campos de tipo `HANDLE`/`GLOBALHANDLE`/`HGLOBAL`/`LOCALHANDLE` en `Opus/`.
De los que son campos de struct (no variables locales ni parámetros —la
mayoría de las ~90 coincidencias de `HANDLE` en el árbol son eso), estos
son los que aparecieron y por qué ninguno se persiste:

| Struct | Campo(s) | Qué es | Por qué no se serializa |
|---|---|---|---|
| `Opus/core.h` | `rghcdModules[]` | caché de handles de módulo de código (`GetCodeHandle`, `wproc.c`) | vive y muere con la sesión; ninguna escritura a archivo la toca |
| `Opus/dde.h` (`DDLI`) | `hData` | último mensaje DDE | IPC en vivo entre procesos, no hay "DDE guardado" |
| `Opus/dde.h` (`DRVDATA`) | `hrgbKeyState` | registro de teclas para reproducción de macros | estado de sesión de `eldde.c`/`quit.c`, `GlobalAlloc`/`Free` en memoria |
| `Opus/filecvt.h` (`EXCR`) | `hLib`, `ghszFn`, `ghszSubset`, `ghBuff`, `ghszVersion` | carga de DLL conversora externa | scratch de `filecvt.c` mientras dura la conversión, `GlobalAlloc`/`Free` puros |
| `Opus/el.h` (`CABX`) | `rgh[]` | contenedor genérico "SDM privado" | comentario propio del código: "necessary for CAB access... internal"; sin escritura a archivo |
| `Opus/el.h` (`DKD`) | `hLib` | biblioteca de add-in cargada | no referenciado por nombre en ningún `.c`; huérfano |
| `Opus/grstruct.h` (`PICT`) | `hbm` | bitmap de GDI para repintar | único uso real es `SelectObject` (`rsb.c:601`); el formato de imagen en disco (`PIC.H`) no tiene campos handle |
| `Opus/wordtech/file.h` (`ELOF`) | `hFile` | handle de SO de un archivo abierto por el lenguaje de macros | por definición no sobrevive a un reinicio; nunca se escribe a sí mismo |

Cruzado contra las estructuras que sí son el formato de archivo real —FIB,
FIB30, DOP, STSH, PAP, CHP, SEP, BKF, FFN, STTB— **ninguna tiene un campo
handle.** Coincide con la práctica ya esperada de la época: el formato de
Word 1.1a guarda índices (FTC, STC, desplazamientos de FKP) precisamente
porque un handle no sobrevive a un guardado, no es un cuidado que este
port haya introducido.

**Veredicto de Fase 1: ninguna estructura persistida con campo handle.**
No cambia la forma del contrato —sigue siendo el handle opaco de
§B3.2— y no bloquea la Fase 2.

**Hallazgo colateral, fuera de alcance de este documento pero que no se
puede callar:** al verificar `struct FTI` para esta fase se encontró que
la cita central de §B2.1 (`Opus/wordtech/format.h:379-410`, con
`dxuFrac`/`bmpchdxu`/`struct FONTREC far * far *qqftr`) describe una
estructura que está **dentro de `#ifdef MAC`** —muerta en este build,
igual que le pasaba a `qwindows.h`—. El `struct FTI` que sí se compila
bajo `WIN`/`OPUS_X64` vive en `Opus/fontwin.h:126-152`: sin acumulador de
fracción, con `int rgdxp[256]` (tabla de anchos inline, no un puntero a
tabla externa) y `HFONT hfont`. No se toca el contrato de medición de
texto (B2) en este documento —fuera de alcance de este prompt—, pero
queda anotado: **B2.1 describe la estructura equivocada** y necesita
revisión propia antes de implementarse.

### B3.4 Contrato

Declarado en `src/core/include/OpusShellMemory.h`, con `OpusMemHandle`
(equivalente de `GlobalHandle`) agregado en la Fase 2 de este contrato:

```c
/* Handle opaco de ancho completo. Nunca se empaqueta en 16 bits, nunca se
   escribe a disco, nunca se compara contra un literal. */
typedef struct OpusHandleImpl *OpusHandle;

#define OPUS_MEM_ZEROINIT 0x0001u

OpusHandle    OpusMemAlloc(unsigned long cb, unsigned flags);
void         *OpusMemLock(OpusHandle h);     /* fija y devuelve puntero */
void          OpusMemUnlock(OpusHandle h);   /* libera la fijación */
OpusHandle    OpusMemRealloc(OpusHandle h, unsigned long cb, unsigned flags);
unsigned long OpusMemSize(OpusHandle h);
OpusHandle    OpusMemHandle(void *ptr);      /* puntero → handle dueño */
void          OpusMemFree(OpusHandle h);
```

Un solo contrato cubre `Global*` y `Local*`: bajo este port ambos heaps
Win16 ya son el mismo heap nativo, así que la distinción no tiene nada que
preservar. `LocalHandle` no aparece (0 coincidencias por grep independiente
en todo `Opus/`+`OpusEtAl/`); el resto de la familia sí (`GlobalHandle`: 11,
`LocalAlloc`: 5, `LocalReAlloc`: 3, `LocalLock`/`LocalUnlock`/`LocalSize`:
1 cada uno) pese a no estar en la tabla de símbolos del inventario Qt-0
—`debugwin.h` no intercepta esos nombres puntuales, así que el mecanismo
de derivación del diccionario no los capturó; confirmado con grep directo,
no asumido ausente—.

Notas de diseño, en orden de riesgo:

1. **Memoria movible y disciplina de fijación.** El modelo Win16 permitía que
   el gestor moviera un bloque desbloqueado; `GlobalLock`/`GlobalUnlock` forman
   pares que el código respeta hoy. Qt no tiene equivalente. El allocator del
   núcleo puede fijar todo permanentemente —la memoria de un proceso de 64 bits
   lo permite— pero **los pares deben conservarse en el código**: eliminarlos
   como no-operaciones haría imposible detectar un uso de puntero tras mover,
   si más adelante se introdujera compactación. Verificado en la
   implementación (§B3.5): `OpusMemLock` tras `OpusMemFree` devuelve `NULL`
   en vez de un puntero colgante.
2. **`GlobalLockClip`.** Variante de bloqueo específica de portapapeles. Va al
   contrato de portapapeles de Qt-6, no a este. Se anota aquí para que no se
   resuelva dos veces.
3. **Todo campo de estructura serializada con tipo handle necesita
   indirección.** Resuelto por la Fase 1 (§B3.3): no hay ninguno hoy. Si
   algún día se agrega uno, ese es el momento de diseñar la indirección, no
   antes.

### B3.5 Implementación y verificación — cerrado

Lado núcleo implementado en `src/core/src/OpusShellMemory.cpp`: handle
opaco sobre `malloc`/`realloc`/`free` con contador de fijación y una
marca de "liberado" que se conserva después de `OpusMemFree` —a propósito,
para poder detectar mal uso en vez de leer memoria ya reciclada—.
`OpusMemHandle` usa un registro `puntero → handle` (`std::unordered_map`),
no aritmética de punteros, porque la dirección que devuelve `malloc` no
tiene relación fija con la del `OpusHandleImpl` que la posee. No depende
de Qt: es la primera implementación de los tres contratos restantes, y no
necesitaba Qt para nada.

**No alcanza con que compile ni enlace** —eso ya lo probó
`link-check/` para paso de valores por copia (commit `7760a28`)—.
Lo que este contrato cruza es un handle/puntero real, así que la prueba
tiene que demostrar que sobrevive la frontera, no solo que se puede pasar.

Sonda en `docs/port-qt/scripts/handle-check/` (`handle_check.c`),
compilada y enlazada con **wineg++** —el driver real de `WORD1`, no
`winegcc`— contra `libopus_shell_memory.a`:

1. `OpusMemAlloc` → handle real.
2. `OpusMemLock` → puntero válido.
3. Escribe el patrón `"OpusMemHandleRoundTrip-2026"` a través del puntero.
4. `OpusMemUnlock`, `OpusMemLock` de nuevo: **el patrón sigue ahí**, no
   solo "el puntero es no nulo".
5. `OpusMemHandle(puntero)` devuelve el mismo handle original.
6. `OpusMemFree`, luego `OpusMemLock` sobre el handle liberado: `NULL`,
   fallo controlado, no puntero colgante.
7. Modo aparte `--double-free`: `OpusMemAlloc` → `OpusMemFree` →
   `OpusMemFree` otra vez. La segunda llamada hace `abort()`. Bajo Wine
   esto se ve como el volcado de WineDbg capturando el `SIGABRT` —ruidoso,
   pero es exactamente Wine reaccionando a una terminación anormal real,
   no un error de la sonda—; el proceso termina con exit 134
   (128+SIGABRT), nunca con exit 0.

Las siete verificaciones pasaron en la corrida real (`run.sh`, sobre la
biblioteca que construye `opus_core_build`, no una copia de scratch); el
proceso de doble liberación terminó con exit 134, no 0. Sin regresión:
`ctest -R opus_shell_config_test` sigue en verde después de agregar
`opus_shell_memory` al mismo `CMakeLists.txt`.

**Consecuencia:** la frontera física ya probó pasar valores por copia
(configuración) y ahora pasar ownership de un bloque de memoria real
(handles). Los dos modos de cruce que necesitan los contratos restantes
—espina de mensajes (callbacks, sin estado que cruce la frontera más que
el valor de retorno) y medición de texto (arrays de anchos por valor,
igual que configuración)— no introducen una tercera categoría nueva.

---

## B4 — Contrato de espina de mensajes y ventanas

287 sitios, 47 TUs: `Opus/` raíz 38, `Opus/wordtech/` 6, `Opus/debug/` 3.
Los dos fragmentos con firma concreta hoy (§B4.3) están declarados en
`src/core/include/OpusShellSpine.h`; el resto de esta sección sigue siendo
tabla conceptual, no API.

### B4.1 La inversión de control es el cambio estructural mayor

Hoy el código de Word **posee** el bucle de mensajes: llama a `GetMessage` y
`DispatchMessage` desde `Opus/` raíz. En Qt el bucle pertenece a
`QCoreApplication` y llama hacia dentro.

Esa inversión, no el mapeo de símbolos, es el trabajo real de esta frontera.
El núcleo pasa de conductor a biblioteca: expone entradas que el shell invoca,
y notifica hacia fuera por callbacks en lugar de publicar mensajes.

### B4.2 Mapeo conceptual

| Win16 | Qt | Nota |
|---|---|---|
| `GetMessage`, `PeekMessage`, `DispatchMessage`, `TranslateMessage` | bucle de `QCoreApplication` | Desaparecen del núcleo; el shell posee el bucle |
| `RegisterClass` | subclase de `QWidget` | Sin equivalente directo, se disuelve |
| `CreateWindow`, `DestroyWindow` | construcción y destrucción de `QWidget` | |
| `SendMessage` | llamada directa por la API de frontera | Semántica sincrónica, se preserva |
| `PostMessage` | `QMetaObject::invokeMethod` con `Qt::QueuedConnection` | Semántica diferida, se preserva |
| `DefWindowProc`, `CallWindowProc` | manejo por defecto de `QWidget` | |
| `ShowWindow`, `UpdateWindow`, `MoveWindow`, `SetWindowPos`, `EnableWindow` | métodos de `QWidget` | Mapeo directo |
| `MessageBox` | `QMessageBox` | Solo desde el shell; el núcleo nunca abre UI |
| `SetTimer` | `QTimer` | |
| `MakeProcInstance`, `FARPROC` | punteros a función | Ya neutralizado por Winelib |
| `Yield` | se elimina | Artefacto de multitarea cooperativa de Win16 |

### B4.3 Las 6 TUs de `wordtech/` con espina de mensajes

Exigen extracción, no mapeo. Por orden de dificultad creciente:

- **`error.c`** — la más simple y la primera a hacer. `MessageBox` real en la
  línea 1618, más dos `Yield`. Se sustituye por un callback de error en la API
  de frontera: el núcleo entrega código y contexto, el shell decide la
  presentación.
- **`editspec.c`, `undo.c`** — **corrección sobre una lectura anterior de este
  documento.** No notifican cambios de documento por mensajes: el símbolo que
  los clasifica en esta categoría es `MessageBeep(MB_OK)`
  (`editspec.c:1855,2075`, `undo.c:97`), verificado independientemente por
  grep, no un mecanismo de notificación. En los tres sitios es el mismo
  patrón — pila de deshacer vacía (`undo.c:97`, `vuab.uac == uacNil`) o rango
  de bloque inválido (`editspec.c`, `LRetFalse`) — señal audible de "operación
  rechazada", con `Beep()` bajo la rama `!OPUS_X64` ya presente en el propio
  archivo. Se sustituye por un callback de alerta trivial, sin texto, del
  mismo tipo que el de `error.c` pero sin mensaje que resolver. La
  notificación real de cambio de documento —si existe como mecanismo
  distinto— no está localizada todavía; no se afirma que exista solo porque
  Qt-1 la previó en abstracto.
- **`scroll.c`, `disp3.c`, `pagevw.c`** — mezclan cálculo de qué es visible
  (núcleo) con repintado (shell). Requieren la separación por función descrita
  en §B1.3 y no deberían intentarse antes de que el contrato de medición de
  texto esté implementado y verificado.

### B4.4 Conexión real del primer sitio: `error.c:1618`

`Opus/wordtech/error.c`, función `ErrorEidStartup`, sustituye el `MessageBox`
real por `OpusShellReportError(eid, szMsg)`, guardado bajo
`#if defined(__GNUC__) && !defined(_MSC_VER)` — mismo patrón que §B2.7. MSVC
sigue con `MessageBox` real, sin cambios.

Verificado antes del cambio, `git grep -n 'MessageBox' src/Opus/wordtech/
error.c`: la línea 1618 es la única llamada directa a `MessageBox` en el
archivo — las otras tres coincidencias (1214, 1549, 1674) son
`IdMessageBoxMstRgwMb`, el wrapper propio de Word, no la API Win32, y la de
1582 es un comentario.

Mapeo de parámetros contra la firma (`src/core/include/OpusShellSpine.h:46`,
`void OpusShellReportError(int eid, const char *message);`):

- `eid` — el mismo parámetro que ya recibe `ErrorEidStartup(eid)`, sin
  transformación.
- `message` ← `szMsg`, el texto que la propia función ya resuelve vía
  `IemdFromEid`/`CopyEmdSt` antes de la llamada — el contenido del error no
  pierde nada.
- `szApp` (el título de la ventana) **no** tiene lugar en el contrato a
  propósito: `OpusShellSpine.cpp:20` fija el título a `"Word"` traducido por
  Qt, no al valor de `szApp`. `szApp` es una constante global de la app
  (`extern CHAR szApp[]`, el mismo valor en cualquier sitio de llamada, no un
  dato específico de este error), así que no hay pérdida de información *del
  error*: es una decisión de presentación ya explícita en el header
  (`OpusShellSpine.h:17`, "el shell decide la presentación"), no un recorte
  improvisado en este cambio.
- `MB_OK|MB_SYSTEMMODAL` ← ya fijos dentro de `OpusShellReportError`
  (`QMessageBox::Ok`, `Qt::ApplicationModal`) — la llamada real en `error.c`
  nunca varía esos flags entre invocaciones (es la única llamada), así que no
  hay combinación de flags que el contrato deba parametrizar.

Fallo controlado: sin cambios de comportamiento en caso de error — la ruta
sigue siendo la misma función, mismos dos `Yield()` alrededor de la llamada
(bug de Windows documentado en el comentario original), sin `try`/`catch` ni
fallback silencioso añadido.

**Diferencia real frente a §B2.7: esta vez sí se pudo compilar contra
Winelib de punta a punta.** `error.c` no incluye `disp.h` ni `rsb.h` ni
directa ni transitivamente (verificado por `git grep`, dos niveles, ver la
sesión de reconocimiento previa) — no choca con el bloqueador de GCC 14 en
`Opus/wordtech/disp.h:248`. `ninja
CMakeFiles/opus_original_engine.dir/Opus/wordtech/error.c.o` bajo
`wineg++`/`winegcc` real compiló sin errores (solo warnings preexistentes,
ninguno en las líneas tocadas) y produjo el objeto
(`error.c.o`, 41784 bytes). Esto es compilación real contra el árbol
Winelib, no solo los 5 tests nativos de `src/core` — pero **no es
ejecución**: `WORD1` completo sigue sin poder enlazarse/arrancar en este
entorno (el resto del árbol sí choca con `disp.h`/GCC 14, y por separado
`WORD1` tiene el bloqueador de arranque conocido,
`word1_startup_blocked`), así que el diálogo modal real de
`OpusShellReportError` en este sitio de llamada específico no se disparó ni
se observó en ejecución esta sesión. Lo que sí está verificado en
ejecución es el contrato en sí (`opus_shell_spine_test`, diálogo modal real
vía `QMessageBox`/`QTimer`, commit 79b181f) — no la ruta completa desde
`ErrorEidStartup` real.

**Esto NO desbloquea `disp.h`/GCC 14.** Sigue siendo un bloqueador de
entorno/toolchain aparte, sin tocar en esta sesión, que sigue afectando a
`editspec.c`/`undo.c` (los otros dos sitios de §B4.3, ambos incluyen
`disp.h`) y a la mayoría del árbol. Que `error.c` haya compilado limpio es
una propiedad de ese archivo puntual (no incluye `disp.h`/`rsb.h`), no una
resolución del problema de fondo.

---

## B5 — Contrato de persistencia de configuración

42 sitios, 12 TUs (cifra corregida en §B5.2 al migrar; la categoría
*Persistencia de configuración* del inventario suma 43 porque también
incluye `GetEnvironmentVariableA`, fuera de este contrato). Símbolos:
`GetProfileString`, `GetProfileInt`, `WriteProfileString`.

El más simple de los cuatro, y se deja simple. Declarado en
`src/core/include/OpusShellConfig.h`. La semántica de `WIN.INI`
—sección, clave, valor por omisión— corresponde uno a uno con `QSettings`:

```c
int OpusShellProfileString(const char *section, const char *key,
                           const char *deflt, char *out, int cbOut);
int OpusShellProfileInt(const char *section, const char *key, int deflt);
int OpusShellProfileWrite(const char *section, const char *key,
                          const char *value);
```

Tres funciones, traducción directa a `QSettings::value` y `setValue` con
`beginGroup(section)`. Sin caché propia, sin capa de esquema, sin migración:
`QSettings` ya resuelve formato y ubicación por plataforma. Confirmado como el
contrato completo; no hay nada más que diseñar aquí.

### B5.1 Implementación (Qt-2, paso 1) — cerrado

Lado shell implementado en `src/core/src/OpusShellConfig.cpp`, sub-proyecto
nativo `src/core/` (siempre gcc/g++ y Qt6 reales, nunca winegcc/wineg++;
construido bajo `OPUS_WINELIB_BUILD` vía `ExternalProject_Add`, mismo esquema
que los host tools de `src/port/tools/host/`). Al cerrar este paso, nada de
`Opus/` se tocaba todavía: los sitios de llamada seguían sobre
`GetProfileString`/`GetProfileInt`/`WriteProfileString`. La migración de
esos sitios es el trabajo de §B5.2, cerrado por separado.

**Prueba:** `src/core/src/OpusShellConfig_test.cpp`, registrada como
`opus_shell_config_test` en ctest. Verificado con `ctest -R
opus_shell_config_test` sobre el preset `linux-winelib-debug` real (no solo
en un build aislado): 9 verificaciones, todas en verde. `QSettings::setPath`
redirige a un directorio temporal antes de la primera llamada, así que la
prueba no toca la configuración real de quien la corre — confirmado
revisando `~/.config` después de correrla.

**Lo que la prueba cubre, y por qué así:** antes de escribirla se buscó en
el árbol de pruebas existente (`src/port/original/opus_*_test.c*`) algún
sitio que ya ejerciera estas tres funciones, tal como pedía el encargo. No
hay ninguno — cero coincidencias de `GetProfileString`/`GetProfileInt`/
`WriteProfileString` en los archivos de prueba actuales. No hay, entonces,
un comportamiento previo puntual que igualar; la prueba verifica la
implementación contra la semántica documentada del `Profile` Win16 que
`Opus/*.c` sigue llamando hoy:

- Ida y vuelta de cadena, con verdad sobre el valor por omisión cuando la
  clave no existe, y truncamiento correcto cuando el buffer de salida es
  más chico que el valor (mismo contrato que `cbMax` en `GetProfileString`).
- Ida y vuelta de entero, incluyendo negativos.
- La distinción que de verdad importaba verificar: `GetProfileInt` usa el
  valor por omisión **solo cuando la clave no existe**; si existe pero no es
  numérica, el valor real es `0`, no el valor por omisión. `QString::toInt()`
  exige la cadena completa como número válido y no reproduce eso, así que la
  implementación usa una conversión propia estilo `atoi` (`AtoiLike`,
  `OpusShellConfig.cpp`). La prueba fija este caso explícitamente para que
  una futura simplificación no lo pierda en silencio.

**Encontrado al revisar los call sites reales, no inventado:**
`Opus/print2.c:833` llama `GetProfileString(..., key = NULL, ...)` para
enumerar todas las claves de la sección `"devices"` de una sola vez —una
forma de uso que el contrato de tres funciones de §B5 no cubre. No se tocó
en el paso de migración porque exige ampliar el contrato, no solo traducir
la llamada.

### B5.2 Migración de call sites — cerrado (issue #2)

**Corrección sobre la cifra de B5.1**, hecha al migrar contra la tabla de
símbolos del reporte en vez de grep manual: `GetProfileString`(17) +
`GetProfileInt`(14) + `WriteProfileString`(11) = **42** sitios reales, no
43. El símbolo restante que el reporte agrupa bajo *Persistencia de
configuración* es `GetEnvironmentVariableA` (`dlgmisc.c:2145`), una API sin
relación con `WIN.INI`/`QSettings`, fuera de alcance de este contrato.
`profwin.c` no aparece en el inventario porque no está en
`OPUS_ORIGINAL_ENGINE_SOURCES` — no compila en este build, y sus 3 sitios
(`GetProfileIntPR`/`GetProfileStringPR`/`WriteProfileStringPR`, destino de
la redirección de `debugwin.h` bajo `DEBUG`, ninguno definido aquí) nunca
fueron parte del recuento. `print2.c:848` no es enumeración —tiene key real
(`pchPrinters`)—, a diferencia de `print2.c:833`: solo ese queda excluido.

**41 de 42 sitios migrados**, en 12 archivos: `ddesub.c`(1), `dlgmisc.c`(1),
`elmisc.c`(2), `fieldpic.c`(4), `filecvt.c`(6), `filewin.c`(3), `init2.c`(1),
`initwin.c`(7), `print2.c`(4 de 5), `quit.c`(8), `wproc.c`(2),
`debug/debugcmd.c`(2). Cada sitio queda envuelto en
`#if defined(__GNUC__) && !defined(_MSC_VER) / #else / #endif` por
`CONTRIBUTING.md`: la rama GNUC llama `OpusShellProfile*`, la rama MSVC
conserva la llamada original sin cambios — verificado por diff contra el
árbol previo, cero deltas más allá del whitespace incidental de retipeo.
`src/core/include` se agregó a `OPUS_ORIGINAL_INCLUDE_DIRS` para que el
`#include "OpusShellConfig.h"` resuelva.

**Verificación real, no solo la del target completo.** `ninja -k 0 -C
build/linux-winelib-debug opus_original_engine` no llega a 0 FAILED —pero
por el bloqueador ya documentado y preexistente a este trabajo:
`Opus/wordtech/disp.h:248` y `Opus/rsb.h:38,73`, miembro flexible de arreglo
bajo GCC 14.2, ajeno a esta migración. Para no dejar la verificación
colgada de ese bloqueador, cada uno de los 12 archivos se compiló también de
forma aislada con el comando real de `ninja -t commands`: los 12 llegan al
mismo punto (`ddesub.c`, `filewin.c` y `debug/debugcmd.c` compilan limpio de
punta a punta; los otros 9 fallan exactamente en `disp.h`/`rsb.h`, nunca en
código de este cambio). Cero errores y cero warnings mencionan
`OpusShellConfig`/`OpusShellProfile*` en ningún log.

**Estados de compilación condicional relevados, no asumidos:** de los 41
sitios, 6 están dentro de ramas hoy inactivas en este build —
`initwin.c:530` y `wproc.c:473,497` bajo `#ifdef DEBUG`/`HYBRID` (ninguno
definido), `initwin.c:578-579` bajo `#ifdef HYBRID`, `initwin.c:1136` bajo
el `#else` de `#ifdef OPUS_X64` (que sí está definido, así que ese `#else`
nunca compila), `init2.c:570` bajo `#ifdef MKTGPRVW`, y el bloque completo
de `debug/debugcmd.c` bajo `#ifdef DEBUG` además de no estar en ningún
target. Se migraron igual, por consistencia y para no dejar una mezcla de
API vieja y nueva si algún día se activan.

**Qué queda:** el cuarto issue para `print2.c:833` (ampliar el contrato con
una función de enumeración, o tratarlo aparte). La pregunta de si
`opus_shell_config` realmente enlaza contra un binario Winelib quedó
abierta al cerrar este paso — se resolvió por separado, ver más abajo.

---

## Reconocimiento Qt-3: candidatos limpios de `disp.h`/`rsb.h` sin sitio real

Tres TUs de `wordtech/` identificadas en el reconocimiento previo como
limpias de `disp.h`/`rsb.h` (directa y transitivamente, mismo método de
§B2.7/B4.4) pero sin asignar todavía a ninguna sección B:
`src/Opus/wordtech/sttb.c`, `src/Opus/wordtech/inssubs.c`,
`src/Opus/wordtech/prl.c`. El propósito de este reconocimiento era
encontrar el siguiente sitio real conectable mientras `disp.h`/GCC 14
sigue bloqueado. **Resultado: negativo en los tres — no hay ningún sitio
que requiera (ni tenga ya) un contrato del núcleo Qt.** Se documenta igual
que un resultado positivo, para no repetir la búsqueda.

Verificado por `git log --oneline --all -- <archivo>` que ninguno de los
tres fue tocado por trabajo de ninguna sección B previa (solo aparecen en
`a1c4a1f Initial commit`, y `inssubs.c` además en dos commits de la fase
de port original — `c40d4e0 Fase 3: compilar el motor a 0 errores` y
`27a2f60 Full operating system font support` — ninguno de los dos toca
`MessageBox`/`Global*`/GDI).

### `src/Opus/wordtech/sttb.c`

Cuatro familias de grep, cada una vacía:

```
$ git grep -n 'MessageBox' -- src/Opus/wordtech/sttb.c
$ git grep -n 'MessageBeep' -- src/Opus/wordtech/sttb.c
$ git grep -nE 'Global[A-Z][a-zA-Z]*' -- src/Opus/wordtech/sttb.c
$ git grep -nE 'GetDC|ReleaseDC' -- src/Opus/wordtech/sttb.c
$ git grep -nE 'HFONT|SelectObject|CreateFont|GetTextMetrics|GetTextExtent|GetCharWidth' -- src/Opus/wordtech/sttb.c
```

(sin salida en las cinco)

Sí tiene una familia de API real, encontrada al inspeccionar el archivo:
`HqAllocLcb`/`FreeHq`/`UnlockHq`/`HpOfHq` sobre el tipo `HQ`, 8 sitios
(líneas 70, 111, 131, 177, 286, 494, 546, 736).

| Sitio (línea) | API | Contrato existente/faltante | Complejidad |
|---|---|---|---|
| 70, 111, 131, 177, 286, 494, 546, 736 | `HQ`/`HqAllocLcb`/`FreeHq`/`UnlockHq`/`HpOfHq` | **No hace falta ninguno.** `HqAllocLcb` no es Win16 `GlobalAlloc` — es un macro ya resuelto por el port x64 (`src/port/original/opus_x64_heap.h:41`, `#define HqAllocLcb(cb) ((HQ)OpusHAllocateCb((size_t)(cb)))`), respaldado por un allocator nativo propio (`OpusHAllocateCb`/`OpusDerefH`, handles pointer-a-puntero estables, sin relación con memoria segmentada Win16). Fuera del alcance de B3: B3 son los ~201 sitios de `Global*`/`GMEM_*` reales (`GlobalAlloc`/`GlobalLock`/`GlobalFree` de la API Win32), no el `HQ` interno de Opus, que ya es portable tal cual. | **N/A — no es trabajo pendiente.** Ya ported, nada que sustituir. |

`sttb.c` es un candidato disp.h-limpio, pero no un candidato de *trabajo*:
no queda ningún sitio Win16/GDI real dentro del archivo.

### `src/Opus/wordtech/inssubs.c`

Mismas cinco familias, todas vacías:

```
$ git grep -n 'MessageBox' -- src/Opus/wordtech/inssubs.c
$ git grep -n 'MessageBeep' -- src/Opus/wordtech/inssubs.c
$ git grep -nE 'Global[A-Z][a-zA-Z]*' -- src/Opus/wordtech/inssubs.c
$ git grep -nE 'GetDC|ReleaseDC' -- src/Opus/wordtech/inssubs.c
$ git grep -nE 'HFONT|SelectObject|CreateFont|GetTextMetrics|GetTextExtent|GetCharWidth|CreateDC' -- src/Opus/wordtech/inssubs.c
$ git grep -nE 'HqAllocLcb|LpLockHq|UnlockHq|FreeHq|HAllocateCw|HAllocateCb' -- src/Opus/wordtech/inssubs.c
```

(sin salida en las seis)

Inspección adicional (barrido heurístico de toda llamada con mayúscula
inicial, descartando las funciones internas de Opus obviamente propias)
no encontró ninguna otra API Win16/GDI — el archivo es lógica de inserción
de campos/números de página y conversión de bytes de archivo
(`ReadRgchFromFn`, `WriteRgchToFn`, `PnAlloc`, `MapStc`, etc.), toda
interna a Opus, ninguna cruza a Win32.

| Sitio (línea) | API | Contrato existente/faltante | Complejidad |
|---|---|---|---|
| — | — | — | **No hay sitios.** Nada que conectar. |

### `src/Opus/wordtech/prl.c`

Mismas seis familias, todas vacías:

```
$ git grep -n 'MessageBox' -- src/Opus/wordtech/prl.c
$ git grep -n 'MessageBeep' -- src/Opus/wordtech/prl.c
$ git grep -nE 'Global[A-Z][a-zA-Z]*' -- src/Opus/wordtech/prl.c
$ git grep -nE 'GetDC|ReleaseDC' -- src/Opus/wordtech/prl.c
$ git grep -nE 'HFONT|SelectObject|CreateFont|GetTextMetrics|GetTextExtent|GetCharWidth|CreateDC' -- src/Opus/wordtech/prl.c
$ git grep -nE 'HqAllocLcb|LpLockHq|UnlockHq|FreeHq|HAllocateCw|HAllocateCb' -- src/Opus/wordtech/prl.c
```

(sin salida en las seis)

Mismo barrido heurístico: solo funciones internas de Opus sobre `PRL`/tabs
(`AddPrlSorted`, `ApplyPrlSgc`, `ApplySprm`, `DeleteTabs`, etc.) más el
macro ya portado `LpFromHp` (mismo `opus_x64_heap.h` que en `sttb.c`, no
un sitio nuevo). Ninguna API Win16/GDI.

| Sitio (línea) | API | Contrato existente/faltante | Complejidad |
|---|---|---|---|
| — | — | — | **No hay sitios.** Nada que conectar. |

### Conclusión de este reconocimiento

Ninguno de los tres es un candidato de trabajo Qt-3 viable, a pesar de
estar limpios de `disp.h`/`rsb.h`: `sttb.c` solo toca memoria ya portable
(`HQ`, fuera del alcance de B3), y `inssubs.c`/`prl.c` no tienen ninguna
superficie Win16/GDI en absoluto. El siguiente sitio real conectable
mientras `disp.h`/GCC 14 siga bloqueado no está entre estos tres — hace
falta repetir la búsqueda sobre otro subconjunto de la lista de candidatos
limpios ya inventariada (`src/Opus/debug/debugdde.c`, `debugdlg.c`,
`debuggdi.c`, `debugrep.c`, `debugwin.c`, o el resto de TUs limpias de
`Opus/` raíz listadas en el reconocimiento anterior), no asumir que
"limpio de `disp.h`" implica "tiene trabajo pendiente".

---

## Verificación de la frontera física: ¿enlaza de verdad?

Hasta B5.2 solo se había confirmado que `opus_shell_config` compila de
forma aislada (§B5.1) y que los 41 call sites compilan bajo GNUC dentro de
`opus_original_engine`, una biblioteca **estática** que no fuerza la
resolución de símbolos externos. Nunca se había probado el enlace real:
un binario Winelib (winegcc/wineg++) enlazando contra una biblioteca nativa
gcc/g++ que depende de Qt6. Esa prueba es la que sostiene la arquitectura
de frontera completa, no solo el contrato de configuración — si no
enlazara, los otros tres contratos (B2, B3, B4) heredarían el mismo
problema el día que se implementen.

**Veredicto: enlaza, con dos ajustes menores, ninguno estructural.**
Sonda en `docs/port-qt/scripts/link-check/` (`link_check.c`, compilado con
winegcc, llamando a `OpusShellProfileWrite`/`OpusShellProfileString` reales
contra la `libopus_shell_config.a` que produce `opus_core_build`).

1. **`-fPIC` en la biblioteca nativa.** El primer intento de enlace directo
   dio `relocation R_X86_64_PC32 ... can not be used when making a shared
   object; recompile with -fPIC`. Causa, verificada con `winegcc -v`: el
   paso final de enlace de winegcc es literalmente
   `gcc -m64 -shared -Wl,-Bsymbolic -o foo.exe.so ...` — así arma Winelib
   sus "ejecutables" (el `.exe` es un stub que Wine carga, el código real
   vive en `.exe.so`). Un binario `-shared` no admite objetos sin código
   independiente de posición. No es un detalle de header: es cómo Winelib
   construye binarios, con o sin Qt de por medio. Ajuste: `opus_shell_config`
   pasa a compilarse con `POSITION_INDEPENDENT_CODE ON`
   (`src/core/CMakeLists.txt`), con nota para que los tres contratos
   restantes hereden la misma propiedad cuando se implementen.
2. **`-lstdc++` explícito, solo con `winegcc` puro.** Con `-fPIC` aplicado,
   el segundo intento dio `undefined reference to __gxx_personality_v0`
   (la rutina de manejo de excepciones de C++). `winegcc` es un driver de
   C: no enlaza `libstdc++` por defecto, y `opus_shell_config.cpp` es C++
   (usa `QString`, que internamente puede lanzar). Con `-lstdc++` agregado
   al comando de enlace, resuelve. **Dato adicional que reduce el impacto
   real de este punto:** `WORD1` no enlaza con `winegcc` sino con
   `wineg++` — lo obliga `target_compile_features(WORD1 PRIVATE
   cxx_std_20)`, porque el target ya mezcla fuentes `.cpp`
   (`port/original/opus_asm_*.cpp`, etc.). Probado explícitamente: con
   `wineg++` como enlazador, el enlace funciona **sin** agregar
   `-lstdc++` a mano, porque el driver de C++ ya lo enlaza por diseño. El
   ajuste con `-lstdc++` sigue documentado por si algún target futuro usa
   `winegcc` puro para enlazar contra estas bibliotecas.

**Prueba de ejecución, no solo de enlace.** El binario resultante corre
bajo Wine y ejecuta la llamada real: `OpusShellProfileWrite("LinkCheck",
"Saludo", "hola desde winegcc")` seguido de `OpusShellProfileString` sobre
la misma clave devuelve `cch=18`, valor `"hola desde winegcc"` — la cadena
completa, intacta, de vuelta a través de la frontera. Confirmado con los
dos enlazadores (`wineg++` y `winegcc + -lstdc++`) y con la biblioteca real
que construye `opus_core_build`, no una copia de scratch.

**Descartados por no ser la causa:** mangling de C++ (los cuatro headers
`OpusShell*` ya declaraban `extern "C"` desde que se escribieron, verificado
antes de tocar nada) y convención de llamada/ABI Win32 (el objeto que
produce `winegcc -c` es ELF x86-64 SysV estándar, igual que el de `gcc`;
Winelib no cambia la ABI de llamada en modo x64, solo provee los headers y
tipos de Win32 — la única fricción real fue de generación de código
(`-fPIC`) y de runtime enlazado (`libstdc++`), no de ABI de llamada).

**Consecuencia para B3/B4/B2:** la arquitectura de frontera —núcleo nativo
Qt6/gcc + shell winegcc/wineg++ enlazados en el mismo binario— queda
confirmada, no solo asumida. Los tres contratos que faltan implementar
pueden proceder sobre este mismo esquema sin rediseño; cuando cada uno
tenga su primera implementación, aplicar `POSITION_INDEPENDENT_CODE ON` a
su biblioteca en `src/core/CMakeLists.txt` desde el primer commit, y si el
target consumidor llega a enlazar con `winegcc` puro en vez de `wineg++`,
agregar `-lstdc++` a su línea de enlace.

---

## Secuencia recomendada para Qt-2

El orden no es arbitrario: cada paso deja verificable el siguiente.

1. **Configuración (B5) — cerrado.** 42 sitios reales (no 43, ver §B5.2),
   contrato trivial. Sirvió para establecer el mecanismo de frontera —cómo
   el núcleo llama al shell— sobre algo cuyo fallo es visible al instante y
   cuyo riesgo es nulo. 41 migrados; 1 (`print2.c:833`, enumeración) espera
   una extensión del contrato en issue aparte.
2. **Tabla de sustitución de fuentes (§B2.5).** Extraer del oráculo el mapeo
   real de los nombres de época a archivos físicos. Es barato, es un
   prerrequisito de cualquier prueba de fidelidad, y hoy no está escrito en
   ninguna parte.
3. **Memoria (B3) — implementación y verificación cerradas, migración de
   call sites pendiente.** El allocator opaco existe
   (`src/core/src/OpusShellMemory.cpp`) y probó, con un handle real, que
   sobrevive Alloc/Lock/escritura/Unlock/Lock/Free a través de
   winegcc/wineg++ (§B3.5). Falta sustituir los 201 sitios de `Global*` en
   `Opus/` por `OpusMem*` — issue previo por `CONTRIBUTING.md`, igual que
   configuración.
4. **Enumeración de handles serializados (§B3.3) — cerrada.** Ninguna
   estructura persistida tiene campo handle. No quedó como inventario
   pendiente: se hizo antes de diseñar el header, no después.
5. **Medición de texto (B2) — implementación inicial cerrada, verificada
   con 2660 puntos de dato, no solo uno.** `src/core/src/
   OpusShellFontMetrics.cpp` implementa el contrato con la estrategia de
   §B2.3. `opus_shell_font_metrics_fidelity_test` compara contra una
   tabla capturada del oráculo Winelib real
   (`docs/port-qt/scripts/fidelity/capture.py` →
   `opus_shell_font_metrics_oracle_table.h`): 4 nombres de época × 7
   tamaños (8-36pt) × 95 caracteres ASCII imprimibles = 2660 anchos.
   **2660/2660 coinciden exactamente** con el oráculo -- no aproximado,
   no "cerca". Ascenso/descenso, que §B2.3 no cubría, se comparan también
   (±1px, redondeo distinto de `ascent()`/`descent()` de Qt contra
   `tmAscent`/`tmDescent` enteros de GDI). Cubre los 4 `ftc` conocidos,
   peso regular únicamente (`catr != 0` falla controlado -- GDI sintetiza
   negrita/cursiva, `QRawFont` no), pantalla a 96 ppp fija (sin
   impresora). Sigue siendo la pieza de la que depende la restricción de
   fidelidad, y la que hace que `wordtech/` pueda compilar sin GDI.
5b. **Primer llamador real conectado (§B2.7).** `Opus/LOADFONT.C:187
   C_LoadFcid`, camino de pantalla/paso variable, llama a
   `OpusShellCharWidths` en vez de GDI. Tests de `src/core` en verde
   (incluida la fidelidad de 2660 puntos), pero sin verificación en
   ejecución contra `WORD1` real -- dos bloqueadores independientes de
   este trabajo lo impiden (compilación de `Opus/wordtech/disp.h` bajo
   GCC 14, arranque de `WORD1` ya roto de antes). `scroll.c`/`disp3.c`/
   `pagevw.c` siguen sin desbloquear: el criterio del documento pide
   verificación contra layout real, no solo conexión de tipos.
6. **`error.c`, luego `editspec.c` y `undo.c` (§B4.3) — contrato
   implementado.** `src/core/src/OpusShellSpine.cpp`:
   `OpusShellReportError` (`QMessageBox::Critical`,
   `Qt::ApplicationModal` -- equivalente real de `MB_SYSTEMMODAL`) y
   `OpusShellAlert` (`QApplication::beep()`). Probado con un diálogo modal
   real, auto-cerrado desde `QTimer::singleShot` una vez que
   `QApplication::activeModalWidget()` lo confirma activo -- no un stub.
   No conectado todavía a ningún call site de `Opus/` (esos tres archivos
   siguen sin migrar, siguen usando `MessageBox`/`MessageBeep` reales) ni
   a `opus_qt_shell` (deliberado: un diálogo modal disparado
   automáticamente en cada arranque del andamiaje sería ruido, no una
   comprobación útil).
7. **Inversión del bucle de mensajes (§B4.1) — patrón demostrado,
   adelantado fuera de orden por decisión explícita del mantenedor
   2026-08-11 (B2 verificado de forma aislada, no contra `wordtech/`
   real; riesgo aceptado a sabiendas, no un descuido).** `opus_qt_shell`
   corre bajo el bucle de `QApplication` -- sin `GetMessage`/
   `DispatchMessage` en ningún punto del binario -- y ya llama hacia
   dentro con los dos patrones de despacho de §B4.2: menú "Despacho
   (B4.2)", acción directa (`SendMessage` → llamada síncrona en el mismo
   ciclo) y acción diferida (`PostMessage` →
   `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, corre en un
   ciclo posterior). Una bitácora en pantalla hace la diferencia
   observable, no solo afirmada. **Lo que esto NO es:** la inversión de
   `Opus/wproc.c` -- sigue siendo el conductor real del motor de
   documento, y `Opus/` es árbol restringido, no tocado. Este es el
   molde de despacho a reutilizar el día que `wordtech/` se conecte, no
   la migración en sí. El oráculo Winelib sigue siendo necesario para
   verificar fidelidad; adelantar este paso no lo reemplaza ni lo
   invalida, solo dejó de bloquear en seco a la espera de B2 contra
   documento real.

---

## Preguntas abiertas

Las dos que este documento tenía sobre fidelidad quedaron cerradas en §B2.3 y
§B2.5. Las dos que quedaban tras esa ronda —ubicación de la API de frontera y
el veredicto de `opustlbx/`— quedan cerradas aquí:

1. **Ubicación de la API de frontera: `src/core/include/`.** No `port/`: ese
   directorio es andamiaje de compatibilidad temporal (LP64 sobre Winelib,
   build de host), semánticamente distinto de la API estable del núcleo
   nuevo. Los cuatro headers de contrato (`OpusShellFontMetrics.h`,
   `OpusShellMemory.h`, `OpusShellSpine.h`, `OpusShellConfig.h`) viven ahí; ver
   inventario de headers en la sección siguiente.
2. **`opustlbx/` resuelto: excluir, no incluir.** Sí hay relación real con
   `port/original/toolbox.h` — el propio comentario de cabecera de
   `toolbox.h` lo dice: es el sucesor escrito a mano del `toolbox.h`
   *generado*, y `opustlbx.c` es precisamente ese generador (lee
   `Opus/resource/toolbox.txt` y emite el `.h` con el mecanismo `tlbx` de
   llamada lejana entre segmentos más el `.asm` con la tabla `mptlbxpfn` /
   `tlbxMac` que consumen `Opus/asm/int3f.asm` y `CkTlbx` en
   `Opus/debug/debugstr.c`). Pero la relación es de linaje textual, no de
   acoplamiento activo: `opustlbx` no tiene target en CMake, no genera nada
   que el build actual use, y lo que genera pertenece al mecanismo de
   llamada de `Opus/asm/`, ya fuera de alcance de esta rama. `toolbox.h` sí
   es capa activa del port —lo incluyen `Opus/windows.h` y trece TUs de
   `wordtech/`/`interp/` directamente— pero eso no arrastra a `opustlbx`
   consigo: el sucesor no depende del generador de su predecesor.
   Consecuencia: `opustlbx.c`/`.h` pasan de «diferir» a «excluir» en el
   triage; `cashmere/fldexp/` (4 archivos) se mantiene «diferir» sin cambios,
   no apareció nada en esta investigación que lo justifique. Triage cerrado:
   4 diferir, 54 excluir.

   Nota lateral, no una tarea nueva: `CkTlbx` (`Opus/debug/debugstr.c:2039`)
   referencia `tlbxMac`/`mptlbxpfn`, símbolos definidos solo en
   `Opus/asm/int3f.asm`. `Opus/debug/` está en alcance por decisión de
   proyecto y `Opus/asm/` no; esa TU ya tenía un hueco de enlace conocido
   antes de esta investigación, independiente del veredicto de `opustlbx`.

**Nueva, abierta, no resuelta en este documento — encontrada al hacer
Fase 1 de B3 (§B3.3), no buscada a propósito:**

3. **§B2.1 describe la estructura equivocada. RESUELTO 2026-08-11 — no invalida
   §B2.2/§B2.3, corrige la narrativa.** La cita central del contrato de
   medición de texto (`Opus/wordtech/format.h:379-410`,
   `dxuFrac`/`bmpchdxu`/`struct FONTREC far * far *qqftr`) es código
   `#ifdef MAC`, muerto en este build. El `struct FTI` real que se compila
   bajo `WIN`/`OPUS_X64` está en `Opus/fontwin.h:126-152`, y su gemelo de
   caché es `struct FCE` (`Opus/fontwin.h:96-124`, primeros
   `cbFtiFceSame` bytes idénticos a `FTI` por diseño). Ninguno de los dos
   tiene acumulador de fracción — **la tabla de anchos real no vive
   inline en `FTI`, vive en `FCE.hqrgdxp`**: un handle Win16 (`HQ`, la
   misma familia de handle que el contrato B3 ya cubre) a un array de
   **256 `int`** (uno por valor de byte 0-255, ancho ya en píxeles
   enteros, sin fracción), asignado con `HqAllocLcb(256*sizeof(int))`
   (`Opus/initwin.c:2946`). El consumidor de layout no lee `FTI`/`FCE`
   directamente carácter a carácter: copia anchos a `vfli.rgdxp[]`
   (`struct` de resultado de línea formateada, `Opus/disp1.c:761` y
   alrededores) durante `FormatLine`, y esa copia sí es aritmética entera
   plana, sin fracción — confirma, no contradice, la ausencia de
   acumulador.

   **Consecuencia sobre la estrategia de §B2.3: ninguna.** La estrategia
   (ppem entero + `QFont::PreferFullHinting`, medida contra el
   comportamiento observable de `GetTextExtent`/`GetTextMetrics`) nunca
   dependió del layout interno de `FONTREC`/`FTI` — se validó contra el
   *comportamiento* de GDI, no contra una estructura de datos. Un array
   de 256 enteros en píxeles es, si acaso, más simple de rellenar que el
   modelo con acumulador de fracción que §B2.1 describía: no cambia qué
   pide `OpusShellCharWidths` (avances enteros), solo el nombre de la
   estructura interna de destino, que el contrato ya no expone.

   **Consecuencia sobre el contrato (`OpusShellFontMetrics.h`):
   `OpusFontMetrics`/`OpusShellFontMetrics()` siguen intactos** (ascenso,
   descenso, overhang — los mismos campos existen en `FCE`/`FTI` con los
   mismos nombres `dypAscent`/`dypDescent`/`dxpOverhang`).
   `OpusShellCharWidths(key, chFirst, cch, rgdxu)` **sigue siendo la
   firma correcta**, pero en este build el llamador real siempre pedirá
   `chFirst=0, cch=256` (el rango completo de `FCE.hqrgdxp`) — no hace
   falta soporte de rango parcial en la primera implementación, aunque el
   contrato ya lo permite si algún consumidor futuro lo necesitara.
   **Nuevo, no cubierto por el contrato de hoy:** `FCE`/`FTI` tienen campos
   que `OpusFontMetrics` no expone (`dypXtraAscent`, `fVisiBad`, `fPrvw`,
   `dxpBorder`/`dypBorder`, `dxpExpanded`) — no se sabe todavía cuáles lee
   el motor de layout fuera de ascenso/descenso/overhang/anchos; auditar
   antes de dar la primera implementación de `OpusShellFontMetrics()` por
   completa.

   **Localizado 2026-08-11: `C_LoadFcid()` en `Opus/LOADFONT.C:187` (bajo
   `#if defined(DEBUG) || defined(OPUS_X64)` — vivo en este build).** Es
   la función de carga/caché de fuente completa (comentario de cabecera:
   "last `ifceMax` fonts requested through LoadFcid are kept in a LRU
   cache"), no algo escondido en `dispspec.c`. Camino de relleno real
   para fuente de paso variable, `LOADFONT.C:391-434`:

   1. `CreateFontIndirect(&lf)` (`:315`) selecciona la fuente física.
   2. `GetTextMetrics(hdc, &tm)` (`:340`).
   3. `pfce->hqrgdxp = HqAllocLcb(256 * sizeof(int))` (`:398`, rama
      `OPUS_X64`) — el mismo array de 256 `int` ya identificado.
   4. Relleno bulk: `GetCharWidth(hdc, chDxpMin, chDxpMax-2, lpdxp)`
      (`:421`, API Win32 real, no una función del proyecto) para los
      caracteres 0-253; el 254 se rellena aparte
      (`OurGetCharWidth(hdc, chDxpMax-1, chDxpMax-1, ...)`, `:426`).
      Dos casos caen al *fallback* carácter-por-carácter en vez del bulk:
      modo vista previa (`vfPrvwDisp`, `:415-416`) siempre, y cualquier
      driver que falle `GetCharWidth` (`:421-425`, con
      `ReportSz("Driver does not support GetCharWidth!")`).
   5. **`OurGetCharWidth()` (`LOADFONT.C:976-991`) es literalmente el
      bucle `GetTextExtentPoint32A(hdc, &ch, 1, &size)` carácter por
      carácter que la sonda de §B2.3 ya reproduce** — mismo mecanismo, no
      uno análogo. Para pantalla (no impresora, no preview) todo pasa por
      `GetCharWidth`, no por este fallback, pero ambos son wrappers finos
      sobre la misma medición de GDI subyacente.
   6. Corrección de overhang: si `pfce->dxpOverhang != 0`, se resta de
      **toda** la tabla ya rellenada (`:428-434`), una sola vez sobre el
      array — no por-carácter durante el relleno. Neto idéntico al
      `GetTextExtent(hdc,&ch,1) - tm.tmOverhang` por-carácter que usa la
      sonda de §B2.3 (resta distribuye sobre suma), así que **la
      estrategia medida en §B2.3 ya reproduce este paso sin cambio
      adicional** — no hay una corrección de overhang nueva que
      incorporar.
   7. `LLoadFce:` (`:523`) es el punto de convergencia con el camino de
      restauración desde stream (`initwin.c:2909-2965`): copia
      `pfce->hqrgdxp` a `pfti->rgdxp[256]` vía `bltbh` (`:544-547`) sea
      cual sea el origen del array. Fuente de paso fijo: sin tabla,
      `pfce->dxpWidth = tm.tmAveCharWidth` (`:386`) replicado a las 256
      posiciones con `SetWords` (`:539`).

   **Consecuencia:** cerrado. §B2.3 no necesita ajuste — midió
   exactamente el mecanismo que `C_LoadFcid` usa (GDI per-char / bulk
   `GetCharWidth`, mismo neto tras overhang). El único hallazgo nuevo,
   real: **`OpusFontKey.ftc` no basta como clave de entrada del contrato
   sin la tabla `ftc → nombre de época` que hoy solo existe dentro de
   `Opus/initwin.c` (`vhsttbFont`, orden verificado: `ibstFontDefault` =
   Tms Rmn, `+1` Symbol, `+2` Helv, `+3` Courier —
   `Opus/initwin.c:1541-1583`).** `OpusShellFontMetrics()`/
   `OpusShellCharWidths()` tal como están declaradas reciben `ftc` (un
   entero sin significado fuera de esa tabla), pero el shell
   (`OpusShellFontSubstitution`) solo conoce nombres de época (cadenas).
   Antes de escribir la implementación real hace falta decidir: (a) el
   núcleo traduce `ftc → nombre` antes de llamar al shell (el contrato ya
   dice "el núcleo traduce", §B2.2, así que esto puede ser tan simple
   como añadir esa tabla de 4 entradas al lado núcleo del wrapper, no al
   header de frontera); o (b) el contrato cambia para recibir el nombre
   directamente. (a) no requiere tocar `OpusShellFontMetrics.h`.

4. **¿Deberían el núcleo Qt (`src/core`) y el port Winelib vivir en
   repositorios separados, para que el núcleo termine siendo una
   aplicación Qt de verdad? Preguntada por el mantenedor 2026-08-14 — no
   se separa por ahora:**

   Hoy `src/core` no es una aplicación: son cinco bibliotecas estáticas
   angostas (`opus_shell_config`/`memory`/`font_substitution`/
   `font_metrics`/`spine`) cuya única razón de existir es enlazarse dentro
   de `WORD1` (Winelib) y ser llamadas desde call sites dentro de `Opus/`
   — árbol restringido de este mismo repo (ver "Estado real de hoy"
   arriba). Separar ahora movería la mitad del acoplamiento — los headers
   de contrato, el `ExternalProject_Add(opus_core_build ...)` de
   `src/CMakeLists.txt` — a través de un límite de repositorio sin
   eliminarlo: `Opus/` seguiría necesitando enlazar contra ese código en
   cada build de `WORD1`, ahora vía submódulo o paquete instalado en vez
   de un subdirectorio del mismo checkout. Y la estrategia de fidelidad de
   §B2.3/§B2.5 depende de capturar comportamiento real del oráculo
   Winelib (`docs/port-qt/scripts/fidelity/capture.py` →
   `opus_shell_font_metrics_oracle_table.h`): la tabla ya capturada viaja
   bien entre repos (es un header generado, no un binario), pero
   regenerarla cuando cambie la versión de Wine exige tener el oráculo
   Winelib al lado, en el mismo checkout o en uno hermano sincronizado a
   mano.

   La separación empieza a tener sentido el día que exista un ejecutable
   Qt que ya no dependa de `Opus/`/Winelib para nada — es decir, cuando el
   paso 7 de "Secuencia recomendada para Qt-2" deje de ser demostración
   (`opus_qt_shell`, sin motor de documento) y pase a ser la migración
   real de `Opus/wproc.c`. Antes de eso, separar repos no compra
   independencia: solo cambia dónde vive el mismo acoplamiento, y agrega
   fricción de versión cruzada (¿qué commit de `src/core` fija cada commit
   de `WORD1`?) que hoy resuelve gratis un solo `git log` sobre un único
   árbol.

   No cerrado para siempre — reabrir cuando `opus_qt_shell` (o un sucesor)
   tenga motor de documento propio y dependa de `Opus/` en cero sitios, no
   antes.

   **Addendum, mismo día:** el hallazgo de que `WORD1.exe.so` ya enlaza en
   runtime contra `libQt6Widgets/Gui/Core/DBus.so.6` (ver "Estado real de
   hoy" arriba) refuerza esta recomendación en vez de cambiarla — el
   acoplamiento entre `src/core` y `WORD1` no es solo de build
   (`ExternalProject_Add`) sino de carga en cada arranque del binario. Eso
   es más razón, no menos, para no partirlo en dos repos mientras ese
   enlace exista.

---

## Bloqueador de build: `disp.h:248` en GCC 14 (VPS Debian) — solo diagnóstico

**Estado real: reproducido y caracterizado, no resuelto.** No es un bug de este
fork: reproducido también con `git stash` antes de cualquier cambio propio, así
que es preexistente al trabajo de esta rama. No reproducible en Fedora
44/GCC 16.1.1 (sin acceso a esa máquina en esta sesión — dato reportado, no
reverificado aquí). No se tocó `src/Opus/` ni `src/OpusEtAl/`: solo lectura,
compilación de prueba aislada y búsqueda.

### El código exacto

`Opus/wordtech/disp.h:235-250`:

```c
struct PLDR
	{
	int     idrMac;
	int     idrMax;
	int     cbDr;   /* set to cbDR */
	int     brgdr;  /* set to point to rgdr */
	int	fExternal;
	struct PLDR **hpldrBack;
	int     idrBack;
	struct PT ptOrigin;
	int     dyl;
	union   {
		HQ	hqpldre;    /* when fExternal true */
		struct DR rgdr[];   /* when fExternal false */
		};
	};
#define cwPLDR   (sizeof(struct PLDR) / sizeof(int))
```

La construcción es un flexible array member (`struct DR rgdr[]`) como miembro
de una `union`, junto a `HQ hqpldre`. Presente desde el commit inicial del
fork (`a1c4a1f`, `git blame` no muestra ningún commit posterior tocando estas
líneas) — no es una regresión introducida en esta rama.

### El error, literal

Compilación real vía `cmake --build --preset linux-winelib-debug --target
opus_original_engine`, disparado por `port/original/opus_x64_layout.c:6`
(`#include "wordtech/disp.h"`):

```
/home/pablo/msword/src/Opus/wordtech/disp.h:248:27: error: flexible array member in union
  248 |                 struct DR rgdr[];   /* when fExternal false */
      |                           ^~~~
winegcc: /usr/bin/gcc failed
```

Flags reales de esa TU (capturados con `ninja -t commands`):

```
winegcc -DCRLF -DNOMINMAX -DNONATIVE -DOPUS_X64 -DWIN -DWIN23 [...] \
  -g -std=gnu89 -funsigned-char -fms-extensions -fpermissive -MD [...] \
  -c src/port/original/opus_x64_layout.c
```

**Dato clave: `-fms-extensions` ya está activo en el build real** (línea de
CMake existente) y el error persiste. No es un `-Werror` — es un `error:`
directo del front-end de C, sin prefijo `[-W...]`, así que ningún flag de
warning-a-error lo controla.

### Causa exacta — confirmada, no supuesta

Reproducido aislado (`gcc -std=gnu89 -fms-extensions -fpermissive`, mismo
resultado). Con `-pedantic-errors` el mismo caso también dispara, por
separado, `ISO C90 does not support flexible array members [-Wpedantic]` —
pero ese es un diagnóstico *distinto* (gateable por `-Wpedantic`); el que
realmente bloquea el build (`flexible array member in union`, sin sufijo
`-W`) no aparece bajo ningún nombre de warning: es un `error_at()`
incondicional en el front-end de C de GCC 14, en `c/c-decl.cc` (mensaje
localizado en `#: c/c-decl.cc:9556` en el árbol fuente de
`gcc-14_14.2.0-19.debian.tar.xz`, confirmado descargando el paquete fuente
Debian real, no por inspección de memoria).

**Por qué GCC 16 no lo reproduce — confirmado por búsqueda, no supuesto:**
GCC aceptó oficialmente PR53548 ("allow flexible array members in unions")
como commit `r15-209` (rama de desarrollo de GCC 15, mayo 2024), que
convirtió este `error_at()` incondicional en un `pedwarn` (advertencia
pedante, no error duro) y documentó la construcción como extensión soportada
("Flexible Array Members in Unions" en la documentación oficial de GCC:
<https://gcc.gnu.org/onlinedocs/gcc/Flexible-Array-Members-in-Unions.html>,
confirma "GCC permits a C99 flexible array member (FAM) to be in a union").
Debian `gcc-14.2.0` es anterior a ese cambio (GCC 14 se ramificó antes de
mayo 2024); Fedora 44 con GCC 16.1.1 lo hereda. **Es una diferencia de
versión del compilador, no de flags ni de modo de lenguaje** — por eso
ningún flag de `-std=`/`-fms-extensions`/`-fplan9-extensions` lo mueve: el
soporte no está condicionado a un flag, está condicionado a que el
front-end tenga el parche aplicado.

Referencias usadas (búsqueda web, no memoria del modelo):
- <https://gcc.gnu.org/pipermail/gcc-cvs/2024-May/401756.html> — commit
  `r15-209`, "C and C++ FE changes to support flexible array members in
  unions and alone in structures."
- <https://www.mail-archive.com/gcc-patches@gcc.gnu.org/msg364477.html> —
  serie de parches posteriores (PR119001, febrero 2025) afinando casos de
  inicialización, confirma que el soporte sigue activo y evolucionando en
  ramas posteriores a GCC 15.
- <https://gcc.gnu.org/onlinedocs/gcc/Flexible-Array-Members-in-Unions.html>

### Flags probados — ninguno resuelve, en este orden

Repro aislado (`/tmp/.../repro.c`, mismo patrón exacto: `union { HQ; struct
DR rgdr[]; }`), cada uno con salida literal idéntica (`error: flexible array
member in union`, exit 1):

| Flag probado | Resultado |
|---|---|
| `-fms-extensions` (ya activo en el build real) | falla |
| `-fplan9-extensions` | falla |
| `-std=gnu17 -fms-extensions` | falla |
| `-std=gnu2x -fms-extensions` | falla |
| `-Wno-error=pedantic -fms-extensions` | falla (confirma que no es un `-Werror`; el error no tiene tag `-W`, así que no hay warning que degradar) |

Ninguno de los cinco cambia el resultado. Esperable dado lo anterior: el
`error_at()` de GCC 14 es incondicional, no depende de dialecto de C ni de
extensión GNU/MS/Plan9 activada — solo del parche de front-end que llegó en
GCC 15.

### Candidato de código mínimo — NO aplicado, solo propuesto para revisión

No hay flag de compilador que resuelva esto en GCC 14. El cambio mínimo
sería en `disp.h`, guardado como exige la disciplina del proyecto para
código Linux-only dentro de `Opus/`:

```diff
--- a/src/Opus/wordtech/disp.h
+++ b/src/Opus/wordtech/disp.h
@@ -244,7 +244,11 @@ struct PLDR
 	union   {
 		HQ	hqpldre;    /* when fExternal true */
+#if defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)
+		struct DR rgdr[1];  /* when fExternal false -- GCC <15 rejects FAM in union (PR53548, r15-209) */
+#else
 		struct DR rgdr[];   /* when fExternal false */
+#endif
 		};
 	};
```

Por qué `rgdr[1]` y no otra cosa: no es un patrón nuevo importado — es
exactamente el idiom que ya usa `struct WWD` en el mismo archivo
(`disp.h:471`, comentario original `/* WWD is a pldr */`), para el mismo
propósito (tail de `struct DR` de tamaño variable tras un header fijo).
Verificado antes de proponerlo: `cwPLDR` (única macro que depende de
`sizeof(struct PLDR)`, `disp.h:251`) no tiene ningún sitio de uso en
`src/Opus/**/*.c` (`grep` vacío) — así que el único efecto observable de
pasar de `rgdr[]` a `rgdr[1]` (que `sizeof(struct PLDR)` crezca en
`sizeof(struct DR)`) no llega a ningún cálculo de asignación real. Todo el
acceso real al array es vía puntero (`&(pwwd)->rgdr[0]` en el macro
`PdrGalley`, mismo archivo) o vía offsets calculados a mano con `cbDr`, no
vía `sizeof` de la struct completa — el patrón pre-C99 "struct hack" que
`rgdr[1]` implementa es semánticamente neutro aquí, no solo "compila".

**No implementado.** Pendiente autorización explícita (issue de GitHub) para
tocar `src/Opus/`, por disciplina del proyecto.

### Aplicado y verificado 2026-08-12 — autorizado explícitamente

El cambio anterior se aplicó tal cual (misma guarda, mismo idiom `[1]`),
**más un segundo hallazgo del mismo tipo, expuesto solo al destrabar el
primero:** `Opus/rsb.h:38` (`struct BMS rgbms []`, dentro de `struct RSBI`) y
`Opus/rsb.h:73` (`struct ZPP rgzpp[]`, dentro de `union GRPZPP`), ambos
diagnosticados por GCC 14 como *"flexible array member in a struct with no
named members"* — variante distinta del mismo `error_at()` incondicional
(cubierta por el mismo commit upstream `r15-209`/PR53548, que dice
explícitamente "in unions **and alone in structures**"). Confirmado
pre-existente con `git stash` antes de tocar `rsb.h`: el error ya estaba ahí,
solo quedaba oculto detrás del bloqueo de `disp.h` porque `ninja` no llega a
compilar `cmdwnd.c` (el primer TU que arrastra `rsb.h`) hasta que otro TU en
paralelo falla primero. Ambos sitios recibieron la misma guarda `#if
defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)` con `rgbms[1]` /
`rgzpp[1]`, autorizado explícitamente por el mantenedor tras reportar el
hallazgo (no estaba en el alcance original aprobado, se pidió permiso aparte
antes de tocar el segundo archivo).

**Es un contorno de compatibilidad de versión de compilador, no un cambio de
diseño.** El layout de `struct PLDR`, `struct RSBI` y `union GRPZPP` es el
mismo en intención (tail de tamaño variable tras un header fijo/alias sobre
campos nombrados); lo único que cambia es qué expresión de C acepta GCC 14
para declararlo. Bajo GCC ≥15 (rama `#else` de cada guarda) el código sigue
siendo el flexible array member `[]` original, sin ningún cambio de
comportamiento — la guarda es simétrica y no toca el camino MSVC
(`_MSC_VER` excluido) ni GCC ≥15.

**Neutralidad de `cwPLDR` confirmada, no solo argumentada:** `grep -rn
cwPLDR src/Opus` antes y después del cambio devuelve únicamente la
definición de la macro (`disp.h:251`), cero sitios de uso. `izppMax`
(`rsb.h:89`, `sizeof(union GRPZPP)/sizeof(struct ZPP)`) tampoco se movió:
la rama nombrada de la unión (5 `struct ZPP`) sigue siendo estrictamente
mayor que la rama con `rgzpp[1]` (1 `struct ZPP`), así que
`sizeof(union GRPZPP)` no cambió — mismo razonamiento para `struct RSBI`
frente a `ibmsMax`/`ibmsMax2`/`ibmsMax3` (constantes literales, no
derivadas de `sizeof`, y en cualquier caso la rama nombrada de 5-9 `BMS`
domina sobre `rgbms[1]`).

**Verificación real ejecutada (VPS Debian, GCC 14.2.0, este build):**

1. `cmake --build --preset linux-winelib-debug --target opus_original_engine`
   — **compila y linka limpio (exit 0)**, sin ningún `error: flexible array
   member ...` en la salida. Confirmado antes/después con `git stash`: sin
   el fix, el mismo build para en `disp.h:248`; con el fix, no vuelve a
   aparecer esa clase de error en ningún punto del árbol `Opus/`.
2. `cmake --build --preset linux-winelib-debug --target WORD1` — **sigue sin
   completar, pero por un motivo totalmente ajeno**: `wrc: Error: codepage
   1252 not supported` al compilar `port/word1.rc` (falta de datos de
   codepage en el `wrc` de este VPS). No relacionado con FAM/union, no
   tocado, ya venía así.
3. `ctest --test-dir out/linux-winelib-debug` (suite gating completa) — los
   tests que dependen de `opus_original_engine`/enlace Winelib puro contra
   `user32`/`gdi32`/`comdlg32`
   (`opus_x64_runtime_test`, `opus_original_sttb_test`,
   `opus_original_plc_test`, `opus_sdm_cab_test`,
   `opus_original_command_test`) **no llegan a linkar en este VPS por falta
   de `wine32:i386`/multiarch** (`it looks like wine32 is missing... apt-get
   install wine32:i386`) — confirmado pre-existente con `git stash`, mismo
   síntoma con o sin el fix de FAM. Bloqueador de entorno, no de código,
   fuera de alcance de esta tarea.
   Los tres que sí dependen de la frontera núcleo/shell cruzando
   winegcc/wineg++ **compilan, linkan y pasan**: `opus_shell_memory_foreign_test`,
   `opus_shell_config_test`, `opus_shell_font_substitution_test` — 3/3
   ✓ (sin regresión).
4. Suite propia de `src/core` (`OPUS_CORE_BUILD_TESTS`, compilador nativo,
   `ctest --test-dir
   out/linux-winelib-debug/opus_core_build-prefix/src/opus_core_build-build`)
   — **5/5 ✓** (`opus_shell_font_substitution_test`,
   `opus_shell_font_metrics_test`, `opus_shell_font_metrics_fidelity_test`,
   `opus_shell_spine_test`, `opus_shell_config_test`). Esperable: `src/core`
   nunca incluye `Opus/wordtech/disp.h` ni `Opus/rsb.h`, el cambio no podía
   afectarlo; se corrió de todas formas por rigor, sin regresión.

**Pendiente, fuera de esta tarea:** el bloqueador de `wrc`/codepage 1252
(bloquea `WORD1` completo) y la falta de `wine32:i386` en este VPS (bloquea
5 tests gating por enlace) son blockers de entorno distintos, sin relación
con FAM/union — no se investigan ni se tocan aquí.

### Verificación cruzada en Fedora 44 / GCC 16.1.1 (segunda máquina) — 2026-08-12

**Estado real: el guard `__GNUC__ < 15` se comporta como se esperaba —
GCC 16 toma la rama `#else` original, cero diagnósticos de FAM/union en
todo el árbol.** Pero el build completo (`ninja -k 0`, reconfigurado limpio)
y la suite de `src/core` **no quedan en verde** en esta máquina, por motivos
enteramente ajenos al guard — un hueco de enlace real recién expuesto
(`WORD1` → `opus_shell_spine`) y dos diferencias de entorno (convención de
rutas de fuentes de Fedora, segfault de Qt). Se documentan las dos salidas
de compilador lado a lado porque son las dos máquinas reales donde se probó
este fix, no una proyección:

| | VPS Debian (sección anterior) | Fedora 44 (esta sección) |
|---|---|---|
| Compilador | GCC 14.2.0 | **GCC 16.1.1** (`gcc (GCC) 16.1.1 20260515 (Red Hat 16.1.1-2)`) |
| Rama del guard que compila | `#if` (`rgdr[1]`/`rgbms[1]`/`rgzpp[1]`) | **`#else`** (`rgdr[]`/`rgbms[]`/`rgzpp[]`, forma original sin workaround) |
| `opus_original_engine` (207 TUs) | compila y linka limpio | **compila y linka limpio** |
| `WORD1.exe` completo | bloqueado por `wrc`/codepage 1252 (ajeno) | **bloqueado por enlace: falta `opus_shell_spine`** (ajeno, ver más abajo) |
| Suite `src/core` (5 tests) | 5/5 ✓ | **1/5 ✓** (4 fallos ajenos al guard, ver más abajo) |

#### Comando y salida literal: versión de compilador

```
$ gcc --version
gcc (GCC) 16.1.1 20260515 (Red Hat 16.1.1-2)
Copyright (C) 2026 Free Software Foundation, Inc.
Esto es software libre; vea el código para las condiciones de copia.  NO hay
garantía; ni siquiera para MERCANTIBILIDAD o IDONEIDAD PARA UN PROPÓSITO EN
PARTICULAR
```

#### Guard confirmado, contexto exacto (`git grep -n -A4 -B6 '__GNUC__ < 15'`)

```
src/Opus/rsb.h-35-struct RSBI {
src/Opus/rsb.h-36-	union {
src/Opus/rsb.h-37-	struct	{
src/Opus/rsb.h:38:#if defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)
src/Opus/rsb.h-39-		struct BMS rgbms [1];  /* GCC <15 rejects FAM alone in struct (PR53548, r15-209) */
src/Opus/rsb.h-40-#else
src/Opus/rsb.h-41-		struct BMS rgbms [];
src/Opus/rsb.h-42-#endif
...
src/Opus/rsb.h:77:#if defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)
src/Opus/rsb.h-78-		struct ZPP rgzpp[1];  /* GCC <15 rejects FAM alone in struct (PR53548, r15-209) */
src/Opus/rsb.h-79-#else
src/Opus/rsb.h-80-		struct ZPP rgzpp[];
src/Opus/rsb.h-81-#endif
...
src/Opus/wordtech/disp.h-246-	union   {
src/Opus/wordtech/disp.h-247-		HQ	hqpldre;    /* when fExternal true */
src/Opus/wordtech/disp.h:248:#if defined(__GNUC__) && !defined(_MSC_VER) && (__GNUC__ < 15)
src/Opus/wordtech/disp.h-249-		struct DR rgdr[1];  /* when fExternal false -- GCC <15 rejects FAM in union (PR53548, r15-209) */
src/Opus/wordtech/disp.h-250-#else
src/Opus/wordtech/disp.h-251-		struct DR rgdr[];   /* when fExternal false */
src/Opus/wordtech/disp.h-252-#endif
```

Con `__GNUC__` = 16 en esta máquina, `(__GNUC__ < 15)` evalúa a falso en los
tres sitios: se compila la rama `#else`, es decir **la forma original de
Microsoft sin ningún workaround**, exactamente como debía ser. Confirmado
no solo por lectura del guard sino por el resultado de compilación (próxima
sección): cero apariciones de `flexible array member` en ningún log, en
ninguna de las dos pasadas de build completas que se corrieron.

#### Build limpio, `ninja -k 0`: reconfiguración + resultado

```
$ rm -rf out/linux-winelib-debug out/linux-winelib-release
$ cmake --preset linux-winelib-debug
-- The C compiler identification is GNU 16.1.1
-- The CXX compiler identification is GNU 16.1.1
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/winegcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/wineg++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Performing Test HAVE_STDATOMIC
-- Performing Test HAVE_STDATOMIC - Success
-- Found WrapAtomic: TRUE
-- Found OpenGL: /usr/lib64/libOpenGL.so
-- Found WrapOpenGL: TRUE
-- Found WrapVulkanHeaders: /usr/include
-- Configuring done (3.4s)
-- Generating done (0.1s)
-- Build files have been written to: /home/exia/word1/msword/out/linux-winelib-debug
$ ninja -k 0 -j4
```

(`-j4` en vez del paralelismo por defecto de 12: esta máquina tiene 7,7 GiB
de RAM, no 32+; con -j12 el build de 370 objetivos arriesgaba OOM. `-k 0`
se preservó tal cual se pidió — no limita reintentos, solo limita
paralelismo.)

El build avanzó **370/370 objetivos intentados** (sin bloqueo temprano por
`disp.h`/`rsb.h` — la clase de error que bloqueaba el VPS ni aparece) y
terminó con exactamente **3 `FAILED:`**, ninguno relacionado con FAM/union.
Log completo (120118 líneas, 7,5 MB) guardado en
`/tmp/claude-1000/-home-exia-word1-msword/ef583425-7fc7-457c-b877-9abeeaa77950/scratchpad/ninja-full-build.log`
de esta sesión — no se incluye íntegro aquí por tamaño; se pega cada
`FAILED:` completo, que es donde está toda la señal:

```
$ grep -c 'FAILED:' ninja-full-build.log
3
$ grep -c ' error:' ninja-full-build.log
3
$ grep -in 'flexible array' ninja-full-build.log ninja-targeted-retry.log
(sin coincidencias)
```

**Fallo 1/3 — `opus_original_plc_test.exe`, enlace, no relacionado con el guard:**

```
FAILED: [code=2] /home/exia/word1/msword/build/tests/Debug/opus_original_plc_test.exe 
: && /usr/bin/wineg++ -g -Wl,--dependency-file=CMakeFiles/opus_original_plc_test.dir/link.d CMakeFiles/opus_original_plc_test.dir/Opus/wordtech/clsplc.c.o CMakeFiles/opus_original_plc_test.dir/port/original/opus_asm_plc_adapters.cpp.o CMakeFiles/opus_original_plc_test.dir/port/original/opus_original_plc_test.c.o -o /home/exia/word1/msword/build/tests/Debug/opus_original_plc_test.exe  /home/exia/word1/msword/build/lib/Debug/libopus_x64_runtime.a  -luser32 && :
/usr/bin/ld.bfd: /home/exia/word1/msword/build/lib/Debug/libopus_x64_runtime.a(opus_win16_platform.cpp.o): en la función `GetPhysicalFontHandle':
/home/exia/word1/msword/src/port/original/opus_win16_platform.cpp:69:(.text+0x1aa): referencia a `GetCurrentObject' sin definir
/usr/bin/ld.bfd: [...] referencia a `GetBitmapDimensionEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetBitmapDimensionEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `GetViewportExtEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `GetViewportOrgEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetViewportExtEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetViewportOrgEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetWindowExtEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `SetWindowOrgEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `GetTextExtentPoint32A' sin definir
/usr/bin/ld.bfd: [...] referencia a `MoveToEx' sin definir
/usr/bin/ld.bfd: [...] referencia a `ShellExecuteA' sin definir
/usr/bin/ld.bfd: [...].exe.so: el símbolo oculto «SetWindowExtEx» no está definido
/usr/bin/ld.bfd: falló el enlace final: valor incorrecto
collect2: error: ld devolvió el estado de salida 1
winegcc: /usr/bin/g++ failed
```

Causa: `src/CMakeLists.txt:1220` — `target_link_libraries(opus_original_plc_test
PRIVATE opus_original_c_dialect opus_x64_runtime user32)` — no lista
`gdi32` ni `shell32`, y `opus_win16_platform.cpp` (dentro de
`opus_x64_runtime`) llama a las variantes `*Ex` de GDI (`gdi32`) y a
`ShellExecuteA` (`shell32`). No es un hueco nuevo de esta sesión: no hay
ningún cambio reciente en ese target — es preexistente, solo visible ahora
porque en el VPS ni se llegaba a intentar enlazar (bloqueado antes por
`disp.h`, y luego por falta de `wine32:i386`).

**Fallo 2/3 — `opus_x64_runtime_test.exe`, enlace, no relacionado con el guard:**

```
FAILED: [code=2] /home/exia/word1/msword/build/tests/Debug/opus_x64_runtime_test.exe 
: && /usr/bin/wineg++ -g -Wl,--dependency-file=CMakeFiles/opus_x64_runtime_test.dir/link.d CMakeFiles/opus_x64_runtime_test.dir/port/original/opus_x64_runtime_test.cpp.o CMakeFiles/opus_x64_runtime_test.dir/port/original/opus_x64_layout_test_fixture.c.o -o /home/exia/word1/msword/build/tests/Debug/opus_x64_runtime_test.exe  /home/exia/word1/msword/build/lib/Debug/libopus_x64_runtime.a  -luser32  -lgdi32 && :
/usr/bin/ld.bfd: [...]opus_sdm_runtime.cpp.o: en la función `(anonymous namespace)::commit_ribbon_list_selection(...)':
/home/exia/word1/msword/src/port/original/opus_sdm_runtime.cpp:1779:(.text+0x8a99): referencia a `OpusX64TraceRibbon' sin definir
/usr/bin/ld.bfd: [...] (más 4 sitios, mismo símbolo)
/usr/bin/ld.bfd: [...] en la función `run_word95_common_file_dialog(...)':
/home/exia/word1/msword/src/port/original/opus_sdm_runtime.cpp:2184:(.text+0xabc7): referencia a `GetOpenFileNameA' sin definir
/usr/bin/ld.bfd: [...]:2184:(.text+0xabe3): referencia a `GetSaveFileNameA' sin definir
/usr/bin/ld.bfd: [...]:2186:(.text+0xabfd): referencia a `CommDlgExtendedError' sin definir
/usr/bin/ld.bfd: [...].exe.so: el símbolo oculto «GetSaveFileNameA» no está definido
/usr/bin/ld.bfd: falló el enlace final: valor incorrecto
collect2: error: ld devolvió el estado de salida 1
winegcc: /usr/bin/g++ failed
```

Dos causas distintas en el mismo fallo: (a) `GetOpenFileNameA`/
`GetSaveFileNameA`/`CommDlgExtendedError` son de `comdlg32`, ausente del
`target_link_libraries` (`src/CMakeLists.txt:1447`, solo lista `user32
gdi32`); (b) `OpusX64TraceRibbon` — `extern "C" void OpusX64TraceRibbon(...)`
está **declarada** en `opus_sdm_runtime.cpp:24` y **llamada** desde ahí, pero
solo está **definida** en
`port/original/opus_original_startup_probe.cpp:387`, que es fuente exclusiva
del ejecutable `WORD1` — no forma parte de `libopus_x64_runtime.a`. Cualquier
binario que enlace `opus_x64_runtime` sin también incluir el probe (como
este test) queda con una referencia sin resolver por diseño del árbol
actual, no por un error transitorio.

**Fallo 3/3 — `WORD1.exe`, enlace, no relacionado con el guard — bloquea la Fase 4 completa en esta máquina:**

```
FAILED: [code=2] /home/exia/word1/msword/bin/WORD1.exe 
: && /usr/bin/wineg++ -g -mwindows -municode -Wl,--dependency-file=CMakeFiles/WORD1.dir/link.d generated/original/word1.spec generated/original/word1.res CMakeFiles/WORD1.dir/port/original/opus_original_startup_probe.cpp.o -o /home/exia/word1/msword/bin/WORD1.exe  /home/exia/word1/msword/build/lib/Debug/libopus_original_engine.a  /home/exia/word1/msword/build/lib/Debug/libopus_x64_runtime.a  -luser32  -ldbghelp  core/lib/libopus_shell_config.a  core/lib/libopus_shell_memory.a  core/lib/libopus_shell_font_metrics.a  /usr/lib64/libQt6Gui.so.6.11.1  /usr/lib64/libGLX.so  /usr/lib64/libOpenGL.so  /usr/lib64/libQt6Core.so.6.11.1  core/lib/libopus_shell_font_substitution.a && :
/usr/bin/ld.bfd: /home/exia/word1/msword/build/lib/Debug/libopus_original_engine.a(error.c.o): en la función `ErrorEidStartup':
/home/exia/word1/msword/src/Opus/wordtech/error.c:1630:(.text+0xc4f): referencia a `OpusShellReportError' sin definir
collect2: error: ld devolvió el estado de salida 1
winegcc: /usr/bin/g++ failed
ninja: build stopped: cannot make progress due to previous errors.
```

Causa, identificada con `git show`: el commit `ea5f908` (B4.4, "conecta
`error.c:1618` al contrato `OpusShellReportError`") agregó la llamada real
en `error.c` pero **no tocó `src/CMakeLists.txt`** — el target `WORD1`
(línea 1298-1309) nunca recibió `opus_shell_spine` en su
`target_link_libraries`, a diferencia de `opus_shell_config`,
`opus_shell_memory` y `opus_shell_font_metrics`, que sí están (líneas
1306-1308). El propio §B4.4 de este documento ya advertía la falta de
verificación de punta a punta ("no se disparó ni se observó en ejecución
esta sesión"), atribuyéndolo entonces al bloqueo de `disp.h` — en Fedora
`disp.h` ya no bloquea, y este es el siguiente bloqueador real de la
cadena, no una regresión de esta tarea. Reintentado explícitamente con
`ninja -k 0 opus_original_plc_test opus_x64_runtime_test WORD1` tras el
build completo: mismos tres fallos, idénticos, byte a byte en el mensaje
de enlace (log en
`.../scratchpad/ninja-targeted.log`).

**No se tocó `src/CMakeLists.txt` ni ningún archivo de código en esta
tarea** — los tres fallos se dejan diagnosticados, no corregidos, a la
espera de decisión (afecta directamente a la Tarea 2 de esta misma sesión,
que necesita un `WORD1.exe.so` enlazable).

#### `ctest` de `src/core` — 1/5 ✓, 4 fallos ajenos al guard

```
$ cd out/linux-winelib-debug/opus_core_build-prefix/src/opus_core_build-build
$ ctest --output-on-failure
Test project /home/exia/word1/msword/out/linux-winelib-debug/opus_core_build-prefix/src/opus_core_build-build
    Start 1: opus_shell_font_substitution_test
1/5 Test #1: opus_shell_font_substitution_test .......***Failed    0.00 sec
FALLÓ: Tms Rmn: archivo de fuente '/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf' debe poder abrirse -- ¿faltan las fuentes Liberation en esta máquina?
FALLÓ: Symbol: archivo de fuente '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf' debe poder abrirse -- ¿faltan las fuentes Liberation en esta máquina?
FALLÓ: Helv: archivo de fuente '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf' debe poder abrirse -- ¿faltan las fuentes Liberation en esta máquina?
FALLÓ: Courier: archivo de fuente '/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf' debe poder abrirse -- ¿faltan las fuentes Liberation en esta máquina?
OpusShellFontSubstitution_test: 4 fallo(s) de 16 verificaciones.

    Start 2: opus_shell_font_metrics_test
2/5 Test #2: opus_shell_font_metrics_test ............***Failed    0.10 sec
[21 fallo(s), misma causa raíz: no puede abrir los .ttf en la ruta hardcodeada]

    Start 3: opus_shell_font_metrics_fidelity_test
3/5 Test #3: opus_shell_font_metrics_fidelity_test ...***Failed    0.11 sec
[56 fallo(s), misma causa raíz]

    Start 4: opus_shell_spine_test
4/5 Test #4: opus_shell_spine_test ...................***Exception: SegFault  0.58 sec

    Start 5: opus_shell_config_test
5/5 Test #5: opus_shell_config_test ..................   Passed    0.01 sec

20% tests passed, 4 tests failed out of 5
```

**Los 3 fallos de fuentes son un solo hallazgo, no tres:** las rutas
hardcodeadas en `src/core/src/OpusShellFontSubstitution.cpp:31-34` y su
espejo en `OpusShellFontSubstitution_test.cpp:35-38` asumen la convención
Debian/Ubuntu (`/usr/share/fonts/truetype/liberation/…`). **Las fuentes sí
están instaladas en esta máquina** — confirmado, no asumido:

```
$ rpm -q liberation-serif-fonts liberation-sans-fonts liberation-mono-fonts
liberation-serif-fonts-2.1.5-15.fc44.noarch
liberation-sans-fonts-2.1.5-15.fc44.noarch
liberation-mono-fonts-2.1.5-15.fc44.noarch
$ fc-match "Liberation Serif"
LiberationSerif-Regular.ttf: "Liberation Serif" "Regular"
$ find / -iname 'LiberationSerif-Regular.ttf' 2>/dev/null
/usr/share/fonts/liberation-serif-fonts/LiberationSerif-Regular.ttf
[...dos rutas más dentro de runtimes Flatpak, irrelevantes...]
```

Fedora empaqueta cada familia en su propio directorio
(`liberation-serif-fonts/`, no `truetype/liberation/`). Es una diferencia
de convención de empaquetado entre distribuciones, no una fuente ausente —
el mismo bug de portabilidad que §B2.6/B2.7 de este documento ya resuelve
para la *sustitución de nombre de época → familia* (vía `fc-match`), pero
que **no se aplicó a la ruta de archivo físico**: ese segundo paso sigue
hardcodeado a una ruta absoluta en vez de resolverse vía `fc-match -f
'%{file}'` como hace la sonda de §B2.6. Es un bug real de portabilidad
entre distros, descubierto por esta verificación cruzada — no estaba
caracterizado antes porque el VPS es Debian y ahí la ruta sí existe. Fuera
del alcance de esta tarea (solo pide verificar el guard FAM/union); se deja
anotado para una tarea de port aparte, no se corrige aquí.

**`opus_shell_spine_test` segfaulta**, causa no diagnosticada en esta
tarea (esta máquina no tiene `gdb` ni `valgrind` instalados — ver Tarea 2
de esta misma sesión para el mismo hueco de herramientas). Capturado por
`systemd-coredump`:

```
$ coredumpctl list | tail -1
Tue 2026-08-11 22:49:36 -04 27390 1000 1000 SIGSEGV present  .../core/bin/opus_shell_spine_test  1.7M
```

Sin `gdb`, `coredumpctl info` no produce un backtrace simbólico utilizable
(solo lista de módulos cargados). No se investigó más a fondo: es un
segundo hallazgo nuevo, no pedido por esta tarea, y comparte el mismo
bloqueador de herramientas que la Tarea 2. Reproducido dos veces
(`QT_QPA_PLATFORM=offscreen` incluido) con el mismo resultado, así que no es
un fallo intermitente relacionado con la sesión Wayland de esta máquina.

#### Conclusión de esta sección

**El fix del guard `__GNUC__ < 15` queda verificado en las dos máquinas
reales donde se probó, con las dos versiones de compilador documentadas
explícitamente: GCC 14.2.0 (VPS Debian, toma la rama `#if`, con workaround)
y GCC 16.1.1 (Fedora 44, esta sección, toma la rama `#else`, sin
workaround) — ambas compilan `opus_original_engine` (207 TUs) limpio, cero
diagnósticos de FAM/union en ningún caso.** Esa es la afirmación puntual que
esta tarea pedía verificar, y se sostiene.

**Lo que NO se sostiene es "el build/los tests están en verde en Fedora"**
— no lo están, por motivos enteramente ajenos al guard: un hueco de enlace
real (`WORD1`/`opus_shell_spine`, expuesto ahora que `disp.h` deja de
bloquear antes de llegar ahí) y dos diferencias de entorno (convención de
rutas de fuentes, segfault de Qt sin diagnóstico por falta de `gdb`).
Documentado con el mismo rigor que el hallazgo del VPS: cada afirmación
tiene su comando y su salida literal arriba, nada se da por bueno sin
evidencia.
