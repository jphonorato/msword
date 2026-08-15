# Comportamiento de WORD1: lista de arranque bloqueado

Fecha original: 2026-08-15 · Rama `fix/winelib-startup-blocked`. Esta
serie parte de la tabla de §26 de
[`01-diagnostico-heap-corruption-arranque.md`](01-diagnostico-heap-corruption-arranque.md)
(los 7 fallos de comportamiento real que quedaron cuando el arnés dejó
de crashear). Cada sección es un ítem de esa lista.

Todas las afirmaciones están respaldadas por comandos ejecutados en
debian13 contra `/home/pablo/build-debian13-verify`, `DISPLAY=:59`.

---

## 1. `--about`: "Help About dialog did not appear"

**Test:** `opus_word1_about_test` · `stage=0 responsive=1` (y, si se
fuerza el cierre del MessageBox, `exit=0x0 mainWindow=0`).

**Estado:** causa raíz confirmada. El diálogo About **sí se materializa**
cuando `WM_COMMAND` 182 llega a `CmdAbout`. El test no lo ve porque ese
comando nunca se ejecuta: un MessageBox modal de arranque deja
`vcInMessageBox=1` y `AppWndProc` descarta todo `WM_COMMAND`. Fix de
producto diferido (3+ intentos de desbloqueo no dejaron el test en
Passed; ver más abajo).

### Qué significa `stage=0` / `responsive=1`

En `opus_word1_ui_test.cpp` (`about_mode`):

1. Espera la ventana principal `"Microsoft Word - Document1"`.
2. `Sleep(1000)`.
3. `PostMessageW(main, WM_COMMAND, kHelpAbout=182, 0)`.
4. Espera una top-level `OpusSdmDialog` 5 s.
5. Si no aparece, imprime
   `exit=… mainWindow=… responsive=… stage=…`.

- `responsive=1` es `window_is_responsive`: el proceso sigue vivo, la
  ventana principal responde a `WM_NULL`.
- `stage=` es `GetPropA(main, "OpusX64AboutStage")`. **Nadie en el
  árbol pone esa propiedad.** `stage=0` solo dice “nunca se instrumentó
  el comando”, no en qué línea falló SDM.

`182` es el `bcm` correcto (`opuscmd.h`: `bcmAbout = 182`).

### El diálogo no se crea (never-reached), no es invisible

Instrumentación temporal en `NatAppWndProc` y
`HdlgStartDlg`/`TmcDoDlgDli`/`IdDoMsgBox` (revertida; no quedó en el
árbol):

```
create_dialog_host hid=32773 / 32774   ← ribbon / ruler (hijos, no About)
IdDoMsgBox parent=(nil) flags=0x30
  caption='Microsoft Word'
  text='Low memory: cannot display requested font'
NatAppWndProc WM_COMMAND LOWORD=2799 (ViewPage)  vcInMessageBox=1
NatAppWndProc WM_COMMAND LOWORD=182  (HelpAbout) vcInMessageBox=1
  sy[182] mct=3 name=HelpAbout pfn≠0
— no hay TmcDoDlgDli hid=44 —
```

La tabla de comandos está bien (`mctSdm`, `CmdAbout` resuelto).
`AppWndProc` (`wproc.c:854`) hace `if (vcInMessageBox) return 0`. El
About **nunca llega** a `HdlgStartDlg` ni al bloque compartido
`create_dialog_host` líneas 338-397.

El MessageBox es real: `xwininfo` muestra
`"Microsoft Word" 295x82+364+356` junto a
`"Microsoft Word - Document1"`. En el screenshot de Xvfb se ve un
rectángulo negro (el `#32770` no pinta el texto en este Wine). El
origen del texto es `eidCantRealizeFont` (`error.c:933-934`), disparado
desde `ReportPendingAlerts` cuando `vmerr.mat == matFont`.
`LOADFONT.C` pone `matFont` si `CreateFontIndirect` / `OurSelectObject`
falla y cae a `SYSTEM_FONT`.

### El camino About funciona si el comando se ejecuta

Con WORD1 lanzado a mano en `:59`, `xdotool` Return sobre el
MessageBox y luego Alt+H, A:

