# Fase Qt-0 v2 — Inventario de acoplamiento Win32

**Árbol medido:** commit `a3c81ac` (2026-08-10) — con cambios sin comitear en el árbol de trabajo
**Generado por:** `docs/port-qt/scripts/audit_win32_v2.py`
**Spec:** `docs/superpowers/specs/2026-08-10-qt-branch-fase0-design.md`

## Método en una línea

Universo: **173 TU del motor** (`OPUS_ORIGINAL_ENGINE_SOURCES` en `src/CMakeLists.txt`) más **14 TU de `Opus/debug/`** contadas aparte (no enlazan en `WORD1`; en alcance por decisión de proyecto). Diccionario de **1618 símbolos** derivado mecánicamente de `Opus/lib/qwindows.h` (SDK Win16 vendorizado), `Opus/debugwin.h` (capa de interceptación que enumera las funciones Win16 llamadas) y las cabeceras Win32 de Wine intersectadas con el residual de identificadores externos. Esos dos headers son la superficie de API a reemplazar y se excluyen de los conteos de sitio. Patrones anclados con `\b`.

De los 1618 símbolos del diccionario, **697 tienen al menos un sitio** y 921 tienen cero. Los de cero no se listan: la derivación parte de la superficie completa del SDK Win16, así que las partes del SDK que Word nunca usó dan cero por construcción — eso es evidencia de cobertura, no un hueco de curación.

## Vista 1 — Por región arquitectónica

Esta es la tabla que Qt-1 debe leer primero: dice dónde está la frontera.

| Región | TUs | portable | frontera | presentación | % portable |
|---|---|---|---|---|---|
| Opus/wordtech/ (documento y layout) | 40 | **22** | 4 | 14 | 55% |
| Opus/interp/ (intérprete de macros) | 7 | **3** | 4 | 0 | 42% |
| Opus/ raíz (presentación) | 124 | **27** | 34 | 63 | 21% |
| port/original/ (capa del port) | 2 | **2** | 0 | 0 | 100% |
| Opus/debug/ (no enlazado en WORD1) | 14 | **6** | 2 | 6 | 42% |
| **TOTAL** | 187 | **60** | 44 | 83 | 32% |

`presentación` = toca GDI, mensajes, diálogos, portapapeles, impresión o entrada; se reescribe en el shell Qt. `frontera` = toca handles Win16, el modelo de memoria `Global*`/`Local*`, geometría o persistencia de configuración; se queda en el núcleo detrás de la API de Qt-1. `portable` = solo tipos primitivos, convenciones ABI o constantes Win16; entra al núcleo con la capa de typedefs, sin reescritura. Lo ya neutralizado por la capa Winelib no cuenta como pendiente.

### Corrección frente al reconteo previo

El reconteo manual que motivó esta revisión daba `wordtech/` 26/40, `interp/` 6/7 y `Opus/` raíz 32/124. Esta corrida da **22/40**, **3/7** y **27/124**: más estricta en `interp/` y en la raíz, y la diferencia es explicable por método, no por error de una u otra medición. Aquel reconteo usó unos 30 símbolos elegidos a mano (GDI, mensajes, memoria, handles). El diccionario de v2 tiene 1618 símbolos derivados de la superficie completa del SDK Win16 e incorpora tres categorías que aquel conteo no tenía: *Geometría* (`RECT`, `POINT`, `IntersectRect`…), *Entrada/cursor* (`LoadCursor`, `SetCursor`, caret) y *Persistencia de configuración*. TUs que antes pasaban como portables se reclasifican por esos ejes. La cifra de v2 es la que debe usarse.

## Vista 2 — Por TU del motor

