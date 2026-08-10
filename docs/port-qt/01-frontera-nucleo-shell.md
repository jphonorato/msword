# Fase Qt-1 — Diseño de la frontera núcleo/shell

**Estado:** diseño, sin implementar. Ninguna línea de port se escribe en esta
fase; eso empieza en Qt-2.
**Insumo:** `docs/port-qt/00-inventario-win32.md`, en su versión posterior a la
exclusión de comentarios y literales.
**Decisiones de alcance cerradas:** fidelidad de paginación idéntica byte a
byte contra el oráculo Winelib; `Opus/interp/` es núcleo; `OpusEtAl/` por
veredicto individual (52 excluir, 6 diferir); `Opus/debug/` se porta.

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

Interfaz en C, implementada por el shell, consumida por el núcleo:

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

**Por qué la coincidencia exacta sí es alcanzable.** Porque el oráculo del
proyecto es la GDI de **Wine**, y Wine rasteriza con FreeType, igual que Qt.
Pedir el mismo modo de hinting al mismo ppem entero produce los mismos
enteros porque debajo corre el mismo motor. Esto es una dependencia de la
conclusión y hay que enunciarla: **el resultado no se transfiere a la GDI de
Microsoft sobre Windows**, cuyo rasterizador es otro. El oráculo declarado del
proyecto es el binario Winelib, así que la comparación es la correcta, pero la
equivalencia vale contra ese oráculo y no contra Windows.

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

### B3.2 El problema que queda: el handle mide 16 bits por definición

`Opus/lib/qwindows.h:630` declara `typedef WORD HANDLE`. Un handle Win16 **es**
un entero de 16 bits, y `struct FTI` guarda un
`struct FONTREC far * far *qqftr` —un puntero a puntero— junto a campos de 16
bits, en una estructura que el código serializa.

De ahí el contrato: **los handles son de tiempo de ejecución y no se
serializan nunca.**

```c
/* Handle opaco de ancho completo. Nunca se empaqueta en 16 bits, nunca se
   escribe a disco, nunca se compara contra un literal. */
typedef struct OpusHandleImpl *OpusHandle;

OpusHandle    OpusMemAlloc(unsigned long cb, unsigned flags);
void         *OpusMemLock(OpusHandle h);     /* fija y devuelve puntero */
void          OpusMemUnlock(OpusHandle h);   /* libera la fijación */
OpusHandle    OpusMemRealloc(OpusHandle h, unsigned long cb, unsigned flags);
unsigned long OpusMemSize(OpusHandle h);
void          OpusMemFree(OpusHandle h);
```

Notas de diseño, en orden de riesgo:

1. **Memoria movible y disciplina de fijación.** El modelo Win16 permitía que
   el gestor moviera un bloque desbloqueado; `GlobalLock`/`GlobalUnlock` forman
   pares que el código respeta hoy. Qt no tiene equivalente. El allocator del
   núcleo puede fijar todo permanentemente —la memoria de un proceso de 64 bits
   lo permite— pero **los pares deben conservarse en el código**: eliminarlos
   como no-operaciones haría imposible detectar un uso de puntero tras mover,
   si más adelante se introdujera compactación.
2. **`GlobalLockClip`.** Variante de bloqueo específica de portapapeles. Va al
   contrato de portapapeles de Qt-6, no a este. Se anota aquí para que no se
   resuelva dos veces.
3. **Todo campo de estructura serializada con tipo handle necesita
   indirección.** Antes de tocar código, Qt-2 debe enumerar qué estructuras
   persistidas contienen campos de tipo `HANDLE` y sustituirlos por un índice
   en una tabla, dejando el handle fuera del formato de archivo. Esto conecta
   con la limitación ya documentada del port Winelib sobre cambios de formato
   bajo LP64, y es la parte de este contrato con más probabilidad de sorpresas.

---

## B4 — Contrato de espina de mensajes y ventanas

287 sitios, 47 TUs: `Opus/` raíz 38, `Opus/wordtech/` 6, `Opus/debug/` 3.

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
- **`editspec.c`, `undo.c`** — notifican cambios de documento por mensajes. Se
  sustituyen por el callback de notificación de cambio, una de las piezas que
  Qt-1 tenía previstas desde el principio.
- **`scroll.c`, `disp3.c`, `pagevw.c`** — mezclan cálculo de qué es visible
  (núcleo) con repintado (shell). Requieren la separación por función descrita
  en §B1.3 y no deberían intentarse antes de que el contrato de medición de
  texto esté implementado y verificado.

---

## B5 — Contrato de persistencia de configuración

43 sitios, 12 TUs. Símbolos: `GetProfileString`, `GetProfileInt`,
`WriteProfileString`.

El más simple de los cuatro, y se deja simple. La semántica de `WIN.INI`
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

---

## Secuencia recomendada para Qt-2

El orden no es arbitrario: cada paso deja verificable el siguiente.

1. **Configuración (B5).** 43 sitios, contrato trivial. Sirve para establecer
   el mecanismo de frontera —cómo el núcleo llama al shell— sobre algo cuyo
   fallo es visible al instante y cuyo riesgo es nulo.
2. **Tabla de sustitución de fuentes (§B2.5).** Extraer del oráculo el mapeo
   real de los nombres de época a archivos físicos. Es barato, es un
   prerrequisito de cualquier prueba de fidelidad, y hoy no está escrito en
   ninguna parte.
3. **Memoria (B3), sin tocar serialización.** Sustituir `Global*` por el
   allocator opaco, conservando los pares de fijación. Verificable: el motor
   sigue enlazando y las pruebas de humo del port Winelib siguen pasando.
4. **Enumeración de handles serializados (§B3.2).** Solo inventario, sin
   cambios. Es la compuerta antes de cualquier trabajo de formato de archivo.
5. **Medición de texto (B2)** con la comparación contra el oráculo activa desde
   el primer commit, usando la estrategia de §B2.3. Es la pieza de la que
   depende la restricción de fidelidad, y la que hace que `wordtech/` compile
   sin GDI.
6. **`error.c`, luego `editspec.c` y `undo.c` (§B4.3).** Callbacks de error y
   de cambio de documento.
7. **Inversión del bucle de mensajes (§B4.1).** Último, porque hasta aquí el
   núcleo puede seguir siendo conducido por el binario Winelib, que es el
   oráculo. Invertirlo antes de tener la medición verificada quitaría el
   oráculo justo cuando más se necesita.

---

## Preguntas abiertas

Las dos que este documento tenía sobre fidelidad quedaron cerradas en §B2.3 y
§B2.5. Quedan dos, ninguna bloqueante para empezar Qt-2:

1. **Nombre y ubicación de la API de frontera.** Este documento usa el prefijo
   `OpusShell*` para lo que el shell implementa. Falta decidir si vive en
   `src/core/` con un `include/` público, o si se mantiene la disposición
   actual con un header nuevo en `port/`.
2. **Los 6 archivos «diferir» de `OpusEtAl/`** (`cashmere/fldexp/` 4,
   `tools/src/opustlbx/` 2) siguen sin veredicto. No bloquean Qt-2, pero
   `opustlbx` podría estar relacionado con `port/original/toolbox.h`, que sí es
   capa activa del port.