```
IdDoMsgBox returned 1
NatAppWndProc WM_COMMAND LOWORD=182  vcInMessageBox=0
HcabAlloc cabi=1824 ok=1
TmcDoDlgDli enter hid=44 flags=0x41
create_dialog_host popup caption=About Microsoft Word
materialize_about_template hid=44
TmcDoDlgDli branch hid=44 modal=1 native_modal=1
```

`xwininfo`: `"About Microsoft Word" 410x230`. El host SDM, el
`materialize_about_template` y el loop modal de `TmcDoDlgDli` están
sanos. El fallo del test no está en las líneas 338-397.

### Intentos de desbloqueo (ninguno dejó Passed)

| # | Cambio | Resultado |
|---|---|---|
| 1 | `IdDoMsgBox` con owner `vhwndApp` | El `#32770` se puede encontrar; al cerrarlo WORD1 acaba en `exit=0` |
| 2 | El test hace `PostMessage(IDOK)` / `WM_CLOSE` / `BM_CLICK` al `#32770` y luego About | Tras dismiss `mainWindow=1`; el About deja `exit=0x0 mainWindow=0` |
| 3 | `IdDoMsgBox` devuelve `IDOK` al ver `"cannot display requested font"` (sin `MessageBoxA`) | WORD1 **sale solo a los ~3 s** (el loop modal del MB era el que mantenía vivo el arranque: bombea `ViewPage` y el resto del idle) |
| 4 | Test envía `VK_RETURN` + `TmcDoDlgDli` drena `WM_QUIT` | Sigue `exit=0x0 mainWindow=0` |

Cerrar el alerta desde otro proceso Wine, o saltárselo, no reproduce el
camino interactivo (Return en el X window, luego el menú). El MessageBox
de arranque es a la vez **el bloqueo de `WM_COMMAND`** y **parte del
bombeo de mensajes que termina el init**. Eso es pregunta de
arquitectura (¿arreglar `CreateFontIndirect` para que no haya
`matFont`? ¿un `IdDoMsgBox` que no sea modal y no corte el idle? ¿el
arnés usa `SendInput` en el hilo de WORD1?), no un parche de una línea
en `create_dialog_host`.

### Shared vs independent (Tasks 4–5)

**El mismo punto de fallo cubre `kIddNewDoc` y `kIddSaveAs`.** File New
(1813) y File Save As (1897) también van por `AppWndProc` → `FExecCmd`.
Con `vcInMessageBox=1` esos `WM_COMMAND` se tragan igual. El bloque
338-397 de `create_dialog_host` **no** es el fallo compartido: About,
New y Save As divergen *antes*, en el gate de `vcInMessageBox`.

Cuando el alerta deje de bloquear el pump, Tasks 4–5 deben
**verificar primero** el mismo camino (`TmcDoDlgDli` ya creó About con
`hid=44`). Si New/Save As siguen fallando *después* de un About verde,
entonces sí investigación propia (Save As además entra en
`run_word95_common_file_dialog`).

Pendiente de producto, fuera de este ítem: por qué
`CreateFontIndirect` falla (Arial 10 en la cinta se ve; el documento
cae a `SYSTEM_FONT`); el `#32770` y el About se pintan negros en este
Xvfb/Wine.

### Fix round (2026-08-15): sitio LOADFONT + `OpusShellCharWidths`

De los tres sitios que ponen `matFont` en `LOADFONT.C`, el que disparó
antes del MessageBox es el **3** (camino Linux, líneas 428-459):
`OpusShellCharWidths(...) != 0` → `LSystemFontErr`.

Instrumentación temporal de `OpusShellCharWidths` (luego retirada):

```
OpusShellCharWidths ftc=2 ps=0 catr=0 chFirst=0 cch=256 rc=-1 why=bad-px
```

No es `CreateFontIndirect` NULL (sitio 1) ni `OurSelectObject`/`FSelectFont`
(sitio 2): se llegó a pedir anchos. La petición de arranque es Helv
(`ftc=2`) con `hps==0`. `RawFontFor` hacía `px = MulDiv(ps/2, 96, 72)`
y rechazaba `px<=0`. Ese `-1` es el que pone `matFont` y deja
`vcInMessageBox=1`.

Cambio en `src/core/src/OpusShellFontMetrics.cpp` (sin tocar `src/Opus/`):