| TU | Región | Veredicto | Categorías pendientes |
|---|---|---|---|
| `Opus/wordtech/banter.c` | Opus/wordtech/ | portable | Tipos primitivos |
| `Opus/wordtech/block.c` | Opus/wordtech/ | presentación | GDI dibujo, Entrada/cursor, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/clsplc.c` | Opus/wordtech/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/curskeys.c` | Opus/wordtech/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/disp2.c` | Opus/wordtech/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/wordtech/disp3.c` | Opus/wordtech/ | presentación | Espina de mensajes/ventanas, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/wordtech/disptbl.c` | Opus/wordtech/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/wordtech/editspec.c` | Opus/wordtech/ | presentación | Espina de mensajes/ventanas, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/editsub.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/error.c` | Opus/wordtech/ | presentación | Espina de mensajes/ventanas, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/fetch.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/fetch1.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/fetch2.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/fetchtb.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/footnote.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/format.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/hdd.c` | Opus/wordtech/ | portable | Constantes Win16 |
| `Opus/wordtech/hyph.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/ihdd.c` | Opus/wordtech/ | frontera | Tipos HANDLE-like, Constantes Win16 |
| `Opus/wordtech/insert.c` | Opus/wordtech/ | presentación | Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/inssubs.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/layout.c` | Opus/wordtech/ | portable | Constantes Win16 |
| `Opus/wordtech/layout1.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/layout2.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/layoutap.c` | Opus/wordtech/ | presentación | GDI texto/fuentes, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/wordtech/outline.c` | Opus/wordtech/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/pagevw.c` | Opus/wordtech/ | presentación | Espina de mensajes/ventanas, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/wordtech/prcsubs.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/printsub.c` | Opus/wordtech/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/savefast.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/scroll.c` | Opus/wordtech/ | presentación | Espina de mensajes/ventanas, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/wordtech/select.c` | Opus/wordtech/ | presentación | Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/selectsp.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/selecttb.c` | Opus/wordtech/ | presentación | Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/sttb.c` | Opus/wordtech/ | portable | Constantes Win16 |
| `Opus/wordtech/stysubs.c` | Opus/wordtech/ | portable | Constantes Win16 |
| `Opus/wordtech/tablecmd.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/tableins.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/tablesub.c` | Opus/wordtech/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/wordtech/undo.c` | Opus/wordtech/ | presentación | Espina de mensajes/ventanas, Tipos primitivos, Constantes Win16 |
| `Opus/interp/corout.c` | Opus/interp/ | portable | — |
| `Opus/interp/elcore.c` | Opus/interp/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/interp/elinit.c` | Opus/interp/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/interp/exp.c` | Opus/interp/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/interp/main.c` | Opus/interp/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/interp/sym.c` | Opus/interp/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/interp/to.c` | Opus/interp/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/annot.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/catalog.c` | Opus/ | frontera | Memoria Win16, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/cmd.c` | Opus/ | presentación | Mensajes WM_*, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/cmd2.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/cmdcore.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/cmdwnd.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/command.c` | Opus/ | presentación | Diálogos/menús, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/command2.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, Portapapeles, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/compare.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/create.c` | Opus/ | presentación | Espina de mensajes/ventanas, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/curswin.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/customiz.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/ddeclnt.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Portapapeles, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/ddesub.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/dialog1.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Diálogos/menús, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/dialog2.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/dialog3.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/dialog4.c` | Opus/ | portable | Tipos primitivos |
| `Opus/disp1.c` | Opus/ | presentación | Espina de mensajes/ventanas, GDI texto/fuentes, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/dispbrc.c` | Opus/ | presentación | GDI dibujo, Impresión, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/dispspec.c` | Opus/ | presentación | Espina de mensajes/ventanas, GDI texto/fuentes, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/dlgdoc.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/dlghyph.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Diálogos/menús, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/dlglook1.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/dlglook2.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/dlgmisc.c` | Opus/ | frontera | Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/dlgopen.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/dlgrec.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/dlgtable.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/docman1.c` | Opus/ | frontera | Tipos HANDLE-like, Constantes Win16 |
| `Opus/docman2.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/edmacro.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/eldde.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Mensajes WM_*, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/eldlg.c` | Opus/ | presentación | GDI texto/fuentes, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/elfile.c` | Opus/ | frontera | Memoria Win16, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/elmisc.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, Diálogos/menús, Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/elmisc2.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/elsubs.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/elsubs2.c` | Opus/ | frontera | Memoria Win16, Tipos primitivos, Constantes Win16 |
| `Opus/elsubs3.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/etcmd.c` | Opus/ | frontera | Memoria Win16, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/ffcrypt.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/ffread.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/fieldclc.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/fieldcr.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/fieldfmt.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/fieldpic.c` | Opus/ | frontera | Persistencia de configuración, Tipos primitivos, Constantes Win16 |
| `Opus/fieldprs.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/fieldsc2.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/fieldsp.c` | Opus/ | presentación | GDI texto/fuentes, GDI dibujo, Impresión, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/fieldspc.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/filecvt.c` | Opus/ | frontera | Memoria Win16, Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/filewin.c` | Opus/ | frontera | Persistencia de configuración, Tipos primitivos, Constantes Win16 |
| `Opus/fltexp.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/formatsp.c` | Opus/ | presentación | GDI texto/fuentes, Impresión, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/formula.c` | Opus/ | presentación | GDI texto/fuentes, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/glsy.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/grbit.c` | Opus/ | presentación | GDI dibujo, Tipos primitivos, Constantes Win16 |
| `Opus/grswath.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/grtiff.c` | Opus/ | portable | Tipos primitivos |
| `Opus/hddwin.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/help.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Mensajes WM_*, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/iconbar1.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/iconbar2.c` | Opus/ | presentación | Espina de mensajes/ventanas, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/iconbar3.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/idle.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Mensajes WM_*, Diálogos/menús, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/index1.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/index2.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/init2.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Diálogos/menús, Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/initwin.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, GDI texto/fuentes, GDI dibujo, Diálogos/menús, Entrada/cursor, Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/insfield.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/mathapi.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/menu.c` | Opus/ | presentación | Diálogos/menús, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/menuhelp.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/merge.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/open.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI dibujo, Diálogos/menús, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/openrare.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/outwin.c` | Opus/ | presentación | Espina de mensajes/ventanas, GDI texto/fuentes, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/pic.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/pictdrag.c` | Opus/ | presentación | GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/preview.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/print.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Impresión, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/print1.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/print2.c` | Opus/ | presentación | Mensajes WM_*, Impresión, Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/prompt.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/prvw2.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/quit.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Mensajes WM_*, GDI dibujo, Entrada/cursor, Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/raremsg.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Portapapeles, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/rcbmp1.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/rcbmp13.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/rcbmp2.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/rcbmp23.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/rcbmp3.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/rcbmp4.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/rcbmp43.c` | Opus/ | presentación | GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/rcinit.c` | Opus/ | presentación | Memoria Win16, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/recorder.c` | Opus/ | presentación | Mensajes WM_*, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/renum.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/replace.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/res.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/rsb.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/rtfsubs.c` | Opus/ | portable | Constantes Win16 |
| `Opus/rtftrans.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/rulerdrw.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/rulrib.c` | Opus/ | presentación | GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/save.c` | Opus/ | presentación | Mensajes WM_*, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/savetext.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/search.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/sort.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/spelcore.c` | Opus/ | frontera | Memoria Win16, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/splitter.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/srchfmt.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/statline.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, GDI texto/fuentes, GDI dibujo, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/strtbl.c` | Opus/ | portable | Tipos primitivos |
| `Opus/style.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/stylesub.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/tabs.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/toc.c` | Opus/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/token.c` | Opus/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/util2.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/vars.c` | Opus/ | portable | — |
| `Opus/wproc.c` | Opus/ | presentación | Memoria Win16, Espina de mensajes/ventanas, Mensajes WM_*, GDI dibujo, Entrada/cursor, Persistencia de configuración, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/wwact.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, Diálogos/menús, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wwchange.c` | Opus/ | presentación | Espina de mensajes/ventanas, Mensajes WM_*, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `port/original/opus_asm_movecmds.c` | port/original/ | portable | Constantes Win16 |
| `port/original/opus_x64_segment_anchors.c` | port/original/ | portable | — |
| `Opus/debug/debug.c` | Opus/debug/ | presentación | Espina de mensajes/ventanas, GDI texto/fuentes, GDI dibujo, Portapapeles, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/debug/debug1.c` | Opus/debug/ | presentación | Memoria Win16, Portapapeles, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/debug/debug2.c` | Opus/debug/ | presentación | Memoria Win16, GDI texto/fuentes, GDI dibujo, Portapapeles, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |
| `Opus/debug/debugcmd.c` | Opus/debug/ | frontera | Memoria Win16, Persistencia de configuración, Tipos primitivos, Constantes Win16 |
| `Opus/debug/debugdde.c` | Opus/debug/ | frontera | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/debug/debugdlg.c` | Opus/debug/ | portable | Constantes Win16 |
| `Opus/debug/debugfn.c` | Opus/debug/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/debug/debugfnt.c` | Opus/debug/ | presentación | Espina de mensajes/ventanas, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/debug/debuggdi.c` | Opus/debug/ | presentación | GDI texto/fuentes, GDI dibujo, Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/debug/debuginf.c` | Opus/debug/ | portable | Constantes Win16 |
| `Opus/debug/debugrep.c` | Opus/debug/ | portable | Tipos primitivos |
| `Opus/debug/debugscc.c` | Opus/debug/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/debug/debugstr.c` | Opus/debug/ | portable | Tipos primitivos, Constantes Win16 |
| `Opus/debug/debugwin.c` | Opus/debug/ | presentación | Memoria Win16, Espina de mensajes/ventanas, GDI texto/fuentes, GDI dibujo, Diálogos/menús, Portapapeles, Impresión, Entrada/cursor, Tipos HANDLE-like, Tipos primitivos, Geometría, Constantes Win16 |

## Vista 3 — Por símbolo

Ordenado por categoría (prioridad de la frontera primero), luego por sitios.

| Símbolo | Categoría | Sitios | TUs | Estado | Fuente |
|---|---|---|---|---|---|
| `GlobalUnlock` | Memoria Win16 | 57 | 15 | pendiente | debugwin.h |
| `GlobalFree` | Memoria Win16 | 38 | 11 | pendiente | debugwin.h |
| `GlobalLock` | Memoria Win16 | 34 | 12 | pendiente | debugwin.h |
| `GlobalLockClip` | Memoria Win16 | 23 | 7 | pendiente | debugwin.h |
| `GMEM_MOVEABLE` | Memoria Win16 | 17 | 8 | pendiente | qwindows.h |
| `GlobalAlloc` | Memoria Win16 | 8 | 4 | pendiente | debugwin.h |
| `GlobalCompact` | Memoria Win16 | 6 | 4 | pendiente | debugwin.h |
| `GlobalReAlloc` | Memoria Win16 | 5 | 3 | pendiente | debugwin.h |
| `GlobalSize` | Memoria Win16 | 5 | 3 | pendiente | debugwin.h |
| `GMEM_FIXED` | Memoria Win16 | 2 | 1 | pendiente | qwindows.h |
| `GlobalAddAtom` | Memoria Win16 | 2 | 2 | pendiente | debugwin.h |
| `GlobalFlags` | Memoria Win16 | 2 | 2 | pendiente | wine-headers |
| `GlobalWire` | Memoria Win16 | 2 | 2 | pendiente | debugwin.h |
| `UpdateWindow` | Espina de mensajes/ventanas | 38 | 20 | pendiente | debugwin.h |
| `ShowWindow` | Espina de mensajes/ventanas | 30 | 11 | pendiente | debugwin.h |
| `EnableWindow` | Espina de mensajes/ventanas | 20 | 8 | pendiente | debugwin.h |
| `DestroyWindow` | Espina de mensajes/ventanas | 19 | 12 | pendiente | debugwin.h |
| `PeekMessage` | Espina de mensajes/ventanas | 18 | 9 | pendiente | debugwin.h |
| `IsWindow` | Espina de mensajes/ventanas | 15 | 10 | pendiente | wine-headers |
| `TranslateMessage` | Espina de mensajes/ventanas | 15 | 10 | pendiente | debugwin.h |
| `CreateWindow` | Espina de mensajes/ventanas | 14 | 9 | pendiente | debugwin.h |
| `DefWindowProc` | Espina de mensajes/ventanas | 14 | 11 | pendiente | debugwin.h |
| `GetMessage` | Espina de mensajes/ventanas | 14 | 7 | pendiente | debugwin.h |
| `IsWindowVisible` | Espina de mensajes/ventanas | 14 | 8 | pendiente | wine-headers |
| `SetFocus` | Espina de mensajes/ventanas | 13 | 7 | pendiente | debugwin.h |
| `MessageBeep` | Espina de mensajes/ventanas | 10 | 7 | pendiente | debugwin.h |
| `SetWindowPos` | Espina de mensajes/ventanas | 9 | 6 | pendiente | debugwin.h |
| `MoveWindow` | Espina de mensajes/ventanas | 8 | 4 | pendiente | debugwin.h |
| `MessageBox` | Espina de mensajes/ventanas | 6 | 5 | pendiente | debugwin.h |
| `BringWindowToTop` | Espina de mensajes/ventanas | 4 | 3 | pendiente | debugwin.h |
| `MakeProcInstance` | Espina de mensajes/ventanas | 4 | 3 | pendiente | debugwin.h |
| `SetTimer` | Espina de mensajes/ventanas | 4 | 4 | pendiente | wine-headers |
| `Yield` | Espina de mensajes/ventanas | 4 | 3 | pendiente | debugwin.h |
| `InSendMessage` | Espina de mensajes/ventanas | 2 | 2 | pendiente | wine-headers |
| `IsIconic` | Espina de mensajes/ventanas | 2 | 2 | pendiente | wine-headers |
| `RegisterClass` | Espina de mensajes/ventanas | 2 | 2 | pendiente | debugwin.h |
| `SetActiveWindow` | Espina de mensajes/ventanas | 2 | 2 | pendiente | debugwin.h |
| `AnyPopup` | Espina de mensajes/ventanas | 1 | 1 | pendiente | wine-headers |
| `CallWindowProc` | Espina de mensajes/ventanas | 1 | 1 | pendiente | debugwin.h |
| `EnumWindows` | Espina de mensajes/ventanas | 1 | 1 | pendiente | wine-headers |
| `GetActiveWindow` | Espina de mensajes/ventanas | 1 | 1 | pendiente | wine-headers |
| `GetInputState` | Espina de mensajes/ventanas | 1 | 1 | pendiente | debugwin.h |
| `GetTopWindow` | Espina de mensajes/ventanas | 1 | 1 | pendiente | wine-headers |
| `WM_KEYDOWN` | Mensajes WM_* | 23 | 10 | pendiente | qwindows.h |
| `WM_LBUTTONDOWN` | Mensajes WM_* | 20 | 11 | pendiente | qwindows.h |
| `WM_SYSCOMMAND` | Mensajes WM_* | 20 | 9 | pendiente | qwindows.h |
| `WM_PAINT` | Mensajes WM_* | 16 | 11 | pendiente | qwindows.h |
| `WM_SYSKEYDOWN` | Mensajes WM_* | 15 | 6 | pendiente | qwindows.h |
| `WM_COMMAND` | Mensajes WM_* | 14 | 9 | pendiente | qwindows.h |
| `WM_LBUTTONDBLCLK` | Mensajes WM_* | 13 | 9 | pendiente | qwindows.h |
| `WM_CLOSE` | Mensajes WM_* | 11 | 8 | pendiente | qwindows.h |
| `WM_CREATE` | Mensajes WM_* | 11 | 9 | pendiente | qwindows.h |
| `WM_KEYUP` | Mensajes WM_* | 11 | 7 | pendiente | qwindows.h |
| `WM_MOUSEMOVE` | Mensajes WM_* | 10 | 7 | pendiente | qwindows.h |
| `WM_RBUTTONDOWN` | Mensajes WM_* | 10 | 2 | pendiente | qwindows.h |
| `WM_HSCROLL` | Mensajes WM_* | 9 | 3 | pendiente | qwindows.h |
| `WM_SYSKEYUP` | Mensajes WM_* | 9 | 4 | pendiente | qwindows.h |
| `WM_SIZE` | Mensajes WM_* | 8 | 6 | pendiente | qwindows.h |
| `WM_CHAR` | Mensajes WM_* | 7 | 6 | pendiente | qwindows.h |
| `WM_NCLBUTTONDOWN` | Mensajes WM_* | 7 | 3 | pendiente | qwindows.h |
| `WM_DESTROY` | Mensajes WM_* | 6 | 5 | pendiente | qwindows.h |
| `WM_ERASEBKGND` | Mensajes WM_* | 6 | 5 | pendiente | qwindows.h |
| `WM_LBUTTONUP` | Mensajes WM_* | 5 | 5 | pendiente | qwindows.h |
| `WM_SETCURSOR` | Mensajes WM_* | 5 | 4 | pendiente | qwindows.h |
| `WM_VSCROLL` | Mensajes WM_* | 5 | 4 | pendiente | qwindows.h |
| `WM_WININICHANGE` | Mensajes WM_* | 5 | 5 | pendiente | qwindows.h |
| `WM_KILLFOCUS` | Mensajes WM_* | 4 | 4 | pendiente | qwindows.h |
| `WM_MENUCHAR` | Mensajes WM_* | 4 | 1 | pendiente | qwindows.h |
| `WM_MOUSEACTIVATE` | Mensajes WM_* | 4 | 2 | pendiente | qwindows.h |
| `WM_SETFOCUS` | Mensajes WM_* | 4 | 4 | pendiente | qwindows.h |
| `WM_ACTIVATEAPP` | Mensajes WM_* | 3 | 2 | pendiente | qwindows.h |
| `WM_ENABLE` | Mensajes WM_* | 3 | 2 | pendiente | qwindows.h |
| `WM_MBUTTONDOWN` | Mensajes WM_* | 3 | 1 | pendiente | qwindows.h |
| `WM_MOUSELAST` | Mensajes WM_* | 3 | 2 | pendiente | qwindows.h |
| `WM_MOVE` | Mensajes WM_* | 3 | 3 | pendiente | qwindows.h |
| `WM_NCCREATE` | Mensajes WM_* | 3 | 3 | pendiente | qwindows.h |
| `WM_NCLBUTTONDBLCLK` | Mensajes WM_* | 3 | 3 | pendiente | qwindows.h |
| `WM_NCMOUSEMOVE` | Mensajes WM_* | 3 | 2 | pendiente | qwindows.h |
| `WM_NCRBUTTONDOWN` | Mensajes WM_* | 3 | 2 | pendiente | qwindows.h |
| `WM_QUIT` | Mensajes WM_* | 3 | 2 | pendiente | qwindows.h |
| `WM_RBUTTONUP` | Mensajes WM_* | 3 | 2 | pendiente | qwindows.h |
| `WM_SYSCHAR` | Mensajes WM_* | 3 | 1 | pendiente | qwindows.h |
| `WM_SYSDEADCHAR` | Mensajes WM_* | 3 | 1 | pendiente | qwindows.h |
| `WM_TIMER` | Mensajes WM_* | 3 | 3 | pendiente | qwindows.h |
| `WM_ACTIVATE` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_CLEAR` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_COPY` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_CUT` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_DEVMODECHANGE` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_ENDSESSION` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_ENTERIDLE` | Mensajes WM_* | 2 | 1 | pendiente | qwindows.h |
| `WM_FONTCHANGE` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_GETDLGCODE` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_INITMENUPOPUP` | Mensajes WM_* | 2 | 1 | pendiente | qwindows.h |
| `WM_KEYFIRST` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_KEYLAST` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_MENUSELECT` | Mensajes WM_* | 2 | 1 | pendiente | qwindows.h |
| `WM_NCDESTROY` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_NCMBUTTONDOWN` | Mensajes WM_* | 2 | 1 | pendiente | qwindows.h |
| `WM_NCPAINT` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_PASTE` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_QUERYENDSESSION` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_QUEUESYNC` | Mensajes WM_* | 2 | 1 | pendiente | qwindows.h |
| `WM_SYSCOLORCHANGE` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_SYSTEMERROR` | Mensajes WM_* | 2 | 2 | pendiente | qwindows.h |
| `WM_ASKCBFORMATNAME` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_CHILDACTIVATE` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_CTLCOLOR` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_DESTROYCLIPBOARD` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_GETMINMAXINFO` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_GETTEXT` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_GETTEXTLENGTH` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_HSCROLLCLIPBOARD` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_MBUTTONUP` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_MOUSEFIRST` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_NCACTIVATE` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_NCCALCSIZE` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_NCLBUTTONUP` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_NCMBUTTONDBLCLK` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_NCMBUTTONUP` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_NCRBUTTONDBLCLK` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_NCRBUTTONUP` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_PAINTCLIPBOARD` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_PAINTICON` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_RBUTTONDBLCLK` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_RENDERFORMAT` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_SETREDRAW` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_SETTEXT` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_SETVISIBLE` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_SHOWWINDOW` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_SIZECLIPBOARD` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `WM_VSCROLLCLIPBOARD` | Mensajes WM_* | 1 | 1 | pendiente | qwindows.h |
| `ExtTextOut` | GDI texto/fuentes | 55 | 14 | pendiente | debugwin.h |
| `SetBkColor` | GDI texto/fuentes | 30 | 9 | pendiente | debugwin.h |
| `TextOut` | GDI texto/fuentes | 25 | 11 | pendiente | debugwin.h |
| `GetTextExtent` | GDI texto/fuentes | 24 | 8 | pendiente | debugwin.h |
| `SetTextColor` | GDI texto/fuentes | 15 | 7 | pendiente | debugwin.h |
| `SetBkMode` | GDI texto/fuentes | 13 | 5 | pendiente | debugwin.h |
| `CreateFontIndirect` | GDI texto/fuentes | 3 | 3 | pendiente | debugwin.h |
| `GrayString` | GDI texto/fuentes | 3 | 2 | pendiente | debugwin.h |
| `AddFontResource` | GDI texto/fuentes | 1 | 1 | pendiente | debugwin.h |
| `SetMapperFlags` | GDI texto/fuentes | 1 | 1 | pendiente | debugwin.h |
| `SelectObject` | GDI dibujo | 141 | 27 | pendiente | debugwin.h |
| `PatBlt` | GDI dibujo | 92 | 19 | pendiente | debugwin.h |
| `MoveTo` | GDI dibujo | 57 | 7 | pendiente | debugwin.h |
| `ReleaseDC` | GDI dibujo | 51 | 24 | pendiente | debugwin.h |
| `LineTo` | GDI dibujo | 47 | 8 | pendiente | debugwin.h |
| `GetDC` | GDI dibujo | 46 | 23 | pendiente | debugwin.h |
| `DeleteObject` | GDI dibujo | 40 | 11 | pendiente | debugwin.h |
| `InvalidateRect` | GDI dibujo | 24 | 16 | pendiente | debugwin.h |
| `FillRect` | GDI dibujo | 23 | 6 | pendiente | debugwin.h |
| `BitBlt` | GDI dibujo | 14 | 11 | pendiente | debugwin.h |
| `RestoreDC` | GDI dibujo | 13 | 8 | pendiente | debugwin.h |
| `DeleteDC` | GDI dibujo | 12 | 6 | pendiente | debugwin.h |
| `SaveDC` | GDI dibujo | 12 | 8 | pendiente | debugwin.h |
| `BeginPaint` | GDI dibujo | 11 | 10 | pendiente | debugwin.h |
| `EndPaint` | GDI dibujo | 11 | 10 | pendiente | debugwin.h |
| `GetStockObject` | GDI dibujo | 11 | 8 | pendiente | debugwin.h |
| `CreateBitmap` | GDI dibujo | 10 | 8 | pendiente | debugwin.h |
| `IntersectClipRect` | GDI dibujo | 10 | 6 | pendiente | debugwin.h |
| `InvertRect` | GDI dibujo | 9 | 4 | pendiente | debugwin.h |
| `CombineRgn` | GDI dibujo | 7 | 3 | pendiente | debugwin.h |
| `CreateCompatibleDC` | GDI dibujo | 7 | 5 | pendiente | debugwin.h |
| `CreateRectRgn` | GDI dibujo | 7 | 3 | pendiente | debugwin.h |
| `GetClipBox` | GDI dibujo | 6 | 3 | pendiente | wine-headers |
| `ScrollDC` | GDI dibujo | 6 | 6 | pendiente | debugwin.h |
| `StretchBlt` | GDI dibujo | 6 | 5 | pendiente | debugwin.h |
| `CreateCompatibleBitmap` | GDI dibujo | 5 | 4 | pendiente | debugwin.h |
| `CreateSolidBrush` | GDI dibujo | 5 | 5 | pendiente | debugwin.h |
| `LoadBitmap` | GDI dibujo | 5 | 3 | pendiente | debugwin.h |
| `CreatePen` | GDI dibujo | 4 | 4 | pendiente | debugwin.h |
| `GetUpdateRect` | GDI dibujo | 4 | 3 | pendiente | debugwin.h |
| `GetWindowDC` | GDI dibujo | 4 | 3 | pendiente | debugwin.h |
| `ValidateRect` | GDI dibujo | 4 | 4 | pendiente | debugwin.h |
| `CreateDIBitmap` | GDI dibujo | 3 | 3 | pendiente | wine-headers |
| `FrameRect` | GDI dibujo | 3 | 2 | pendiente | wine-headers |
| `GetSysColorBrush` | GDI dibujo | 3 | 1 | pendiente | wine-headers |
| `Polygon` | GDI dibujo | 3 | 1 | pendiente | wine-headers |
| `Rectangle` | GDI dibujo | 3 | 1 | pendiente | wine-headers |
| `CreatePatternBrush` | GDI dibujo | 2 | 2 | pendiente | debugwin.h |
| `GetBitmapBits` | GDI dibujo | 2 | 2 | pendiente | debugwin.h |
| `CreateBitmapIndirect` | GDI dibujo | 1 | 1 | pendiente | debugwin.h |
| `CreateIC` | GDI dibujo | 1 | 1 | pendiente | debugwin.h |
| `CreatePenIndirect` | GDI dibujo | 1 | 1 | pendiente | debugwin.h |
| `CreateRectRgnIndirect` | GDI dibujo | 1 | 1 | pendiente | debugwin.h |
| `DrawIconEx` | GDI dibujo | 1 | 1 | pendiente | wine-headers |
| `FillRgn` | GDI dibujo | 1 | 1 | pendiente | debugwin.h |
| `GetMapMode` | GDI dibujo | 1 | 1 | pendiente | wine-headers |
| `ScrollWindow` | GDI dibujo | 1 | 1 | pendiente | debugwin.h |
| `SetMapMode` | GDI dibujo | 1 | 1 | pendiente | wine-headers |
| `ChangeMenu` | Diálogos/menús | 35 | 8 | pendiente | debugwin.h |
| `GetMenuState` | Diálogos/menús | 9 | 3 | pendiente | debugwin.h |
| `GetMenuItemCount` | Diálogos/menús | 7 | 4 | pendiente | debugwin.h |
| `CreateMenu` | Diálogos/menús | 5 | 3 | pendiente | debugwin.h |
| `GetDlgCtrlID` | Diálogos/menús | 4 | 2 | pendiente | wine-headers |
| `GetMenuString` | Diálogos/menús | 4 | 3 | pendiente | debugwin.h |
| `DestroyMenu` | Diálogos/menús | 3 | 2 | pendiente | debugwin.h |
| `GetMenuItemId` | Diálogos/menús | 3 | 2 | pendiente | debugwin.h |
| `EnableMenuItem` | Diálogos/menús | 2 | 2 | pendiente | wine-headers |
| `IsMenu` | Diálogos/menús | 2 | 1 | pendiente | wine-headers |
| `SetMenu` | Diálogos/menús | 2 | 2 | pendiente | debugwin.h |
| `CheckMenuItem` | Diálogos/menús | 1 | 1 | pendiente | debugwin.h |
| `LoadMenu` | Diálogos/menús | 1 | 1 | pendiente | debugwin.h |
| `CloseClipboard` | Portapapeles | 10 | 6 | pendiente | debugwin.h |
| `OpenClipboard` | Portapapeles | 8 | 7 | pendiente | debugwin.h |
| `EmptyClipboard` | Portapapeles | 5 | 5 | pendiente | debugwin.h |
| `SetClipboardData` | Portapapeles | 4 | 4 | pendiente | debugwin.h |
| `GetClipboardData` | Portapapeles | 2 | 2 | pendiente | debugwin.h |
| `EnumClipboardFormats` | Portapapeles | 1 | 1 | pendiente | wine-headers |
| `Escape` | Impresión | 18 | 5 | pendiente | debugwin.h |
| `CreateDC` | Impresión | 1 | 1 | pendiente | debugwin.h |
| `GetKeyState` | Entrada/cursor | 24 | 5 | pendiente | wine-headers |
| `ReleaseCapture` | Entrada/cursor | 18 | 15 | pendiente | debugwin.h |
| `SetCapture` | Entrada/cursor | 15 | 14 | pendiente | debugwin.h |
| `ShowCursor` | Entrada/cursor | 11 | 2 | pendiente | debugwin.h |
| `SetCursor` | Entrada/cursor | 10 | 8 | pendiente | debugwin.h |
| `GetMessageTime` | Entrada/cursor | 8 | 6 | pendiente | wine-headers |
| `HideCaret` | Entrada/cursor | 6 | 2 | pendiente | debugwin.h |
| `ShowCaret` | Entrada/cursor | 6 | 2 | pendiente | debugwin.h |
| `LoadCursor` | Entrada/cursor | 3 | 2 | pendiente | debugwin.h |
| `VkKeyScanA` | Entrada/cursor | 3 | 1 | pendiente | wine-headers |
| `SetKeyboardState` | Entrada/cursor | 2 | 2 | pendiente | wine-headers |
| `CreateCaret` | Entrada/cursor | 1 | 1 | pendiente | debugwin.h |
| `DestroyCaret` | Entrada/cursor | 1 | 1 | pendiente | debugwin.h |
| `GetCapture` | Entrada/cursor | 1 | 1 | pendiente | wine-headers |
| `GetCursorPos` | Entrada/cursor | 1 | 1 | pendiente | wine-headers |
| `GetDoubleClickTime` | Entrada/cursor | 1 | 1 | pendiente | wine-headers |
| `GetProfileString` | Persistencia de configuración | 17 | 7 | pendiente | debugwin.h |
| `GetProfileInt` | Persistencia de configuración | 14 | 6 | pendiente | debugwin.h |
| `WriteProfileString` | Persistencia de configuración | 11 | 6 | pendiente | debugwin.h |
| `GetEnvironmentVariableA` | Persistencia de configuración | 1 | 1 | pendiente | wine-headers |
| `HWND` | Tipos HANDLE-like | 616 | 95 | pendiente | qwindows.h |
| `HDC` | Tipos HANDLE-like | 228 | 45 | pendiente | qwindows.h |
| `HANDLE` | Tipos HANDLE-like | 222 | 65 | pendiente | qwindows.h |
| `HCURSOR` | Tipos HANDLE-like | 91 | 31 | pendiente | qwindows.h |
| `HBRUSH` | Tipos HANDLE-like | 75 | 25 | pendiente | qwindows.h |
| `HMENU` | Tipos HANDLE-like | 64 | 21 | pendiente | qwindows.h |
| `HIWORD` | Tipos HANDLE-like | 51 | 16 | pendiente | qwindows.h |
| `HBITMAP` | Tipos HANDLE-like | 26 | 13 | pendiente | qwindows.h |
| `HRGN` | Tipos HANDLE-like | 26 | 10 | pendiente | qwindows.h |
| `HFONT` | Tipos HANDLE-like | 21 | 11 | pendiente | qwindows.h |
| `HICON` | Tipos HANDLE-like | 4 | 3 | pendiente | qwindows.h |
| `HPEN` | Tipos HANDLE-like | 3 | 3 | pendiente | qwindows.h |
| `HIBYTE` | Tipos HANDLE-like | 2 | 1 | pendiente | qwindows.h |
| `HORZRES` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HORZSIZE` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTBOTTOM` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTCAPTION` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTCLIENT` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTHSCROLL` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTLEFT` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTMENU` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTREDUCE` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTRIGHT` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTSIZE` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTSYSMENU` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTTOP` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTTOPLEFT` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTVSCROLL` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `HTZOOM` | Tipos HANDLE-like | 1 | 1 | pendiente | qwindows.h |
| `BOOL` | Tipos primitivos | 1926 | 160 | pendiente | qwindows.h |
| `WORD` | Tipos primitivos | 449 | 88 | pendiente | qwindows.h |
| `LPSTR` | Tipos primitivos | 412 | 66 | pendiente | qwindows.h |
| `LONG` | Tipos primitivos | 142 | 46 | pendiente | qwindows.h |
| `FARPROC` | Tipos primitivos | 91 | 25 | pendiente | qwindows.h |
| `BYTE` | Tipos primitivos | 67 | 29 | pendiente | qwindows.h |
| `DWORD` | Tipos primitivos | 50 | 23 | pendiente | qwindows.h |
| `LPINT` | Tipos primitivos | 36 | 8 | pendiente | qwindows.h |
| `ATOM` | Tipos primitivos | 26 | 4 | pendiente | qwindows.h |
| `LPRECT` | Geometría | 238 | 48 | pendiente | qwindows.h |
| `LPPOINT` | Geometría | 42 | 11 | pendiente | qwindows.h |
| `ScreenToClient` | Geometría | 24 | 12 | pendiente | wine-headers |
| `ClientToScreen` | Geometría | 17 | 6 | pendiente | wine-headers |
| `PtInRect` | Geometría | 4 | 3 | pendiente | wine-headers |
| `AdjustWindowRect` | Geometría | 2 | 2 | pendiente | wine-headers |
| `UnionRect` | Geometría | 2 | 2 | pendiente | wine-headers |
| `IntersectRect` | Geometría | 1 | 1 | pendiente | wine-headers |
| `HUGE` | Convenciones ABI | 456 | 74 | resuelto (Winelib) | opus_x64_compat.h |
| `FAR` | Convenciones ABI | 404 | 70 | resuelto (Winelib) | qwindows.h |
| `huge` | Convenciones ABI | 339 | 23 | resuelto (Winelib) | opus_x64_compat.h |
| `EXPORT` | Convenciones ABI | 295 | 73 | resuelto (Winelib) | qwindows.h |
| `far` | Convenciones ABI | 252 | 65 | resuelto (Winelib) | opus_x64_compat.h |
| `NATIVE` | Convenciones ABI | 209 | 52 | resuelto (Winelib) | qwindows.h |
| `PASCAL` | Convenciones ABI | 82 | 27 | resuelto (Winelib) | qwindows.h |
| `pascal` | Convenciones ABI | 9 | 5 | resuelto (Winelib) | opus_x64_compat.h |
| `NEAR` | Convenciones ABI | 6 | 3 | resuelto (Winelib) | qwindows.h |
| `export` | Convenciones ABI | 3 | 3 | resuelto (Winelib) | opus_x64_compat.h |
| `native` | Convenciones ABI | 3 | 3 | resuelto (Winelib) | opus_x64_compat.h |
| `NULL` | Constantes Win16 | 2037 | 165 | pendiente | qwindows.h |
| `max` | Constantes Win16 | 457 | 72 | pendiente | qwindows.h |
| `min` | Constantes Win16 | 349 | 92 | pendiente | qwindows.h |
| `FALSE` | Constantes Win16 | 295 | 26 | pendiente | qwindows.h |
| `TRUE` | Constantes Win16 | 190 | 29 | pendiente | qwindows.h |
| `Beep` | Constantes Win16 | 179 | 60 | pendiente | wine-headers |
| `LOWORD` | Constantes Win16 | 97 | 22 | pendiente | qwindows.h |
| `SendMessage` | Constantes Win16 | 93 | 33 | pendiente | debugwin.h |
| `GetWindowWord` | Constantes Win16 | 63 | 11 | pendiente | wine-headers |
| `GetClientRect` | Constantes Win16 | 55 | 24 | pendiente | wine-headers |
| `PostMessage` | Constantes Win16 | 54 | 14 | pendiente | debugwin.h |
| `IDYES` | Constantes Win16 | 51 | 25 | pendiente | qwindows.h |
| `LPMSG` | Constantes Win16 | 50 | 13 | pendiente | qwindows.h |
| `PATCOPY` | Constantes Win16 | 50 | 12 | pendiente | qwindows.h |
| `ETO_OPAQUE` | Constantes Win16 | 39 | 8 | pendiente | qwindows.h |
| `DeleteAtom` | Constantes Win16 | 38 | 3 | pendiente | wine-headers |
| `VOID` | Constantes Win16 | 38 | 12 | pendiente | qwindows.h |
| `GetTickCount` | Constantes Win16 | 36 | 13 | pendiente | wine-headers |
| `GetWindowRect` | Constantes Win16 | 36 | 18 | pendiente | wine-headers |
| `SetRect` | Constantes Win16 | 34 | 14 | pendiente | wine-headers |
| `MAKELONG` | Constantes Win16 | 31 | 13 | pendiente | qwindows.h |
| `GWL_STYLE` | Constantes Win16 | 29 | 10 | pendiente | qwindows.h |
| `GetSystemMetrics` | Constantes Win16 | 29 | 8 | pendiente | wine-headers |
| `PATINVERT` | Constantes Win16 | 29 | 8 | pendiente | qwindows.h |
| `IDNO` | Constantes Win16 | 25 | 13 | pendiente | qwindows.h |
| `GetProcAddress` | Constantes Win16 | 24 | 12 | pendiente | wine-headers |
| `MB_OK` | Constantes Win16 | 22 | 15 | pendiente | qwindows.h |
| `SetWindowWord` | Constantes Win16 | 22 | 9 | pendiente | wine-headers |
| `MAKEINTRESOURCE` | Constantes Win16 | 21 | 11 | pendiente | qwindows.h |
| `LPPAINTSTRUCT` | Constantes Win16 | 20 | 9 | pendiente | qwindows.h |
| `SHOW_OPENWINDOW` | Constantes Win16 | 20 | 10 | pendiente | qwindows.h |
| `IDCANCEL` | Constantes Win16 | 19 | 12 | pendiente | qwindows.h |
| `MF_BYPOSITION` | Constantes Win16 | 19 | 5 | pendiente | qwindows.h |
| `OpenFile` | Constantes Win16 | 18 | 9 | pendiente | debugwin.h |
| `InflateRect` | Constantes Win16 | 17 | 6 | pendiente | wine-headers |
| `LF_FACESIZE` | Constantes Win16 | 17 | 7 | pendiente | qwindows.h |
| `MB_SYSTEMMODAL` | Constantes Win16 | 17 | 9 | pendiente | qwindows.h |
| `GetAsyncKeyState` | Constantes Win16 | 16 | 6 | pendiente | wine-headers |
| `GetParent` | Constantes Win16 | 16 | 5 | pendiente | wine-headers |
| `WHITENESS` | Constantes Win16 | 16 | 7 | pendiente | qwindows.h |
| `GetDeviceCaps` | Constantes Win16 | 15 | 2 | pendiente | wine-headers |
| `OffsetRect` | Constantes Win16 | 15 | 8 | pendiente | wine-headers |
| `VK_MENU` | Constantes Win16 | 15 | 3 | pendiente | qwindows.h |
| `VK_SHIFT` | Constantes Win16 | 15 | 6 | pendiente | qwindows.h |
| `WS_CHILD` | Constantes Win16 | 15 | 8 | pendiente | qwindows.h |
| `MF_APPEND` | Constantes Win16 | 14 | 5 | pendiente | qwindows.h |
| `VK_ESCAPE` | Constantes Win16 | 14 | 8 | pendiente | qwindows.h |
| `MAKEPOINT` | Constantes Win16 | 13 | 6 | pendiente | qwindows.h |
| `MB_ICONQUESTION` | Constantes Win16 | 13 | 10 | pendiente | qwindows.h |
| `MB_YESNO` | Constantes Win16 | 13 | 11 | pendiente | qwindows.h |
| `MF_CHANGE` | Constantes Win16 | 13 | 5 | pendiente | qwindows.h |
| `SB_PAGEUP` | Constantes Win16 | 13 | 5 | pendiente | qwindows.h |
| `SC_MAXIMIZE` | Constantes Win16 | 13 | 7 | pendiente | qwindows.h |
| `SC_RESTORE` | Constantes Win16 | 13 | 7 | pendiente | qwindows.h |
| `MB_ICONHAND` | Constantes Win16 | 12 | 7 | pendiente | qwindows.h |
| `MF_POPUP` | Constantes Win16 | 12 | 8 | pendiente | qwindows.h |
| `SC_SIZE` | Constantes Win16 | 12 | 8 | pendiente | qwindows.h |
| `VK_CONTROL` | Constantes Win16 | 12 | 4 | pendiente | qwindows.h |
| `WS_CLIPSIBLINGS` | Constantes Win16 | 12 | 6 | pendiente | qwindows.h |
| `GetSubMenu` | Constantes Win16 | 11 | 9 | pendiente | wine-headers |
| `LPTEXTMETRIC` | Constantes Win16 | 11 | 8 | pendiente | qwindows.h |
| `ETO_CLIPPED` | Constantes Win16 | 10 | 6 | pendiente | qwindows.h |
| `FreeLibrary` | Constantes Win16 | 10 | 6 | pendiente | wine-headers |
| `MB_DEFBUTTON2` | Constantes Win16 | 10 | 4 | pendiente | qwindows.h |
| `MF_BYCOMMAND` | Constantes Win16 | 10 | 4 | pendiente | qwindows.h |
| `PM_REMOVE` | Constantes Win16 | 10 | 6 | pendiente | qwindows.h |
| `SB_PAGEDOWN` | Constantes Win16 | 10 | 5 | pendiente | qwindows.h |
| `SC_MINIMIZE` | Constantes Win16 | 10 | 7 | pendiente | qwindows.h |
| `SC_MOVE` | Constantes Win16 | 10 | 6 | pendiente | qwindows.h |
| `SWP_NOACTIVATE` | Constantes Win16 | 10 | 6 | pendiente | qwindows.h |
| `VK_F1` | Constantes Win16 | 10 | 5 | pendiente | qwindows.h |
| `LPOFSTRUCT` | Constantes Win16 | 9 | 5 | pendiente | qwindows.h |
| `MoveToEx` | Constantes Win16 | 9 | 2 | pendiente | wine-headers |
| `SB_LINEUP` | Constantes Win16 | 9 | 4 | pendiente | qwindows.h |
| `SC_KEYMENU` | Constantes Win16 | 9 | 5 | pendiente | qwindows.h |
| `SetRectRgn` | Constantes Win16 | 9 | 2 | pendiente | wine-headers |
| `WH_MSGFILTER` | Constantes Win16 | 9 | 2 | pendiente | qwindows.h |
| `WS_MAXIMIZE` | Constantes Win16 | 9 | 5 | pendiente | qwindows.h |
| `BLACKNESS` | Constantes Win16 | 8 | 2 | pendiente | qwindows.h |
| `GetFocus` | Constantes Win16 | 8 | 7 | pendiente | wine-headers |
| `GetWindow` | Constantes Win16 | 8 | 3 | pendiente | wine-headers |
| `MB_YESNOCANCEL` | Constantes Win16 | 8 | 6 | pendiente | qwindows.h |
| `SB_LINEDOWN` | Constantes Win16 | 8 | 4 | pendiente | qwindows.h |
| `SRCCOPY` | Constantes Win16 | 8 | 7 | pendiente | qwindows.h |
| `TRANSPARENT` | Constantes Win16 | 8 | 5 | pendiente | qwindows.h |
| `WS_BORDER` | Constantes Win16 | 8 | 6 | pendiente | qwindows.h |
| `ANSI_CHARSET` | Constantes Win16 | 7 | 3 | pendiente | qwindows.h |
| `CS_DBLCLKS` | Constantes Win16 | 7 | 1 | pendiente | qwindows.h |
| `DebugBreak` | Constantes Win16 | 7 | 6 | pendiente | wine-headers |
| `GW_HWNDNEXT` | Constantes Win16 | 7 | 6 | pendiente | qwindows.h |
| `MB_APPLMODAL` | Constantes Win16 | 7 | 5 | pendiente | qwindows.h |
| `MF_DELETE` | Constantes Win16 | 7 | 2 | pendiente | qwindows.h |
| `MF_SEPARATOR` | Constantes Win16 | 7 | 4 | pendiente | qwindows.h |
| `MF_STRING` | Constantes Win16 | 7 | 3 | pendiente | qwindows.h |
| `OF_WRITE` | Constantes Win16 | 7 | 2 | pendiente | qwindows.h |
| `SB_THUMBPOSITION` | Constantes Win16 | 7 | 5 | pendiente | qwindows.h |
| `SetCursorPos` | Constantes Win16 | 7 | 4 | pendiente | wine-headers |
| `VK_LEFT` | Constantes Win16 | 7 | 3 | pendiente | qwindows.h |
| `VK_TAB` | Constantes Win16 | 7 | 3 | pendiente | qwindows.h |
| `VK_UP` | Constantes Win16 | 7 | 4 | pendiente | qwindows.h |
| `WS_VISIBLE` | Constantes Win16 | 7 | 6 | pendiente | qwindows.h |
| `CBM_INIT` | Constantes Win16 | 6 | 3 | pendiente | qwindows.h |
| `CF_TEXT` | Constantes Win16 | 6 | 3 | pendiente | qwindows.h |
| `COLOR_WINDOW` | Constantes Win16 | 6 | 3 | pendiente | qwindows.h |
| `DIB_RGB_COLORS` | Constantes Win16 | 6 | 3 | pendiente | qwindows.h |
| `GetSysColor` | Constantes Win16 | 6 | 3 | pendiente | wine-headers |
| `IDRETRY` | Constantes Win16 | 6 | 4 | pendiente | qwindows.h |
| `LB_ERR` | Constantes Win16 | 6 | 3 | pendiente | qwindows.h |
| `MB_DEFBUTTON1` | Constantes Win16 | 6 | 5 | pendiente | qwindows.h |
| `OF_CREATE` | Constantes Win16 | 6 | 5 | pendiente | qwindows.h |
| `OF_READ` | Constantes Win16 | 6 | 5 | pendiente | qwindows.h |
| `PM_NOREMOVE` | Constantes Win16 | 6 | 3 | pendiente | qwindows.h |
| `SC_CLOSE` | Constantes Win16 | 6 | 4 | pendiente | qwindows.h |
| `SWP_NOMOVE` | Constantes Win16 | 6 | 5 | pendiente | qwindows.h |
| `SWP_NOSIZE` | Constantes Win16 | 6 | 5 | pendiente | qwindows.h |
| `UnhookWindowsHook` | Constantes Win16 | 6 | 3 | pendiente | debugwin.h |
| `VARIABLE_PITCH` | Constantes Win16 | 6 | 4 | pendiente | qwindows.h |
| `VK_CANCEL` | Constantes Win16 | 6 | 5 | pendiente | qwindows.h |
| `BM_SETCHECK` | Constantes Win16 | 5 | 1 | pendiente | qwindows.h |
| `DSTINVERT` | Constantes Win16 | 5 | 5 | pendiente | qwindows.h |
| `GMEM_DISCARDABLE` | Constantes Win16 | 5 | 2 | pendiente | qwindows.h |
| `GetSystemMenu` | Constantes Win16 | 5 | 5 | pendiente | wine-headers |
| `IDOK` | Constantes Win16 | 5 | 3 | pendiente | qwindows.h |
| `KillTimer` | Constantes Win16 | 5 | 5 | pendiente | wine-headers |
| `MB_DEFBUTTON3` | Constantes Win16 | 5 | 4 | pendiente | qwindows.h |
| `MB_ICONEXCLAMATION` | Constantes Win16 | 5 | 5 | pendiente | qwindows.h |
| `MSGF_MENU` | Constantes Win16 | 5 | 1 | pendiente | qwindows.h |
| `SC_NEXTWINDOW` | Constantes Win16 | 5 | 4 | pendiente | qwindows.h |
| `SC_PREVWINDOW` | Constantes Win16 | 5 | 4 | pendiente | qwindows.h |
| `SW_SHOWNORMAL` | Constantes Win16 | 5 | 4 | pendiente | qwindows.h |
| `WS_CLIPCHILDREN` | Constantes Win16 | 5 | 4 | pendiente | qwindows.h |
| `BLACK_PEN` | Constantes Win16 | 4 | 4 | pendiente | qwindows.h |
| `FIXED_PITCH` | Constantes Win16 | 4 | 4 | pendiente | qwindows.h |
| `GW_HWNDFIRST` | Constantes Win16 | 4 | 3 | pendiente | qwindows.h |
| `GetCaretBlinkTime` | Constantes Win16 | 4 | 3 | pendiente | wine-headers |
| `GetTextColor` | Constantes Win16 | 4 | 3 | pendiente | wine-headers |
| `GlobalDiscard` | Constantes Win16 | 4 | 2 | pendiente | qwindows.h |
| `IDABORT` | Constantes Win16 | 4 | 4 | pendiente | qwindows.h |
| `IDIGNORE` | Constantes Win16 | 4 | 4 | pendiente | qwindows.h |
| `IsWindowEnabled` | Constantes Win16 | 4 | 2 | pendiente | wine-headers |
| `LoadLibrary` | Constantes Win16 | 4 | 4 | pendiente | debugwin.h |
| `MA_ACTIVATE` | Constantes Win16 | 4 | 1 | pendiente | qwindows.h |
| `MA_NOACTIVATE` | Constantes Win16 | 4 | 1 | pendiente | qwindows.h |
| `MB_ABORTRETRYIGNORE` | Constantes Win16 | 4 | 4 | pendiente | qwindows.h |
| `MB_ICONMASK` | Constantes Win16 | 4 | 2 | pendiente | qwindows.h |
| `MF_DISABLED` | Constantes Win16 | 4 | 2 | pendiente | qwindows.h |
| `OF_EXIST` | Constantes Win16 | 4 | 2 | pendiente | qwindows.h |
| `OF_READWRITE` | Constantes Win16 | 4 | 3 | pendiente | qwindows.h |
| `RGB` | Constantes Win16 | 4 | 2 | pendiente | qwindows.h |
| `SIMPLEREGION` | Constantes Win16 | 4 | 2 | pendiente | qwindows.h |
| `SM_CYMENU` | Constantes Win16 | 4 | 3 | pendiente | qwindows.h |
| `SWP_NOZORDER` | Constantes Win16 | 4 | 3 | pendiente | qwindows.h |
| `SW_HIDE` | Constantes Win16 | 4 | 4 | pendiente | qwindows.h |
| `VK_DOWN` | Constantes Win16 | 4 | 3 | pendiente | qwindows.h |
| `VK_END` | Constantes Win16 | 4 | 2 | pendiente | qwindows.h |
| `VK_F10` | Constantes Win16 | 4 | 1 | pendiente | qwindows.h |
| `VK_NUMPAD0` | Constantes Win16 | 4 | 3 | pendiente | qwindows.h |
| `VK_NUMPAD5` | Constantes Win16 | 4 | 2 | pendiente | qwindows.h |
| `VK_NUMPAD9` | Constantes Win16 | 4 | 3 | pendiente | qwindows.h |
| `WS_TABSTOP` | Constantes Win16 | 4 | 1 | pendiente | qwindows.h |
| `CF_BITMAP` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `COLOR_BTNFACE` | Constantes Win16 | 3 | 1 | pendiente | qwindows.h |
| `DrawMenuBar` | Constantes Win16 | 3 | 3 | pendiente | wine-headers |
| `ERROR` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `EndMenu` | Constantes Win16 | 3 | 2 | pendiente | wine-headers |
| `FF_ROMAN` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `FF_SWISS` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `GHND` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `HIDE_WINDOW` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `IsZoomed` | Constantes Win16 | 3 | 1 | pendiente | wine-headers |
| `LPLOGFONT` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `MA_ACTIVATEANDEAT` | Constantes Win16 | 3 | 1 | pendiente | qwindows.h |
| `MB_ICONASTERISK` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `MB_OKCANCEL` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `MB_TYPEMASK` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `MF_GRAYED` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `MF_REMOVE` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `MF_SYSMENU` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `MSGF_DIALOGBOX` | Constantes Win16 | 3 | 1 | pendiente | qwindows.h |
| `NUMCOLORS` | Constantes Win16 | 3 | 1 | pendiente | qwindows.h |
| `OBM_BTSIZE` | Constantes Win16 | 3 | 1 | pendiente | qwindows.h |
| `OF_REOPEN` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `PASSTHROUGH` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `PM_NOYIELD` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `RGN_DIFF` | Constantes Win16 | 3 | 1 | pendiente | qwindows.h |
| `SBS_VERT` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `SC_ARRANGE` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `SC_HSCROLL` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `SC_MOUSEMENU` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `SC_VSCROLL` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `SW_SHOW` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `SetStretchBltMode` | Constantes Win16 | 3 | 2 | pendiente | wine-headers |
| `VERTRES` | Constantes Win16 | 3 | 1 | pendiente | qwindows.h |
| `VK_CAPITAL` | Constantes Win16 | 3 | 1 | pendiente | qwindows.h |
| `VK_DECIMAL` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `VK_F2` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `VK_HOME` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `VK_LBUTTON` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `VK_RBUTTON` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `VK_RIGHT` | Constantes Win16 | 3 | 3 | pendiente | qwindows.h |
| `WHITE_BRUSH` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `WS_SIZEBOX` | Constantes Win16 | 3 | 2 | pendiente | qwindows.h |
| `BANDINFO` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `BLACK_BRUSH` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `COLOR_APPWORKSPACE` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `CS_HREDRAW` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `CW_USEDEFAULT` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `DEFAULT_PITCH` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `DLGC_WANTARROWS` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `DRAFTMODE` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `DrawEdge` | Constantes Win16 | 2 | 1 | pendiente | wine-headers |
| `FF_DECORATIVE` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `FF_MODERN` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `FlashWindow` | Constantes Win16 | 2 | 2 | pendiente | wine-headers |
| `GMEM_LOCKCOUNT` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `GMEM_LOWER` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `GMEM_MODIFY` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `GMEM_NOT_BANKED` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `GMEM_SHARE` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `GWL_WNDPROC` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `GetBkColor` | Constantes Win16 | 2 | 1 | pendiente | wine-headers |
| `HTBOTTOMRIGHT` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `HiliteMenuItem` | Constantes Win16 | 2 | 1 | pendiente | wine-headers |
| `IDC_ARROW` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `LOBYTE` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `LOGPIXELSX` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `LOGPIXELSY` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `LPCREATESTRUCT` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `LPWNDCLASS` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `MB_DEFMASK` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `MB_MODEMASK` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `MB_RETRYCANCEL` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `MF_CHECKED` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `MF_ENABLED` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `MF_INSERT` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `MK_SHIFT` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `OBM_DNARROW` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `OBM_LFARROW` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `OBM_RGARROW` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `OBM_UPARROW` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `OEM_CHARSET` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `OF_PROMPT` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `OPAQUE` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `PATTERN` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `PS_SOLID` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SM_CURSORLEVEL` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `SM_CXDLGFRAME` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SM_CXFULLSCREEN` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SM_CYDLGFRAME` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SM_CYVSCROLL` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SM_CYVTHUMB` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SRCAND` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SRCINVERT` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `SWP_NOREDRAW` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SYSTEM_FONT` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `SetCaretPos` | Constantes Win16 | 2 | 1 | pendiente | wine-headers |
| `SetROP2` | Constantes Win16 | 2 | 1 | pendiente | wine-headers |
| `SetScrollPos` | Constantes Win16 | 2 | 1 | pendiente | wine-headers |
| `VK_CLEAR` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_F11` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_NUMLOCK` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_NUMPAD1` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_NUMPAD2` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_NUMPAD3` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_NUMPAD4` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_NUMPAD6` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_NUMPAD7` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_NUMPAD8` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_PRIOR` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `VK_RETURN` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `WH_JOURNALPLAYBACK` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `WS_MAXIMIZEBOX` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `WS_MINIMIZE` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `WS_POPUP` | Constantes Win16 | 2 | 2 | pendiente | qwindows.h |
| `WS_SYSMENU` | Constantes Win16 | 2 | 1 | pendiente | qwindows.h |
| `WindowFromPoint` | Constantes Win16 | 2 | 1 | pendiente | wine-headers |
| `ABORTDOC` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `ASPECT_FILTERING` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `BITSPIXEL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `BLACKONWHITE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `CF_METAFILEPICT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `COLOR_CAPTIONTEXT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `COLOR_GRAYTEXT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `COLOR_WINDOWFRAME` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `COLOR_WINDOWTEXT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `CS_BYTEALIGNCLIENT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `CS_OWNDC` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `CS_VREDRAW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `CTLCOLOR_EDIT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `CreateCursor` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `CreateIcon` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `DEVICE_FONTTYPE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `DKGRAY_BRUSH` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `DLGC_HASSETSEL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `DLGC_WANTCHARS` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `DRAFT_QUALITY` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `DRAWPATTERNRECT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `DestroyIcon` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `EM_GETHANDLE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `EM_GETLINECOUNT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `EM_GETSEL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `EM_REPLACESEL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `EM_SETHANDLE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `EM_SETSEL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `ENDDOC` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `EN_ERRSPACE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `FF_SCRIPT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `FW_NORMAL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `GRAY_BRUSH` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `GW_HWNDPREV` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `GW_OWNER` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `GetKeyboardState` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `GetLastError` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `GetTempFileName` | Constantes Win16 | 1 | 1 | pendiente | debugwin.h |
| `GlobalDeleteAtom` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `GlobalMemoryStatusEx` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `HTBOTTOMLEFT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `HTSIZEFIRST` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `HTTOPRIGHT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `IDC_IBEAM` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `IDC_WAIT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `InvertRgn` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `LPLOGPEN` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `LPMETARECORD` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `LTGRAY_BRUSH` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `MF_BITMAP` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `MF_HILITE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `MF_UNCHECKED` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `MF_UNHILITE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `MK_LBUTTON` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `MM_ANISOTROPIC` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `MM_TEXT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `MSGF_MESSAGEBOX` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `NEXTBAND` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `NULLREGION` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_CLOSE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_DNARROWD` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_LFARROWD` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_OLD_CLOSE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_OLD_DNARROW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_OLD_LFARROW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_OLD_RGARROW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_OLD_UPARROW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_RGARROWD` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OBM_UPARROWD` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OF_CANCEL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `OF_PARSE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `PLANES` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `PostQuitMessage` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `QUERYESCSUPPORT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `R2_COPYPEN` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `R2_NOTXORPEN` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `RGN_AND` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `RGN_OR` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `RGN_XOR` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SBS_HORZ` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SBS_SIZEBOX` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SB_HORZ` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SB_THUMBTRACK` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SB_VERT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SC_ZOOM` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SETABORTPROC` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SHOW_OPENNOACTIVATE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SIZEICONIC` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CXBORDER` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CXCURSOR` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CXHTHUMB` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CXICON` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CXVSCROLL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CYBORDER` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CYCAPTION` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CYCURSOR` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CYFRAME` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CYFULLSCREEN` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CYHSCROLL` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_CYICON` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SM_MOUSEPRESENT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SP_APPABORT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SP_ERROR` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SP_NOTREPORTED` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SP_OUTOFDISK` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SP_OUTOFMEMORY` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SP_USERABORT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `STARTDOC` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SWP_HIDEWINDOW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SWP_SHOWWINDOW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SW_SHOWMAXIMIZED` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SW_SHOWMINIMIZED` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SW_SHOWMINNOACTIVE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `SetPixel` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `TF_FORCEDRIVE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VERTSIZE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_BACK` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_DELETE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_F16` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_F5` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_F7` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_F8` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_F9` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_INSERT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_NEXT` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `VK_SPACE` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `WHITEONBLACK` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `WS_CHILDWINDOW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `WS_DLGFRAME` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `WS_TILEDWINDOW` | Constantes Win16 | 1 | 1 | pendiente | qwindows.h |
| `WaitMessage` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `WinMain` | Constantes Win16 | 1 | 1 | pendiente | wine-headers |
| `string` | Constantes Win16 | 1 | 1 | resuelto (Winelib) | opus_x64_compat.h |

### Totales por categoría

| Categoría | Sitios pendientes | Sitios resueltos (Winelib) | TUs tocadas |
|---|---|---|---|
| Memoria Win16 | 201 | 0 | 21 |
| Espina de mensajes/ventanas | 287 | 0 | 47 |
| Mensajes WM_* | 373 | 0 | 33 |
| GDI texto/fuentes | 170 | 0 | 24 |
| GDI dibujo | 732 | 0 | 52 |
| Diálogos/menús | 78 | 0 | 11 |
| Portapapeles | 30 | 0 | 7 |
| Impresión | 19 | 0 | 6 |
| Entrada/cursor | 111 | 0 | 28 |
| Persistencia de configuración | 43 | 0 | 12 |
| Tipos HANDLE-like | 1445 | 0 | 120 |
| Tipos primitivos | 3199 | 0 | 174 |
| Geometría | 330 | 0 | 49 |
| Convenciones ABI | 0 | 2058 | 0 |
| Constantes Win16 | 5965 | 1 | 179 |
| **TOTAL** | **12983** | **2059** | |

## Vista 4 — Headers (dimensión de dependencia, no unidades portables)

131 headers escaneados (excluidos los 2 que son superficie de API): **82** con símbolos del diccionario, **49** sin ninguno.

Los headers no son unidades de portabilidad: un header con `BOOL` en una firma se corrige cuando se corrige el typedef, no archivo por archivo. Se listan los 15 con más sitios para dimensionar dónde se concentran las declaraciones de frontera.

| Header | Sitios | Categorías |
|---|---|---|
| `Opus/wordtech/word.h` | 325 | Tipos primitivos, Constantes Win16 |
| `Opus/core.h` | 234 | Tipos HANDLE-like, Tipos primitivos |
| `Opus/lib/sdm.h` | 169 | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/lib/sdmproc.h` | 156 | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `port/original/opus_x64_compat.h` | 112 | Espina de mensajes/ventanas, Mensajes WM_*, Diálogos/menús, Tipos HANDLE-like |
| `Opus/el.h` | 104 | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/lib/sbmgr.h` | 100 | Tipos HANDLE-like, Tipos primitivos, Constantes Win16 |
| `Opus/wordwin.h` | 97 | Mensajes WM_*, Tipos HANDLE-like, Tipos primitivos, Geometría |
| `Opus/resource/winrc.h` | 80 | Constantes Win16 |
| `Opus/lib/mathpack.h` | 68 | Tipos primitivos |
| `Opus/wwvk.h` | 61 | Constantes Win16 |
| `Opus/lib/lmem.h` | 60 | Tipos primitivos, Constantes Win16 |
| `Opus/insfield.h` | 46 | Constantes Win16 |
| `Opus/keys.h` | 45 | Constantes Win16 |
| `Opus/wordtech/field.h` | 41 | Tipos primitivos, Constantes Win16 |

## Vista 5 — Triage individual de `OpusEtAl/`

58 archivos, veredicto individual (decisión de proyecto: no excluir en bloque). Propuestas: **6 diferir**, **52 excluir**.

Ninguno aparece en `OPUS_ORIGINAL_ENGINE_SOURCES`: el motor no compila nada de `OpusEtAl/`. La propuesta es sugerencia para revisión humana, no decisión tomada.

La columna *v1* marca los archivos que el inventario anterior listaba como «núcleo puro confirmado» — el subconjunto que motivó esta decisión. El triage cubre los 58 `.c`/`.h` de `OpusEtAl/`, superconjunto de esos.

| Archivo | v1 | Target CMake | Acoplamiento | Propuesta | Motivo |
|---|---|---|---|---|---|
| `OpusEtAl/cashmere/fldexp/expmod.c` |  | — | portable | **diferir** | árbol cashmere, sin target en CMake; procedencia por confirmar |
| `OpusEtAl/cashmere/fldexp/fldexp.c` |  | — | portable | **diferir** | árbol cashmere, sin target en CMake; procedencia por confirmar |
| `OpusEtAl/cashmere/fldexp/fldexp.h` |  | — | portable | **diferir** | árbol cashmere, sin target en CMake; procedencia por confirmar |
| `OpusEtAl/cashmere/fldexp/myypars.c` | sí | — | portable | **diferir** | árbol cashmere, sin target en CMake; procedencia por confirmar |
| `OpusEtAl/tools/src/bcmmap.h` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/bitapp.c` |  | opus_bitapp_tool | portable | **excluir** | herramienta de build (opus_bitapp_tool): genera entradas del motor en tiempo de compilación, no código de ejecución |
| `OpusEtAl/tools/src/bitapp.h` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/buildelt.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/convtest/conv-tst.c` |  | — | presentación | **excluir** | banco de pruebas de conversión, sin target; no es núcleo |
| `OpusEtAl/tools/src/convtest/crmgr.h` |  | — | portable | **excluir** | banco de pruebas de conversión, sin target; no es núcleo |
| `OpusEtAl/tools/src/copyobj.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/coresize.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/dec2sym.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/dini.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/dini.h` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/dnatfile.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/dnatfile.h` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/draw/ddall.h` |  | — | presentación | **excluir** | aplicación DRAW independiente, no es Word |
| `OpusEtAl/tools/src/draw/dddlg.h` | sí | — | portable | **excluir** | aplicación DRAW independiente, no es Word |
| `OpusEtAl/tools/src/draw/ddedit.c` |  | — | presentación | **excluir** | aplicación DRAW independiente, no es Word |
| `OpusEtAl/tools/src/draw/ddesc.c` |  | — | presentación | **excluir** | aplicación DRAW independiente, no es Word |
| `OpusEtAl/tools/src/draw/ddprint.c` |  | — | presentación | **excluir** | aplicación DRAW independiente, no es Word |
| `OpusEtAl/tools/src/draw/mustang.c` |  | — | presentación | **excluir** | aplicación DRAW independiente, no es Word |
| `OpusEtAl/tools/src/draw/resource.c` | sí | — | portable | **excluir** | aplicación DRAW independiente, no es Word |
| `OpusEtAl/tools/src/draw/resource.h` | sí | — | portable | **excluir** | aplicación DRAW independiente, no es Word |
| `OpusEtAl/tools/src/dumprsh.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/echotmpl.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/eldes.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/exestub.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/makeerr.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/makekeys.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/makeopus.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/map2siz.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/mergeelx.c` | sí | opus_mergeelx_tool | portable | **excluir** | herramienta de build (opus_mergeelx_tool): genera entradas del motor en tiempo de compilación, no código de ejecución |
| `OpusEtAl/tools/src/mergeelx.h` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/mkassign.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/mkbcmmap.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/mkcmd.c` |  | opus_mkcmd_tool | portable | **excluir** | herramienta de build (opus_mkcmd_tool): genera entradas del motor en tiempo de compilación, no código de ejecución |
| `OpusEtAl/tools/src/mkdlg.c` | sí | opus_mkdlg_tool | portable | **excluir** | herramienta de build (opus_mkdlg_tool): genera entradas del motor en tiempo de compilación, no código de ejecución |
| `OpusEtAl/tools/src/newlen.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/newtoexe.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/onfilter.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/onresult.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/onwait.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/opl.h` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/opustlbx/opustlbx.c` |  | — | portable | **diferir** | toolbox de época, sin target; posible relación con port/original/toolbox.h |
| `OpusEtAl/tools/src/opustlbx/opustlbx.h` |  | — | portable | **diferir** | toolbox de época, sin target; posible relación con port/original/toolbox.h |
| `OpusEtAl/tools/src/revcnt.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/rtfgen.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/rtfline.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/slice.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/stringpp.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/strpmap.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/symstrip.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/vgrep.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/vk.h` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/waitfile.c` | sí | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |
| `OpusEtAl/tools/src/when.c` |  | — | portable | **excluir** | utilidad de build de época sin target en CMake; no enlaza en WORD1 ni aporta al núcleo Qt |

