# Rama Qt — Fase Qt-0: inventario de acoplamiento Win32

**Fecha:** 2026-08-10
**Estado:** aprobado para implementación
**Alcance:** solo esta fase (Qt-0). Fases Qt-1..Qt-7 quedan fuera de este spec.

## Contexto

La rama Qt busca liberar Word 1.1a de la capa Win32/GDI/Wine y correrlo como
aplicación nativa Linux con shell Qt propio, dividiendo el proyecto en
núcleo (lógica de documento, C89 heredado, sin `windows.h`) y shell (Qt,
presentación). Antes de diseñar la API de frontera (Qt-1) hace falta saber
cuánto código de `src/Opus` y `src/OpusEtAl` depende de Win32, y de qué tipo
es esa dependencia. Esa es la única entrega de Qt-0.

Sin este inventario, el riesgo documentado es "reescritura infinita": el
proyecto puede subestimar el volumen de código de presentación entrelazado
con lógica de documento.

## No-objetivos

- No se diseña la API de frontera (Qt-1).
- No se modifica ninguna línea de `src/Opus/` ni `src/OpusEtAl/` (árbol
  restringido según `CONTRIBUTING.md`; esta fase es de solo lectura).
- No se decide todavía qué símbolos de categoría "frontera" se abstraen con
  qué typedef — eso es diseño de Qt-1, informado por este inventario.

## Método

Script en Python, `docs/port-qt/scripts/audit_win32.py`, de un solo uso para
esta fase (no se integra al build, no es parte del CMake). Recorre
`src/Opus/` y `src/OpusEtAl/` (todos los `.c`/`.h`), excluyendo `src/port/`
(ya es capa Winelib, no es el núcleo que se está auditando).

Búsqueda por texto/regex contra un diccionario curado de símbolos Win32,
no vía preprocesador: el árbol usa macros propias pesadas (`NATIVE`,
`PASCAL`, `Win()/Mac()`) que un `-E` expandiría con ruido no relacionado a
Win32. Regex es suficiente para dimensionar; falsos positivos aislados
(comentarios, nombres de macro propios que coinciden por casualidad) se
filtran en la revisión manual de la tabla, no bloquean el objetivo de la
fase.

Cada patrón se ancla con límites de palabra explícitos (`\b<símbolo>\b`).
Sin ese anclaje, un identificador como `hwndParent` contaría como hit de
`HWND` e infla el conteo de sitios con coincidencias parciales que no son
usos reales del símbolo.

### Diccionario de símbolos

Organizado en las mismas categorías que usa la clasificación final, como
punto de partida (una entrada del diccionario no predetermina la categoría
final — ver más abajo):

- **Tipos/HANDLE-like:** `HWND`, `HANDLE`, `HDC`, `HBITMAP`, `HFONT`, `HMENU`,
  `HGLOBAL`, `HCURSOR`, `HICON`, `RECT`, `POINT`, `SIZE`, `LPARAM`,
  `WPARAM`, `LRESULT`.
- **Mensajes:** todo identificador `WM_*`, `SendMessage`, `PostMessage`,
  `DefWindowProc`, `WndProc`.
- **GDI dibujo:** `BitBlt`, `StretchBlt`, `Rectangle`, `MoveTo`, `LineTo`,
  `Polygon`, `SelectObject`, `CreatePen`, `CreateBrush`, `GetDC`,
  `ReleaseDC`, `InvalidateRect`.
- **GDI texto/fuentes:** `TextOut`, `GetTextExtentPoint`, `CreateFont`,
  `GetTextMetrics`, `SetTextColor`, `SetBkColor`.
- **Diálogos/controles:** `DialogBox`, `CreateDialog`, `GetDlgItem`,
  `SendDlgItemMessage`, `EndDialog`.
- **Portapapeles:** `OpenClipboard`, `CloseClipboard`, `SetClipboardData`,
  `GetClipboardData`, `EmptyClipboard`.
- **Impresión:** `StartDoc`, `EndDoc`, `StartPage`, `EndPage`,
  `CreateDC` (rama impresora).