- `hps==0` se mide como 10 pt (`hpsDefault`), igual que
  `CreateFontIndirect(lfHeight==0)` usa altura por defecto.
- WORD1 no tiene `QGuiApplication`. Crear una en el hilo Wine cuelga el
  pump o mata el proceso. Sin app, `QRawFont` no se construye: se
  rellenan los anchos desde la tabla oráculo ya medida
  (`opus_shell_font_metrics_oracle_table.h`, Helv 10 pt).

Tras ese éxito, el MessageBox de fuente **no aparece**, pero WORD1
sigue sin dejar About verde: el init continúa y cae en
`FInsertInPl` (`clsplc.c:829`) desde `C_PushLbs` (`layout2.c:1430`),
write AV `0xC0000005` vía `HpInPl` (`opus_asm_resn2_pl.cpp`).
`ctest -R opus_word1_about_test` queda:

```
Help About process exit=0x0 mainWindow=0 responsive=0 stage=0
Help About dialog did not appear
```

(debian13, `/home/pablo/build-debian13-verify`, `DISPLAY=:59`)

Una vez con `QGuiApplication` en el hilo Wine el diálogo About
*sí* se creó (`OpusSdmDialog`) y el fallo pasó a “did not finish
initializing” (el host no respondía `WM_NULL`). Ese camino no se dejó:
Qt en el hilo Wine no es viable. El crash de `FInsertInPl` es el
siguiente bloqueo; no es un quinto skip del MessageBox.

### Estado al cortar (2026-08-15, HEAD `134cddc`)

`opus_word1_about_test` **sigue en Failed** (~7.8 s,
`exit=0x0 mainWindow=0`). El MessageBox de fuente ya no aparece. El
proceso muere en layout:

```
Exception 0xC0000005 write at 0xFFFFFFFD3726D202
OpusMoveBytesEnd+0x2B
FInsertInPl+0x1B6
C_PushLbs+0x296
```

(`build/WORD1-crash.txt`; call site C `layout2.c:1430`
`FInsertInPl(vhpllbs, ilbs, plbsTo)`.)

Instrumentación temporal de `HpInPl`/`OpusPlData` (revertida, no quedó
en el árbol) dejó `build/t3-h2-pl.log`. Las primeras llamadas son
PLs sanos:

```
iMac=1 iMax=1 cb=2  brgfoo=20  fExternal=0
iMac=1 iMax=1 cb=136 brgfoo=256 fExternal=0   ← dest in-heap, insane=0
```

La última, ya con el header destrozado, es otro `hpl`:

```
HpInPl hpl=0x7ffffe811970 *hpl=0x7ffffe811bc0
  iMac=-1072622911 iMax=32726 cb=6 brgfoo=0 fExternal=-31497312
  i=-1072622912 base=(nil) dest=0xfffffffe80667080 insane=1
```

`HpInPl` no inventa ese puntero: le pasan un bloque que ya no es un
`struct PL`. `iMac==iMax==1` en las llamadas sanas implica que el
siguiente `FInsertInPl` entra en el grow (`clsplc.c:847-874`).
Siguiente Phase 1 (no hecha): discriminar *grow que corrompe el
header* (`FChngSizeHCw` / tamaño `brgfoo + cb*iMax`) vs *handle
equivocado* (no es `vhpllbs`). No editar `src/Opus/`.

Un intento a medias de anchos GDI (`opus_gdi_char_widths.cpp` +
`OpusShellSetCharWidthsFallback`) se descartó al cortar: no llegó a
ctest verde. No reintroducirlo hasta tener el log de `HpInPl` sobre
el grow.

**Shared vs independent (Tasks 4–5), actualizado:** About / New /
Save As ya no están tapados por `vcInMessageBox`. Siguen
bloqueados porque el proceso no sobrevive al primer layout. Verificar
primero cuando About esté verde.

### Fix round 2 (2026-08-15): H1 vs H2 del AV en `FInsertInPl`

Phase 1, un cambio cada vez. El MessageBox de fuente sigue cerrado.