## Riesgos y restricciones

### Restricción de fidelidad byte a byte (decisión de proyecto)

La paginación del shell Qt debe ser **idéntica byte a byte** respecto del oráculo Winelib. Por tanto la interfaz de medición de texto de la frontera **debe reproducir el redondeo entero de GDI**; no se admiten métricas independientes de dispositivo de Qt sin una capa de compatibilidad explícita que reproduzca ese redondeo.

No es tarea de Qt-0, pero queda asentado aquí para que Qt-2 no lo pierda. Consecuencia concreta que este inventario sí puede aportar: los **170 sitios** de la categoría *GDI texto/fuentes* son la superficie exacta donde esa restricción se hace o se rompe. `GetTextExtent` es el símbolo a vigilar: su distribución por región (Vista 1 y 3) determina si la restricción se puede satisfacer en una sola interfaz o hay que replicarla en varias.

### Comentarios y literales: excluidos desde la revisión de Fase B

Durante el diseño de Qt-1 aparecieron dos falsos positivos del mismo tipo por caminos independientes: `TextOut` dentro de un comentario en `Opus/wordtech/format.c:2165`, y `GetTextExtent` dentro de una cadena literal en `Opus/dispspec.c:539`. Dos veces el mismo modo de falla es un problema sistemático, no dos accidentes, así que el escaneo dejó de contar comentarios y literales.