- **Tipos primitivos:** `WORD`, `DWORD`, `BOOL`, `BYTE`, `LPSTR`, `LPCSTR`,
  `LPVOID`, `FARPROC`, `far`, `near`, `pascal`, `CALLBACK`, `WINAPI`.
  Clasificados como **frontera**, no núcleo puro: son typedefs y
  convenciones de llamada de la ABI Win32/C89 de la época, omnipresentes en
  firmas de función. Un archivo que solo usa `WORD`/`BOOL` en firmas, sin
  tocar GDI ni mensajes, sigue acoplado a esa ABI y necesitará typedef
  propio en Qt-1 — no puede clasificarse como núcleo puro por exclusión.

Punto de partida ampliable: se cruza contra hallazgos ya existentes en
`docs/port-linux/00-reconocimiento.md` (dktString, empaquetado
HIWORD/LOWORD de punteros) para no perder pistas ya documentadas por la
rama Winelib.

### Clasificación

Por símbolo, no por archivo:

- **Presentación** — GDI (dibujo o texto), mensajes `WM_*`, diálogos,
  portapapeles, impresión. Candidato a reescritura total en el shell Qt.
- **Frontera** — tipos usados solo como transporte (`HANDLE`, tamaños,
  flags) sin lógica de presentación asociada. Abstraíbles con typedef
  propio en Qt-1.
- **Núcleo puro** — no se asigna por símbolo. Se deriva por exclusión:
  archivos sin ningún hit del diccionario son candidatos a núcleo puro.

## Salida

`docs/port-qt/00-inventario-win32.md`, con dos secciones:

1. **Tabla de símbolos** (granularidad por símbolo individual, no por
   familia): columnas símbolo | categoría | sitios totales | archivos
   afectados | hasta 5 archivos ejemplo (resto como "+N más").
2. **Resumen agregado**: totales de sitios y archivos por categoría, y
   lista completa de archivos "núcleo puro candidato" (cero hits) — esa
   lista es la que dimensiona cuánto núcleo real hay antes de empezar Qt-1.

## Criterio de éxito

- Todo `.c`/`.h` bajo `src/Opus/` y `src/OpusEtAl/` aparece contabilizado
  (con hits o en la lista de cero-hits).
- Cada símbolo del diccionario con al menos un hit tiene fila propia con
  conteo y categoría.
- El reporte permite responder, sin releer código: "¿qué proporción del
  árbol es núcleo puro candidato hoy?" y "¿qué familia de API concentra más
  sitios de presentación?".

## Restricción heredada por Qt-2 y Qt-7

Decisión de proyecto (2026-08-10), asentada aquí para que no se pierda entre
fases: la paginación del shell Qt debe ser **idéntica byte a byte** respecto
del oráculo Winelib. Por tanto la interfaz de medición de texto de la
frontera **debe reproducir el redondeo entero de GDI**; no se admiten
métricas independientes de dispositivo de Qt sin una capa de compatibilidad
explícita que reproduzca ese redondeo. Qt-0 no la resuelve; su aporte es
delimitar la superficie donde la restricción se hace o se rompe (categoría
*GDI texto/fuentes* del inventario, con `GetTextExtent` como símbolo a
vigilar).

## Riesgos de este spec puntual

- El diccionario de símbolos es curado a mano y puede tener huecos
  (símbolo Win32 real no listado). Mitigación: no es exhaustivo por
  diseño — es de la fase Qt-0, revisable en Qt-1 si aparecen sorpresas al
  extraer el núcleo.
- Regex sobre texto puede contar falsos positivos dentro de comentarios o
  strings. Se acepta como ruido de bajo impacto dado que el objetivo es
  dimensionar, no exhaustividad certificada.
- El método es textual por símbolo y no sigue la cadena de `#include`: un
  archivo shim puro (p. ej. `Opus/windows.h`, que solo reexporta
  `qwindows.h` y `toolbox.h` sin usar ningún símbolo directamente) puede
  aparecer sin hits directos pese a estar funcionalmente acoplado a Win32
  a través del header que incluye. Mitigación aplicada: se verifica un
  solo salto de `#include` sobre los archivos sin hits directos, separando
  "núcleo puro confirmado" de "núcleo puro candidato — acoplamiento
  transitivo vía include". No se sigue la cadena completa de includes —
  eso exige leer código real para diseñar la frontera y es scope de Qt-1,
  no de este inventario.