**H1 (métricas):** `OpusShellCharWidths` devolvió 0 (sin `matFont`) y
rellenó la tabla con un dummy constante 8. El AV **no desapareció**.
Luego se midió con GDI (`CreateCompatibleDC` + `CreateFontIndirectA`
mismo `LOGFONT` que `C_FGraphicsFcidToPlf`, `hps==0` → `lfHeight==0`,
`GetCharWidthA`; confirmado `GDI ftc=2 ps=0 w32=3 wA=7`). El AV
**sigue** en `FInsertInPl`. Los valores de la tabla no son la causa:
oráculo Helv-10, dummy 8 y GDI Helv `lfHeight=0` mueren igual.

El camino interactivo que sí abre About usa `fFallback` /
`fFixedPitch=true` (`LOADFONT.C:397` no llena `hqrgdxp`). Eso no se
puede forzar desde el puerto sin devolver -1 (`matFont` / MessageBox).

**H2 (header PL / HpInPl):** confirmado. El `hpl` del crash **es**
`vhpllbs` (`lbs=1`, `hsz=1428` = `cbPLBase + 8*sizeof(LBS)` con
`sizeof(LBS)==176`). En la primera `FInsertInPl` el header ya es
basura:

```
iMac=-1072622911 iMax=32726 cb=6 brgfoo=0 fExternal=-31497312
i=-1072622912 base=(nil) dest=0xfffffffe80667080
```

`PL`: `cbPLBase=20`, `fExternal` @ 16. `PLLBS` no tiene `fExternal`
(`rglbs` @ 16). `OpusPlData` trataba cualquier `fExternal!=0` como HQ
→ dest salvaje. Las primeras `HpInPl` son **otros** PLs sanos
(`cb=2`/`cb=136`); `vhpllbs` no se ve sano ni una vez.

El smash **no** es un `bltbh` sobre los 20 bytes del header (un
`memmove` vigilado no disparó). Tampoco un `FChngSizeHCb` de
`vhpllbs`: al `HAllocateCw(1428)` `vhpllbs` aún es nil; no hay
`chng` posterior sobre ese handle antes del AV.

Clamp de `dest` en `HpInPl` evita el write salvaje y mueve el crash a
`UnstackLbs` (camina `ilbsMac` basura) o `IpgdPldrFromWwDocCpIpgd`.
No repara el header.

**Ambas:** H1 no es “tabla Helv-10 ≠ GDI”. H2 es el mecanismo del AV
(`HpInPl` sobre `vhpllbs` ya destrozado). El header se corrompe en
el camino variable-pitch (`fFixedPitch=false`) antes de
`C_PushLbs`; el puerto no ve el store. Sin editar Opus no hay sitio
para restaurar `iMac` antes de `layout2.c:1384`.

**ctest** (debian13, `DISPLAY=:59`, GDI + `fExternal==1` en
`OpusPlData`):

```
opus_word1_about_test ***Failed  7.8 sec
Help About process exit=0x0 mainWindow=0 responsive=0 stage=0
Help About dialog did not appear
CTEST_EXIT=8
Exception 0xC0000005 FInsertInPl+0x1B6 C_PushLbs+0x296
```

**BLOCKED** en líneas Opus (no tocadas):
`src/Opus/wordtech/layout2.c:1384` (`ilbs = (*vhpllbs)->ilbsMac`) y
`1430` (`FInsertInPl`); o `LOADFONT.C:397` (único camino que no
entra en layout variable-pitch).

---

## Cómo retomar

Rama `fix/winelib-startup-blocked` (no está en `main`). Plan:
`docs/superpowers/plans/2026-08-15-terminar-winelib.md`. Ledger SDD:
`.superpowers/sdd/2026-08-15-terminar-winelib/progress.md`.

Build/test solo en debian13 contra `/home/pablo/build-debian13-verify`,
`DISPLAY=:59`. No usar el `--preset` del host.

Retomar en Task 3 **después** de Fix round 2 (HEAD incluye `5bebdd9`):
H1 descartada; el header de `vhpllbs` ya es basura en la primera
`FInsertInPl`. Buscar **quién escribe ese bloque entre `HplInit` y
`C_PushLbs`** (no es grow, no es `bltbh` del header). Sin tocar
`src/Opus/` salvo autorización. Tasks 1–2 hechas. Tasks 4–10 no
empezadas.