El script elimina `/* */`, `//`, `"…"` y `'…'` con una máquina de estados antes de tokenizar, preservando la longitud del texto para que los números de línea sigan siendo válidos. No se usó una expresión regular: es frágil justo en los casos que importan (una comilla dentro de un comentario, un `/*` dentro de una cadena).

Delta medido sobre las categorías cuyo conteo sostiene una decisión de diseño:

| Categoría | Antes | Después | Delta |
|---|---|---|---|
| GDI texto/fuentes | 200 | 170 | −30 |
| Convenciones ABI (resueltos) | 2407 | 2058 | −349 |
| Espina de mensajes/ventanas | 323 | 287 | −36 |
| Memoria Win16 | 206 | 201 | −5 |
| **Todas las categorías** | **16687** | **15042** | **−1645 (9,9 %)** |

El delta no es marginal y movió veredictos de TU: `wordtech/format.c` pasó de *presentación* a *portable* al desaparecer su único `TextOut` —estaba en un comentario—, con lo que `Opus/wordtech/` pasa de 21 a 22 TUs portables y la categoría *GDI texto/fuentes* de 27 a 24 TUs. En `Opus/wordtech/` queda una sola TU que toca medición de texto, `layoutap.c`. Los totales de este reporte ya son los posteriores a la limpieza.

