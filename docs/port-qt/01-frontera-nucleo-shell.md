# Fase Qt-1 — Diseño de la frontera núcleo/shell

**Estado:** diseño, sin implementar. Ninguna línea de port se escribe en esta
fase; eso empieza en Qt-2.
**Insumo:** `docs/port-qt/00-inventario-win32.md`, en su versión posterior a la
exclusión de comentarios y literales.
**Decisiones de alcance cerradas:** fidelidad de paginación idéntica byte a
byte contra el oráculo Winelib; `Opus/interp/` es núcleo; `OpusEtAl/` por
veredicto individual (54 excluir, 4 diferir); `Opus/debug/` se porta.
**API de frontera:** cuatro headers de solo declaración en
`src/core/include/` — `OpusShellFontMetrics.h`, `OpusShellMemory.h`,
`OpusShellSpine.h`, `OpusShellConfig.h`.

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
serializan nunca.** Declarado en `src/core/include/OpusShellMemory.h`.

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
una función de enumeración, o tratarlo aparte). Memoria Win16 (B3) es el
siguiente ítem de la secuencia recomendada. Integrar `opus_shell_config`
(Qt6, toolchain nativo) en el enlace final de `WORD1` (toolchain
winegcc/wineg++) es una pregunta de integración cross-toolchain todavía
abierta y no resuelta aquí: este paso solo verificó que los 41 sitios
compilan bajo GNUC como parte de la biblioteca estática
`opus_original_engine`, que no fuerza esa resolución de símbolos.

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