### Limitaciones de método vigentes

- Escaneo textual con `\b`, no preprocesador: no evalúa `#if`, así que cuenta código desactivado por compilación condicional.
- No sigue cadenas de `#include`. Un archivo shim puede aparecer sin hits pese a estar acoplado a través de lo que incluye. En v1 esto se trató con una verificación de un salto; en v2 el problema se reduce porque la unidad es la TU del motor, no cualquier archivo del árbol.
- El árbol de ensamblador legado (`Opus/asm/`, 59 módulos, no compilados en CMake) queda fuera de alcance por decisión de proyecto, pese a que `Opus/asm/formatn2.asm` llama a `GetTextExtent`.
- Los conteos de sitio excluyen `Opus/lib/qwindows.h` y `Opus/debugwin.h`. Contarlos mediría la declaración de la API, no su uso.

## Apéndice A — Procedencia del universo `OpusEtAl/` (58 archivos)

**Mecanismo de extracción de v2:** recorrido recursivo de `src/OpusEtAl/` (`Path.rglob`), filtrado a sufijos `.c` y `.h`. Da 58 archivos.

**Relación con los 33 de v1.** Los 33 no eran una extracción de `OpusEtAl/`: eran el subconjunto de archivos de ese árbol que la lista «núcleo puro confirmado» de v1 contenía, es decir los que no tuvieron ningún hit con el diccionario curado de v1. El universo de v1 eran 378 archivos (`Opus/` + `OpusEtAl/`, `.c`/`.h`), que ya incluía los 58; los 25 restantes sí tenían hits y aparecían en su tabla de símbolos, no en la lista de núcleo puro. **No es una falla de glob**, y por tanto no es la misma clase de defecto que el blocker B1: B1 era un universo equivocado (directorios en vez de TUs del motor), esto es una lista derivada de un filtro por hits.

**Referencias en CMake.** El supuesto de que ninguno de los 25 adicionales figura en un target de `CMakeLists.txt` **es falso**. Resultado del chequeo, idéntico al aplicado a los 33:

| Grupo | Archivos | Referenciados en `CMakeLists.txt` | Cuáles |
|---|---|---|---|
| 33 de v1 | 33 | 3 | `tools/src/mkdlg.c`, `tools/src/mergeelx.c`, `tools/src/draw/resource.h` |
| 25 adicionales | 25 | 2 | `tools/src/bitapp.c`, `tools/src/mkcmd.c` |
| **Total** | **58** | **5** | |

Los 5 se referencian como herramientas de build del host (`opus_mkdlg_tool`, `opus_mergeelx_tool`, `opus_bitapp_tool`, `opus_mkcmd_tool`). **Ninguno de los 58 aparece en `OPUS_ORIGINAL_ENGINE_SOURCES`**, que es la afirmación que sostiene el triage: el motor no compila nada de `OpusEtAl/`.

## Apéndice B — Verificación de los símbolos en cero

De los 1618 símbolos del diccionario, 921 no tuvieron ningún sitio. Muestra aleatoria de 20 (`random.seed(20260810)`, `random.sample`), verificada con `grep -w` sobre las mismas 187 TUs, sin reutilizar el escaneo del script:

```
IE_DEFAULT  CE_BREAK  KNJ_MD_JIS  ANSI_VAR_FONT  META_PATBLT
CE_MODE  OCR_SIZEWE  PBITMAPCOREHEADER  LPLOGBRUSH  CP_GETMOUSE
ABSOLUTE  LPBITMAPINFOHEADER  CS_OEMCHARS  LB_REPLACESTRING  WC_MOVE
GMEM_NOCOMPACT  TC_SO_ABLE  CC_PIE  HS_VERTICAL  RT_MENU
```

**Resultado: 20 de 20 en cero.** Sin discrepancias; no hay bug de conteo. Todos pertenecen a familias del SDK Win16 que Word no usa (metaarchivos, kanji, capacidades de dispositivo, cursores OCR, cabeceras DIB, mensajes de listbox).

**Control de la verificación.** Una construcción errónea del `grep` también habría dado todo cero, así que se contrastó contra cuatro símbolos de conteo conocido:

| Símbolo | `grep -o` (ocurrencias) | `grep -c` (líneas) | Reporte |
|---|---|---|---|
| `GlobalUnlock` | 57 | 57 | 57 |
| `UpdateWindow` | 43 | 43 | 43 |
| `PatBlt` | 99 | 99 | 99 |
| `GetTextExtent` | 26 | 24 | 26 |

Las ocurrencias cuadran exactamente con el reporte en los cuatro casos. La diferencia de `GetTextExtent` es de unidad de conteo, no de método: `grep -c` cuenta líneas y dos líneas contienen el símbolo dos veces (`Opus/dispspec.c:539` y `:540`, donde aparece dentro de una cadena literal y además como llamada). Ese caso es una instancia concreta del ruido por cadenas y comentarios que la sección de limitaciones ya declara aceptado.

