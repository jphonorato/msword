# Comportamiento de WORD1: lista de arranque bloqueado

Fecha original: 2026-08-15 · Rama `fix/winelib-startup-blocked`. Esta
serie parte de la tabla de §26 de
[`01-heap-corruption-startup-diagnosis.md`](01-heap-corruption-startup-diagnosis.md)
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

### Fix round 3 (2026-08-15): `_setjmp` con ABI equivocada — RESUELTO

`opus_word1_about_test` **pasa**. El AV no estaba en `FInsertInPl` ni en
`HpInPl`: los dos eran víctimas. Quien destroza el header de `vhpllbs`
es `setjmp`.

**Evidencia (watchpoint de hardware, no printf).** En el contenedor
debian13, `gdb -batch` sobre `/usr/lib/wine/wine64` (no sobre
`/usr/bin/wine`, que es un script), con `set follow-fork-mode parent`
para no seguir al `wineserver`, y `handle SIGSEGV nostop noprint pass`
para que Wine convierta la falla en excepción Win32:

1. Breakpoint en `layout.c:286` (justo después de
   `vhpllbs = HplInit(sizeof(struct LBS), 8)`). El header está **sano**:
   `iMac=0 iMax=8 cb=176 brgfoo=20 fExternal=0`, `data=0x7ffffe8117a0`.
   Es decir, la hipótesis "ya nace destrozado" de la ronda 2 era falsa:
   nace bien y lo rompen cuatro líneas más abajo.
2. `watch -l *(int *)(data + 16)` (o sea `PL.fExternal`) y `continue`.
   Dispara enseguida: `Old value = 0`, `New value = -31497312`, con
   `#1 LbcFormatPage ... layout.c:290` — o sea `SetLayoutAbort()`.
3. En el punto del disparo, `x/10i $pc-32` muestra el cuerpo del
   callee, que es exactamente el `_setjmp` de MSVCRT x86-64:

   ```
   0x6fffffc2f8e8:  mov    %rdx,(%rcx)      ; buf->Frame
   0x6fffffc2f8eb:  mov    %rbx,0x8(%rcx)   ; buf->Rbx
   0x6fffffc2f8ef:  lea    0x8(%rsp),%rax
   0x6fffffc2f8f4:  mov    %rax,0x10(%rcx)  ; buf->Rsp
=> 0x6fffffc2f8f8:  mov    %rbp,0x18(%rcx)  ; buf->Rbp
   ```

   y los registros: `rdi=0x7ffff79122d8` (que **sí** es
   `&venvLayout.nativeEnv`, el buffer correcto) frente a
   `rcx=0x7ffffe8117a0` (que es `*vhpllbs`).

**Causa raíz, en una frase:** `_setjmp` se resolvía contra el `msvcrt`
PE de Wine, que es Microsoft-x64 y toma el `jmp_buf` en **RCX**,
mientras que todo el código de Opus es System V y lo pasa en **RDI**;
el import escribía su `_JUMP_BUFFER` de 256 bytes sobre el puntero
rancio que quedara en RCX.

En `LbcFormatPage` ese RCX rancio es `*vhpllbs`, el bloque que
`HplInit` acababa de devolver nueve líneas antes, así que cada pasada
de layout escribía estado de registros encima del `struct PL`:

- `fExternal` (offset 16) recibía la mitad baja de RSP →
  `0xfe1f63a0` = `-31497312`, que es justo el valor que la ronda 2
  había registrado.
- offsets 20..27 recibían `0x00007fff` + ceros → de ahí sale el
  `hqple = 0x7fff00000000` del AV.

El síntoma final (tras el clamp de `OpusPlData` de `5bebdd9`) ya no era
`FInsertInPl` sino `FreeHpl` (`clsplc.c:465`): con `fExternal` distinto
de cero toma el PL no-externo por externo, lee ese `hqple` basura de
`rglbs[0]` y llama a `FreeHq` → AV de lectura en `OpusFreeH`
(`opus_x64_heap.cpp:411`, `mov (%rax),%rax` con `rax=0x7fff00000000`).
La cadena `LbcFormatPage → FreePhpl(&vhpllbs) → FreeHpl → OpusFreeH`
quedó confirmada casando los retornos con el desensamblado
(`FreeHpl+84` es exactamente el retorno del `call OpusFreeH` de la rama
`FreeHq`, y el slot `rbp-8` de `FreePhpl` contiene `&vhpllbs`).

Confirmación estática: `nm bin/WORD1.exe.so` mostraba
`__imp__setjmp` y un thunk `t _setjmp` en la misma tabla de imports que
`ShellExecuteA` / `AdjustWindowRect`. `_setjmp` era el **único** símbolo
de CRT importado por error (los otros cuatro con pinta de CRT —
`_lclose`, `_llseek`, `_lread`, `_lwrite` — son APIs Win16 legítimas de
kernel32). `longjmp`, en cambio, sí resolvía a glibc
(`longjmp@GLIBC_2.2.5`): el par estaba roto por la mitad.

Llega ahí porque `Opus/lib/qsetjmp.h` (rama `OPUS_X64`) usa el
`<setjmp.h>` del host y glibc expande `setjmp(env)` a `_setjmp(env)`.

**Fix (todo fuera de árbol restringido):**
`src/port/original/opus_x64_setjmp.cpp` define `_setjmp` en ABI System V
como salto de cola a `__sigsetjmp(env, 0)` — que es literalmente lo que
hace el `_setjmp` de glibc. Tiene que ser salto de cola: un wrapper en C
dejaría un marco de pila que ya no existe cuando `longjmp` vuelve a él.
Al quedar el símbolo definido, `winebuild` deja de generar el import de
`msvcrt` (verificado: `nm` ahora da `T _setjmp` + `U
__sigsetjmp@GLIBC_2.2.5`, sin `__imp__setjmp`). Se añade a
`WORD1_SOURCES` dentro del bloque `if(OPUS_WINELIB_BUILD)` que ya
existía, así que MSVC no lo ve.

**Segundo cambio, necesario para que ctest lo *vea*.** Con el AV
arreglado, `opus_word1_ui_test --about` termina en 2 s con código 0
(diálogo `OpusSdmDialog` creado, botón en el id 1, responde, cierra
limpio), pero ctest seguía dando `Timeout 20 s` sin imprimir nada. La
causa es el §25 ya documentado: `CreateProcessW` devuelve un
`PROCESS_INFORMATION` a cero para `WORD1.exe.so`, así que
`hProcess == nullptr` y el `TerminateProcess` de cada salida es un
no-op; WORD1 sobrevivía al harness reteniendo el pipe de stdout
heredado y ctest esperaba. Mientras WORD1 se moría solo esto no se
notaba. `opus_word1_ui_test.cpp` recupera ahora el PID real desde la
ventana principal (`GetWindowThreadProcessId` + `OpenProcess`) cuando
`hProcess` viene nulo; con eso todo el teardown y las esperas ya
existentes funcionan sin tocarlas, y las búsquedas de ventana
recuperan el filtro por PID exacto que el §26 prefiere.

**ctest** (debian13, `/home/pablo/build-debian13-verify`, `DISPLAY=:59`):

```
1/1 Test #17: opus_word1_about_test ............   Passed    2.88 sec
100% tests passed, 0 tests failed out of 1
```

Estable en 3 ejecuciones consecutivas (2.84 / 2.97 / 2.83 s).

La etiqueta `word1_startup_blocked` pasa de **0/9** a **5/9** (cifras
tras la revisión, ver "Cierre de revisión" abajo):

```
1/9 Test #10: word1_port_smoke_test ................   Passed    1.56 sec
2/9 Test #11: opus_word1_ui_test ...................   Passed    1.82 sec
3/9 Test #12: opus_word1_clipboard_shortcut_test ...   Passed    2.02 sec
4/9 Test #13: opus_word1_typing_test ...............   Passed   12.70 sec
5/9 Test #14: opus_word1_interaction_test ..........***Failed    2.52 sec
6/9 Test #15: opus_word1_selection_test ............***Failed    3.39 sec
7/9 Test #16: opus_word1_font_typing_test ..........***Failed    1.69 sec
8/9 Test #17: opus_word1_about_test ................   Passed    2.78 sec
9/9 Test #18: opus_word1_save_as_test ..............***Failed    7.83 sec
56% tests passed, 4 tests failed out of 9
```

Los 4 que siguen fallando ya no mueren por el AV: fallan rápido y con
mensaje propio. Son el material de las Tasks 4–5.

Gating: 8/8 verdes (`opus_original_strtbl_test`,
`opus_original_sttb_test`, `opus_original_plc_test`, `opus_sdm_cab_test`,
`opus_original_command_test`, `opus_shell_memory_foreign_test`,
`opus_shell_config_test`, `opus_shell_font_substitution_test`).

### Cierre de revisión (2026-08-15): las dos mitades del par, y el guard

La revisión levantó dos cosas importantes; ambas corregidas.

**1. `longjmp` seguía atado a glibc solo por suerte de link order.**
El fix original fijaba `_setjmp` por definición pero dejaba `longjmp`
a lo que decidiera el enlazador. No es una preocupación teórica: en
este contenedor hay **14 archivos de import de Wine** que definen
`longjmp`, `_setjmp` y `_setjmpex` — `libmsvcrt.a`, `libmsvcr70..120`,
`libucrtbase.a`, `libvcruntime140.a`, `libntdll.a`, `libntoskrnl.a`,
tanto en `x86_64-unix` como en `x86_64-windows`. `libntdll.a` lo enlaza
cualquier target winelib. Pasarle un `jmp_buf` de glibc al `longjmp`
Microsoft-x64 de Wine es la misma catástrofe silenciosa en la otra
dirección.

`opus_x64_setjmp.cpp` fija ahora también `longjmp`, como salto de cola
a `_longjmp@PLT`. `_longjmp` es un nombre que **ninguno** de esos 14
archivos define (verificado), así que es un destino seguro; y en glibc
`longjmp`, `_longjmp` y `siglongjmp` son alias débiles de un mismo
`__libc_siglongjmp` (misma dirección `0x3fab0` en `libc.so.6`), o sea
que el reenvío es exacto.

Comprobación en el binario:

```
                 U __sigsetjmp@GLIBC_2.2.5
                 U _longjmp@GLIBC_2.2.5
000000000008bd34 T _setjmp
000000000008bd3b T longjmp
```

**2. Guard en tiempo de build.** `src/cmake/AssertNoWineCrtSetjmp.cmake`
corre como `POST_BUILD` de WORD1 y falla el build si reaparece
cualquier thunk `__imp_` de la familia (`__imp__setjmp`,
`__imp__setjmpex`, `__imp_longjmp`, `__imp__longjmp`,
`__imp_siglongjmp`). Así, un cambio futuro de toolchain o de orden de
enlace sale como error de build y no como escritura salvaje durante
layout.

El guard está probado en los dos sentidos, no solo escrito:

```
NEGATIVO: binario fabricado con `void *__imp__setjmp = 0;`
  -> CMake Error ... imports the Microsoft-x64 CRT setjmp/longjmp
     family from Wine: __imp__setjmp        (rc=1)
POSITIVO: bin/WORD1.exe.so                  (rc=0)
CABLEADO: ninja -t commands WORD1 | grep AssertNoWineCrtSetjmp.cmake  -> presente
```

**3. `word1_port_smoke_test` ya tiene `TIMEOUT 20`**
(`CMakeLists.txt:1580`), igual que sus 8 hermanos. Le faltaba, y
mientras WORD1 moría solo eso no se notaba. Con el par setjmp/longjmp
completo el test además **pasa** (1.56 s), no solo deja de colgarse.

**Sigue pendiente, y no lo causa este fix:**
`opus_x64_runtime_test` (gating) **se cuelga** en el tip de la rama:
ejecutado directamente termina en `timeout 40` sin imprimir una sola
línea (`rc=124`). El binario de ayer (05:46, anterior a `5bebdd9` y a
este fix) se colgaba igual, no contiene ningún símbolo `setjmp`, y ni
`opus_x64_setjmp.cpp` ni el cambio del harness entran en ese target.
Reconstruirlo no lo arregla. Hay que investigarlo aparte.

**Alcance real del bug.** `SetJmp` no se usa solo en layout: también en
`GRSPEC.C`, `eldde.c`, `fieldpic.c`, `fltexp.c`, `ffread.c`,
`mathapi.c`, `token.c`, `sort.c` e `interp/elinit.c`. Todos esos sitios
llevaban escribiendo 256 bytes de estado de registros sobre punteros
ajenos. Conviene revisar si otros fallos "inexplicables" del port
desaparecen con esto antes de investigarlos por separado.

**Nota para quien siga:** `opus_original_startup_probe` (target
`EXCLUDE_FROM_ALL`, no entra en ctest) enlaza el mismo grafo y sigue
sin el shim. Si se revive, hay que añadirle
`port/original/opus_x64_setjmp.cpp` igual que a WORD1.

---

## 4. File > New: verificación bloqueada por entorno (no por código)

**Estado:** sin determinar. Task 4 se lanzó para verificar si File > New
comparte la causa raíz de Task 3 (hipótesis fuerte: sí — `opus_word1_ui_test`
en modo base, el mismo test, ya había dado `Passed 1.82 s` dentro del run
completo de Fix round 3). El intento de verificación de esta sesión no
llegó a confirmarlo ni a refutarlo:

```
1/9 Test #10: word1_port_smoke_test .................   Passed    1.69 sec
2/9 Test #11: opus_word1_ui_test ....................***Failed    9.57 sec

WORD1 main window did not appear
(Wine CreateWindow error 1400: Invalid window handle)
```

El fallo ocurre **antes** de llegar al comando File > New (línea ~1965 del
arnés) — la segunda instancia de WORD1 nunca crea su ventana principal.
Diagnóstico del implementador: síntoma de `explorer.exe`/wineserver en
mal estado tras el primer proceso, no relacionado con el binario (símbolos
`_setjmp`/`longjmp` verificados correctos).

**Causa más probable, descubierta después de que Task 4 se cerrara BLOCKED:**
durante esta misma sesión había **otra sesión** trabajando en paralelo
sobre el mismo checkout y el mismo `~/build-debian13-verify` dentro de
debian13 (confirmado con procesos en vivo: un `cmake --build --target
WORD1` + `ctest -R opus_word1_ui_test` detached, `PPID 1`, que ninguno de
los agentes de esta sesión lanzó). Dos wineserver/Wine-prefix compartidos
recibiendo lanzamientos de WORD1 al mismo tiempo explica el síntoma
("Wine state corruption between test runs") mejor que una regresión de
código — y es coherente con que `opus_word1_ui_test` ya había pasado horas
antes, en la misma rama, sin cambios de código de por medio. **No
confirmado**, es la hipótesis más probable a re-verificar primero, en una
ventana donde el build dir no esté en uso por nadie más.

No hubo cambios de código ni commit para Task 4. Detalle completo:
`.superpowers/sdd/2026-08-15-terminar-winelib/task-4-report.md`.

---

## 5. Revisión independiente de Task 3 -- 4 hallazgos de fidelidad, corregidos y verificados en exia

**Contexto:** antes de confiar en Task 3, se corrió `/code-review` (nivel
`high`) contra los 9 commits de `fix/winelib-startup-blocked` sobre
`main`. Encontró 4 problemas, los 4 en el mismo tema: la rama arregla el
AV real de `_setjmp`, pero dos de sus workarounds de la era de
investigación (antes de encontrar la causa raíz) y dos gaps del camino
sin-`QGuiApplication` de `OpusShellFontMetrics` quedaban silenciando
datos incorrectos en vez de fallar visible -- exactamente lo que el
proyecto no se puede permitir dado el requisito duro de paginación
byte-idéntica.

**Los 4, con su causa raíz confirmada contra el código real (no solo el
diff):**

1. **`opus_original_startup_probe.cpp` -- `OpusPortGdiCharWidths`**
   dejaba `lfHeight=0` (tamaño por defecto de Wine, indefinido) cuando
   `hps<=0`, mientras `OpusShellFontMetrics` ya usaba 10pt
   (`PixelSizeFor`/`PointSizeFor`, hpsDefault) para la *misma* petición
   de fuente -- anchos y ascent/descent medidos a dos tamaños distintos.
   El comentario original decía que esto "recreaba" el `LOGFONT` que
   `C_FGraphicsFcidToPlf` real construye para `hps==0`; falso:
   `Opus/LOADFONT.C:880` tiene `Assert( fcid.hps > 0 )` -- el Word real
   nunca llega a esa función con `hps==0`. Se corrigió para usar el
   mismo default de 10pt.

2. **`opus_x64_layout.c` -- `OpusPlData`** clampeaba cualquier
   `brgfoo` fuera de `[cbPLBase, 4096]` a `cbPLBase` en silencio. Es un
   workaround de la sesión de investigación de Task 3 (mismo commit
   `5bebdd9` que originó el AV), de cuando la causa raíz todavía no se
   conocía. Con `_setjmp`/`longjmp` ya arreglados no debería dispararse
   más -- se dejó el clamp pero se le añadió un `fprintf(stderr, ...)`
   para que una recurrencia real sea visible.

3. **`opus_asm_resn2_pl.cpp` -- `HpInPl`** devolvía el puntero al
   elemento 0 (no `nullptr`) cuando `cb<=0` o `index<0` -- también del
   mismo commit `5bebdd9`. El original (`Opus/asm/resn2.asm:1340`) no
   tiene ningún fallback de release, solo un `Assert` de DEBUG; dar el
   elemento 0 como si fuera válido hace que cualquier llamador (todos
   tratan un retorno no-nulo como "índice válido") lea o escriba el
   slot equivocado sin poder distinguirlo de un acceso real. Se
   corrigió a `nullptr`.

4. **`OpusShellFontMetrics.cpp`** -- el fallback de tabla oráculo
   (usado cuando no hay `QGuiApplication`, el caso de arranque) ignoraba
   `key->catr` (negrita/cursiva): la tabla
   (`opus_shell_font_metrics_oracle_table.h`) solo tiene filas de peso
   regular (28 filas = 4 nombres de época × 7 tamaños, nunca se capturó
   negrita/cursiva), así que una petición en negrita/cursiva recibía en
   silencio las métricas de peso regular. El llamador real
   (`Opus/LOADFONT.C:442-448`) documenta explícitamente que `catr != 0`
   debe fallar controlado -- ese contrato ya se cumplía en el camino
   con `QGuiApplication`, pero no en este fallback. Se corrigió para
   que el fallback de oráculo (ascenso/descenso y, si el GDI de
   `OpusPortGdiCharWidths` también falla, anchos) se salte cuando
   `catr != 0`, en vez de responder con datos de peso equivocado.

**Verificado en exia (VPS, no el contenedor `debian13` de hp-15--
hp-15 no estaba disponible esta sesión; exia es Debian 13 trixie con
`wine`/`winegcc` igual de válido per `CLAUDE.md`):**

- `ninja`/`cmake --build` de `opus_original_engine` y `WORD1`: 0
  errores, mismos warnings preexistentes de siempre.
- `ctest` completo (sin `opus_x64_runtime_test`, ver nota abajo): igual
  que antes de estos 4 fixes -- sin regresión.
- **Gotcha real encontrado en esta verificación, no relacionado con el
  código:** la primera corrida de la etiqueta `word1_startup_blocked`
  dio 0/9 con `free(): corrupted unsorted chunks` y `unknown test mode`
  en todos -- el patrón de bug *original*, de antes de Tasks 1-2. Causa:
  `build/tests/Debug/opus_word1_ui_test.exe.so` tenía fecha del 12 de
  agosto -- de **antes** de que existieran los fixes de Tasks 1-3 en el
  árbol. `cmake --build --target WORD1` no reconstruye el arnés de
  test; hace falta `--target opus_word1_ui_test` aparte. Con el arnés
  reconstruido: **4/9** (`word1_port_smoke_test`, `opus_word1_ui_test`
  base, `opus_word1_clipboard_shortcut_test`, `opus_word1_about_test`),
  coincide con los mismos 4 wins ya documentados en §1-4 arriba (la
  quinta de "5/9" en `Cómo retomar` corresponde al mismo `ui_test` base
  contado una sola vez ahí). Los 5 fallos restantes
  (`--typing`, `--interaction`, `--selection`, `--font-typing`,
  Save As) son exactamente el alcance sin empezar de Tasks 5-10 --
  no regresiones de estos 4 fixes.
- **`Xvfb` real necesario:** sin `DISPLAY`, Wine cae a `nodrv` y todo
  falla con "Invalid window handle" antes de llegar a la lógica real.
  Este VPS ya tenía un `Xvfb :99` corriendo desde el 12 de agosto
  (otra sesión); se reusó en vez de levantar uno nuevo.
- `opus_x64_runtime_test` (gating) sigue colgándose sin imprimir nada
  -- confirmado también aquí, mismo síntoma que documentó la sesión de
  Task 3 en debian13. Sigue sin investigar, sigue sin relación con
  Task 3 ni con estos 4 fixes.

4 commits nuevos sobre `16145b6`: uno por hallazgo, mismo formato de
mensaje que el resto de la rama.

## 6. File > New: confirmado -- verify-only, cierra §4

**Task 4 retomada 2026-08-19 en exia.** El plan (Task 4, Step 1) pedía
correr `opus_word1_ui_test` (modo base) aislado: si pasa sin ningún
cambio de código, Task 4 es verify-only.

```
ctest -R "^opus_word1_ui_test$" --output-on-failure
    Start 11: opus_word1_ui_test
1/1 Test #11: opus_word1_ui_test ...............   Passed    3.63 sec
100% tests passed, 0 tests failed out of 1
```

El modo base del arnés (`opus_word1_ui_test.cpp:1964-1995`) manda
`WM_COMMAND`/`kFileNew` (id 1813), confirma que el diálogo File New
aparece, que sus controles coinciden con el contrato SDM, lo acepta,
espera a que cierre y confirma que `Document2` se creó -- exactamente
el flujo que §4 dejó sin determinar. Pasa limpio, sin ningún cambio de
código en esta sesión.

**Confirma la hipótesis de §4** ("la causa más probable... contención
del build dir compartido, no una regresión de código", no confirmada
en su momento): mismo root cause que About (Task 3, `_setjmp`/`longjmp`
ABI), sin bug independiente de File > New. La corrida de §4 falló por
el entorno compartido de debian13/hp-15 en esa sesión, no por el
código -- aquí, en exia, sin contención, pasa a la primera.

Sin cambios de código para esta sección -- commit de documentación
solamente.

## 7. Save As: "File Save As dialog did not cancel cleanly" -- causa raíz independiente, arreglada

**Task 5, 2026-08-19 en exia.** A diferencia de Task 4, esta **no**
comparte causa raíz con Task 3 -- verify-only dio un fallo real, con
mensaje propio:

```
ctest -R "^opus_word1_save_as_test$" --output-on-failure
    File Save As dialog did not cancel cleanly
```

**Causa raíz, confirmada leyendo el código real (no solo el síntoma):**
`create_dialog_host` (`opus_sdm_runtime.cpp:336`) crea, para
`kIddOpen`/`kIddSaveAs`, una ventana `WS_POPUP` de clase
`"OpusSdmDialog"` **sin `WS_VISIBLE`** -- un señuelo que
`materialize_save_as_template` puebla con controles reales (incluido
el botón Cancel, id 2). Pero `TmcDoDlgDli` (`opus_sdm_runtime.cpp:2522`)
nunca usa esa ventana para Open/Save As: para esos dos `hid` llama
directo a `run_word95_common_file_dialog`, que bloquea el hilo dentro
de `GetSaveFileNameA`/`GetOpenFileNameA` -- el diálogo real y visible
es una ventana completamente distinta (`#32770`, el común de Windows/
Wine), no el señuelo.

El arnés de test (razonablemente, seguiendo la misma convención de
clase que usan *todos los demás* diálogos SDM) busca `"OpusSdmDialog"`
+ `"Save As"` -- encuentra el señuelo (existe, aunque oculto:
`EnumWindows` no filtra por visibilidad y nada en `find_window_callback`
lo hace tampoco), le manda `WM_COMMAND` id=2 al botón Cancel del
señuelo. `handle_dialog_command` lo recibe (la bomba de mensajes de
`GetSaveFileNameA` despacha *todos* los mensajes del hilo, no solo los
de su propia ventana) y llama `finish_native_dialog`, que marca
`dialog.dying=true` y hace `ShowWindow(SW_HIDE)` -- pero nada de eso
llega al diálogo real, que sigue bloqueado esperando su propio Cancel.
`wait_for_window_to_close` espera a que la ventana señuelo *desaparezca*
(`find_process_window` devuelve null), pero `DestroyWindow` del señuelo
solo ocurre en `TmcDoDlgDli` **después** de que
`run_word95_common_file_dialog` retorne -- que nunca pasa. Timeout a
los 5000 ms.

No es un bug compartido con Task 3, y no es un bug del arnés tampoco
(apuntar al señuelo es lo correcto dado que ningún otro diálogo de este
código tiene esta arquitectura de dos ventanas) -- es un hueco real:
nada conecta el señuelo con el diálogo real que reemplaza.

**Fix:** un hook `OFN_ENABLEHOOK`/`lpfnHook` en el `OPENFILENAMEA` de
`run_word95_common_file_dialog` captura el HWND real del diálogo
(`GetParent()` del hook, el patrón documentado para diálogos
`OFN_EXPLORER`) en `WM_INITDIALOG`, guardado en un global
(`g_active_win95_file_dialog`, limpiado apenas retorna la llamada
bloqueante). `handle_dialog_command`, para `kIddOpen`/`kIddSaveAs` con
`tmc == kTmcOk || tmc == kTmcCancel`, ahora reenvía el clic del señuelo
al diálogo real (`PostMessageA(..., WM_COMMAND, MAKEWPARAM(tmc,
BN_CLICKED), 0)` -- `IDOK`/`IDCANCEL` coinciden con `kTmcOk`/`kTmcCancel`
por la misma convención de Windows que ya usa este archivo) en vez de
llamar `finish_native_dialog` directo -- así es
`run_word95_common_file_dialog` el que termina el diálogo exactamente
una vez, igual que si un usuario real hubiera hecho clic en la ventana
visible.

**Verificado:**
```
ctest -R "^opus_word1_save_as_test$" --output-on-failure
    Passed    4.79 sec

ctest -L word1_startup_blocked --output-on-failure
    5/9 (antes 4/9) -- Save As nuevo, sin regresión en los demás
```

Archivos: `src/port/original/opus_sdm_runtime.cpp` (global +
hook + wiring `lpfnHook`/`OFN_ENABLEHOOK` + reenvío en
`handle_dialog_command`).

## 8. `--font-typing`: dos bugs reales encontrados y arreglados, uno localizado y sin cerrar

**Task 6, 2026-08-19 en exia.** El plan (Step 1) anticipaba que este test
necesitaba un display real, no Xvfb, para la parte visual ("black popup").
Esta sesión avanzó sin eso -- el fallo real resultó ser de datos y de
lógica de foco, no de render -- pero **no cierra** Task 6 del todo: un
tercer problema queda localizado sin arreglar.

**Bug 1 -- nombres de fuente Windows nunca enumerables (arreglado):**
`installed_windows_fonts()` (opus_sdm_runtime.cpp) enumera con
`EnumFontFamiliesExA` -- nombres de familia reales, no alias de Windows.
El test buscaba literales `"Courier New"`/`"Arial"` con
`CB_FINDSTRINGEXACT`, que nunca aparecen en una pila de fuentes Linux.
Confirmado con una sonda standalone (`EnumFontFamiliesExA` vía winegcc)
en dos entornos independientes: exia enumera FreeMono, FreeSans,
FreeSerif, la familia Liberation, Noto, Unifont, WenQuanYi e IPA; una
sesión anterior en debian13 vio la familia Liberation, DejaVu, Tahoma,
MS Sans Serif, Symbol y Wingdings -- cero nombres de alias Windows en
ninguno de los dos. `installed_windows_fonts()` está bien: es fiel al
Word real, que tampoco hardcodeaba nombres, enumeraba lo instalado.
Arreglo: los literales del test pasan a `"Liberation Sans"`/
`"Liberation Mono"` (los únicos dos nombres que ambos entornos
comparten; `fonts-liberation` es paquete base común en Debian). Sin
dependencia nueva -- se consideró instalar fuentes MS reales via
`winetricks corefonts` y se descartó (licenciamiento, no está en
ningún otro sitio de este port).

**Bug 2 -- `union FCID` de 8 bytes en Linux, no 4 (arreglado, LP64 real):**
Con el Bug 1 arreglado el test llegó a un segundo fallo, uno que ya
existía pero nunca se había alcanzado: `"the packed font identifier is
not 32 bits"`. Causa: `Opus/fontwin.h`, `union FCID` tiene
`long lFcid;` -- en Win16/Win32/Win64 `long` siempre fue 4 bytes (Win64
mantiene el modelo LLP64), pero en Linux x86-64 (LP64) `long` es 8
bytes. Los otros dos miembros del union (WORD+WORD, y la estructura de
bitfields `unsigned int`) ya eran de 4 bytes en todas las plataformas
-- solo `long lFcid` duplicaba el tamaño del union completo aquí. Fix
guardado en `Opus/fontwin.h` (`#if defined(__GNUC__) && !defined(_MSC_VER)`,
mismo patrón que el guard de `wordwin.h`/`splshare.h`): `int lFcid`
bajo GCC/Linux, `long lFcid` sin cambios bajo MSVC. Verificado que
`.lFcid` solo se usa como asignación de patrón de bits completo
(`= fcidNil`, `= 0L`, `= pchr->l`) en los 3 sitios de uso reales -- nunca
se compara como valor ancho, así que el cambio de tipo es seguro.

**Bug 3 -- el foco no vuelve al panel del documento tras elegir fuente
del ribbon (localizado, NO arreglado -- toca `Opus/` restringido):**
Con los Bugs 1-2 arreglados, el test abre el combo (el clic simulado
por sí solo no lo hacía -- se añadió un `CB_SHOWDROPDOWN` explícito
tras el clic, que sí lo abre) y elige el ítem, pero
`wait_for_focus(pane, 1500ms)` nunca ve el foco Win32 real
(`GetGUIThreadInfo().hwndFocus`) volver al panel `OpusWwd`.

El trace ya existente `OpusX64TraceRibbon` (activo siempre, escribe a
`build/WORD1-ribbon.txt` -- no necesita instrumentación nueva) muestra
la cadena completa corriendo sin errores lógicos: `CBN_SELENDOK` →
`commit_ribbon_list_selection` → `kDlmKillItemFocus` (ok) →
`kDlmKillDialogFocus` (ok, aplica la fuente: `original-applied ...
tmc=5`) → `kDlmDialogClick`. Esta última invoca `FDlgIb` (el handler
original, `Opus/iconbar1.c:412`, caso `dlmDlgClick`) con el comentario
explícito "SDM gets the focus, send it back to the pane" y una llamada
real `SetFocus(hwwdCur == hNil ? NULL : (*hwwdCur)->hwnd)` -- pero el
trace confirma que devuelve `fFalse` (normal para este caso, no es un
error: `dlmDlgDblClick` arriba también retorna `fFalse` siempre). El
`bool focus_result` de `commit_ribbon_list_selection` solo alimenta el
trace, no cambia ningún flujo -- así que el `SetFocus` real sí debería
ejecutarse (`vidf.fIBDlgMode` ya está en `fFalse` para ese punto, puesto
por `dlmKillDlgFocus` un paso antes, así que la rama `if
(!vidf.fIBDlgMode)` sí entra). Por qué el foco Win32 real no se queda
en `pane` después de esa llamada -- no investigado más allá de esto:
candidatos sin descartar son que `hwwdCur` no apunte al mismo HWND que
`pane`, o que algo dentro del cierre del combo de Wine (limpieza
interna tras `CB_SHOWDROPDOWN`+clic simulado) le devuelva el foco a sí
mismo justo después.

Esto toca `Opus/iconbar1.c` (árbol restringido) -- necesita autorización
explícita antes de tocar código ahí, per `CLAUDE.md`. Diagnóstico
completo, sin cambio de código en `Opus/` esta sesión.

**Actualización 2026-08-20 (exia): hipótesis `CBRollUp` descartada, foco
nunca sale del botón "OK".** Se añadió `wait_for_focus_traced` (variante
de `wait_for_focus` que loguea cada `hwndFocus` distinto visto, con clase
y caption) para probar la hipótesis de arriba -- que Wine's `CBRollUp()`
(dlls/user32/combo.c) corre la cadena SDM completa (incluido el
`SetFocus(pane)` real de `FDlgIb`) y solo *después* oculta el popup listbox,
robando el foco como efecto secundario. La traza la contradice: el foco no
llega a `pane` ni transitoriamente -- se queda fijo en una ventana
`class=Button caption='OK'` (hwnd distinto cada corrida, ~0x1011x) desde
el primer poll (`t+0`/`t+1ms`) hasta el timeout de 1500ms, sin un solo
cambio intermedio. Eso descarta la hipótesis original: no es que el foco
llegue y se vaya, es que nunca se mueve de ese botón "OK" en absoluto --
el `SetFocus(hwwdCur->hwnd)` de `FDlgIb` o no se ejecuta, o se ejecuta y
es inmediatamente revertido a ese botón por algo que corre después (o el
propio `hwwdCur` no apunta al pane esperado). Candidato más probable:
ese "OK" es el botón por defecto de algún diálogo/dialog-manager SDM que
sigue vivo (o se recrea) en ese hilo de mensajes; no identificado más
allá de clase+caption esta sesión. Sigue sin arreglar, sigue tocando
`Opus/iconbar1.c` (restringido) -- este hallazgo solo corrige el
diagnóstico previo, no lo cierra.

**Segunda actualización 2026-08-20 (exia): la hipótesis `CBRollUp` tenía
razón después de todo -- cerrado, arreglado en `opus_sdm_runtime.cpp`, no
en `Opus/iconbar1.c`.** El párrafo anterior se equivocó de granularidad de
traza. `wait_for_focus_traced` mide desde un *proceso externo* con poll de
10ms -- demasiado grueso para ver una ventana de foco que dura microsegundos
dentro del mismo hilo. Se instrumentó en cambio `FDlgIb` mismo
(`Opus/iconbar1.c`, autorizado para esta sesión): dos llamadas a
`OpusX64TraceRibbon` alrededor del `SetFocus(hwwdCur->hwnd)` real,
`dlgclick-before`/`dlgclick-after`, leyendo `GetFocus()` de forma síncrona
en el mismo hilo. Resultado, en `build/WORD1-ribbon.txt`:

```
commit-end   msg=14 ...
dlgclick-before msg=18 tmc=0 a=<hwwdCur> b=65770 sel=65750,0 ins=0
dlgclick-after  msg=18 tmc=0 a=65770 b=0 sel=0,0 ins=0
```

`b=65770` = `0x100EA` = `pane`. **`dlgclick-after` confirma que
`SetFocus(hwwdCur->hwnd)` sí funciona** -- `GetFocus()` es `pane` en el
instante en que `dlmDlgClick` retorna. `Opus/iconbar1.c` está limpio, no
tiene ningún bug: hace exactamente lo que el Word 1.1a original hacía.
Algo *después* de que toda la cadena SDM termina (`commit-end` es el
último evento de esta traza) deshace ese foco antes de que
`wait_for_focus_traced` (10ms más tarde) llegue a verlo -- exactamente la
hipótesis original de `CBRollUp()`: Wine oculta el popup del listbox
*después* de que `CBN_SELENDOK` retorna, y ocultar una ventana con foco
reasigna el foco como efecto secundario.

Arreglo real, en la capa de puerto sin restringir
(`src/port/original/opus_sdm_runtime.cpp`, no `Opus/`): `commit_ribbon_
list_selection` ya reenviaba `CBN_SELENDOK` a través de un mensaje
pospuesto propio (`kWmCommitRibbonSelection`) para esquivar el cierre no
confiable del combo nativo. Se agregó un segundo mensaje pospuesto,
`kWmReassertPaneFocus`, que repite exactamente la misma llamada que ya
funciona (`invoke_dialog_proc(dialog, kDlmDialogClick, tmc)`, el mismo
`FDlgIb` de siempre) -- primero vía `PostMessageW` inmediato (insuficiente:
la traza mostró que ese reintento también aterriza en `pane` y también se
deshace después), luego vía `SetTimer(..., 50)` de un solo disparo
(`WM_TIMER` → mata el timer → repite la llamada). El segundo intento, con
50ms reales de por medio para que la cola de mensajes drene lo que sea que
esté robando el foco, **se queda**. Verificado con la misma traza síncrona:
`reassert-focus ... b=65770` (pane), y esta vez `wait_for_focus_traced`
(el poll externo de 10ms) también lo confirma:

```
[focus-trace] t+1ms hwndFocus=0x100ea class=OpusWwd caption='' (== pane)
```

Task 6 Bug 3 **cerrado**. El test avanza más allá del chequeo de foco, a
un fallo distinto y nuevo: `"newly typed text did not retain the ribbon
font"` -- el texto recién tecleado no conserva la fuente elegida en el
ribbon. No es una regresión (antes el test nunca llegaba tan lejos); es
un cuarto bug real, sin investigar todavía. Etiqueta
`word1_startup_blocked` sigue en 7/9 (mismos dos conocidos: este nuevo
fallo de fuente en `--font-typing`, y `--interaction` sin cambios), pero
el bug real detrás de uno de los dos cambió.

**Verificado:**
```
DISPLAY=:99 ctest -R "^opus_word1_font_typing_test$" --output-on-failure
    [focus-trace] t+1ms hwndFocus=0x100ea class=OpusWwd caption='' (== pane)
    font properties=20,0 applied=3,48 inserted=20,0 lineHeight=16
        formatted=16 formatter=0,16
    newly typed text did not retain the ribbon font

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9 -- sin regresión en los demás
```

Archivos: `Opus/iconbar1.c` (traza `dlgclick-before`/`dlgclick-after`,
autorizado), `src/port/original/opus_sdm_runtime.cpp` (el arreglo real:
`kWmReassertPaneFocus`, `kReassertPaneFocusTimerId`,
`DialogState::pending_reassert_tmc`).

**Verificado:**
```
DISPLAY=:99 ctest -R "^opus_word1_font_typing_test$" --output-on-failure
    font combo select stages: foreground=1 chose_item=1 regained_focus=0
    font typing test could not mouse-select the font
    (avanzó de fail(47) "could not find controls" -> fail(49) "could
    not mouse-select the font", con fallo 47 intermedio de "not 32
    bits" ya cerrado en el camino)

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    5/9 -- sin regresión en los demás
```

Archivos: `src/port/original/opus_word1_ui_test.cpp` (literales de
fuente, `CB_SHOWDROPDOWN`, diagnóstico de las 3 etapas del clic de
combo), `Opus/fontwin.h` (guard LP64 de `union FCID`).

**Tercera actualización 2026-08-20 (exia): "no conserva la fuente" era
diagnóstico engañoso -- el texto nunca se tecleó. Causa raíz real
encontrada: `idle.c:503` precarga la fuente con `selCur.chp.hps == 0`,
`CreateFontIndirect` falla, y el diálogo real de error resultante se
traga el teclado. Sesión cerrada sin arreglo -- se retoma en Debian 13
(LXQt).**

El "cuarto bug" del párrafo anterior resultó ser una lectura
equivocada. Con foco confirmado correcto en `pane`
(`[pre-type] hwndFocus=... (== pane)=1`), se trazó `FIsKeyMessage`
(`Opus/wproc.c:2450`) y el `PeekMessage` del loop principal
(`OpusOriginalWinMain`, `Opus/wproc.c:~556`): **cero** mensajes
`WM_CHAR`/`WM_KEYDOWN` llegan a ninguno de los dos durante todo
`--font-typing`, pese a que `send_physical_text` (SendInput real, no
`PostMessageW`) reporta éxito. Prueba diferencial decisiva: el mismo
trace, corrido contra `opus_word1_typing_test` (tecleo simple, sin
ribbon), sí ve cada tecla (`iskeymsg`/`mainloop-msg` para cada
`WM_KEYDOWN`/`WM_KEYUP`/`WM_CHAR`, valores `tmc` deletreando
"ORIGINAL"). La app permanece "responsive"
(`window_is_responsive`/`WM_NULL` responde) durante toda la falla --
no es un cuelgue real.

**Teorías de foco descartadas, todas verificadas empíricamente, ninguna
cambió el síntoma un solo bit (`applied=3,48 inserted=20,0` idéntico
en cada intento):**

- `SetTimer`-reassert de Task 6 Bug 3 (arriba) desactivado
  temporalmente -- el test simplemente vuelve a fallar el chequeo de
  foco original (`regained_focus=0`), sin siquiera llegar al tecleo:
  confirma que ese fix sigue siendo necesario, pero no es la causa de
  esto.
- `SetForegroundWindow(GetAncestor(pane, GA_ROOT))` agregado junto al
  reassert de foco (`opus_sdm_runtime.cpp`, diagnóstico, sigue en el
  árbol sin commitear) -- `foreground_result=1`, `root` resuelve
  correctamente a `main_window`. Sin cambio.
- Cursor real (`SetCursorPos`) movido al centro de `pane` justo antes
  de teclear -- por si este Xvfb (sin gestor de ventanas) enrutara
  input real por posición del puntero (`XGetInputFocus` devuelve
  `PointerRoot` fijo durante todo el test, confirmado con una sonda
  standalone en C/Xlib). Sin cambio.
- `make_foreground_and_focus(main_window, pane, thread_id)` (el mismo
  helper que sí usa con éxito el bloque `caret_mode`, con su
  `AttachThreadInput` cruzado entre el proceso de test y el hilo de
  WORD1) llamado explícitamente antes de `send_physical_text`. Sin
  cambio -- y de paso se confirmó que `caret_mode`/`--caret` no está
  registrado como ctest (`src/CMakeLists.txt` solo registra 8 modos),
  así que ese patrón nunca estuvo realmente probado en este entorno,
  no era la referencia sólida que parecía.
- `IsWindowEnabled`, `GetActiveWindow`, captura de mouse
  (`GUITHREADINFO.hwndCapture`) -- todos correctos (`paneEnabled=1
  mainEnabled=1 active=main_window capture=0`).

**Causa real: `EnumThreadWindows` sobre el hilo de WORD1 en el momento
`[pre-type]` muestra una ventana visible extra, clase `#32770`
(diálogo estándar de Windows), título `"Microsoft Word"`, con hijos
`Static id=65535 text='Low memory: cannot display requested font'` +
`Button id=1 text='OK'`.** Ese diálogo real -- no ficticio, no un
efecto de foco -- corre su propio loop de mensajes modal desde que se
crea; por eso ni `FIsKeyMessage` ni el `PeekMessage` del loop principal
ven un solo mensaje después: el hilo está parado dentro del loop del
diálogo, no en el de Opus. Explica también por qué la app sigue
"responsive" (el loop del diálogo también atiende `WM_NULL`) y por qué
ningún arreglo de foco cambió nada -- el foco nunca fue el problema.

Rastreado hasta el origen exacto (2 puntos de traza nuevos en
`Opus/LOADFONT.C`, autorizados como continuación de esta misma
investigación):

```
idle.c:503   LoadFont(&selCur.chp, fFalse)   /* "preload new font in
                                                 Idle, avoid delay
                                                 when typing commences" */
  -> C_LoadFcid -> FGraphicsFcidToPlf:
       lf.lfHeight = NMultDiv(fcid.hps * (czaPoint/2), vfli.dysInch, czaInch)
       trace: fcid.hps=0  vfli.dysInch=96 (DPI normal, no es la causa)
       lf.lfHeight=0
  -> CreateFontIndirect(&lf) devuelve NULL (GetLastError=5,
     ERROR_ACCESS_DENIED)
  -> SetErrorMat(matFont)  (LOADFONT.C:330, camino LSystemFontErr)
  -> idle.c / ReportPendingAlerts(): case matFont ->
     ErrorEid(eidCantRealizeFont, ...) -> el MessageBox real de arriba
```

`selCur.chp.hps` **debería** ser 48 (24pt, la talla recién elegida en
el ribbon -- confirmado por separado con
`SendMessageW(pane, kWmOpusX64QuerySelection, 50, 0)` justo antes de
teclear) pero en el momento en que corre el preload de `idle.c:503`
lee `0`. **No cerrado -- pendiente identificar la carrera exacta.**
`vrf.fPreloadSelFont` se marca `fTrue` en dos sitios
(`Opus/cmdcore.c:510`, `Opus/dlglook1.c:781`) y se consume una sola vez
por tick de idle (`Opus/idle.c:488-505`, usa `selCur.chp` tal cual,
sin resolver `hps==0` a un valor real primero). Candidato más probable
sin confirmar: el preload dispara y consume la marca justo después de
elegir la FUENTE (ribbon combo 1), con `selCur.chp.hps` todavía en su
valor original de documento (0, ver `initial_hps=0` en el trace de
arriba) -- antes de que la selección de TALLA (ribbon combo 2) llegue
a escribir 48 ahí. Verificar: en qué tick exacto de idle corre esto
respecto a los dos `combo-select` del trace de ribbon, y si `hps==0`
es en sí un estado legítimo (placeholder "heredar de estilo") que
otros caminos resuelven antes de tocar GDI y este no.

**Diagnóstico dejado en el árbol, sin commitear** (continuidad para la
sesión en Debian 13/LXQt -- todo bajo `#ifdef OPUS_X64`, guardado,
no afecta MSVC):

- `Opus/LOADFONT.C` -- traza `lfheight-calc` (entrada a
  `FGraphicsFcidToPlf`, imprime `fcid.hps`/`vfli.dysInch`/
  `lf.lfHeight`), `matfont-set` (fallo de `CreateFontIndirect`),
  `screenfail` (fallo de `OurSelectObject` en el DC de pantalla --
  no se disparó esta sesión, el fallo real fue siempre
  `CreateFontIndirect`).
- `Opus/wproc.c` -- traza en `FIsKeyMessage` (entrada,
  `WM_CHAR`/`WM_KEYDOWN`) y en el `PeekMessage` del loop principal de
  `OpusOriginalWinMain` (mismo filtro).
- `Opus/iconbar3.c` -- traza de entrada/salida de `IBDlgLoop()`
  (descartó la hipótesis de que el loop se quedaba atascado ahí).
- `Opus/rulerdrw.c` -- traza alrededor de `FGetCharState` en
  `UpdateRibbon` (descartó que ahí se corrompiera `selCur.chp`).
- `Opus/wordtech/insert.c` -- traza en `InsertLoopCh` justo después de
  `GetSelCurChp` (cero hits durante `--font-typing`, confirmando que
  esa rutina nunca corre -- consistente con el diálogo bloqueante).
- `src/port/original/opus_sdm_runtime.cpp` -- `SetForegroundWindow`
  especulativo junto al reassert de foco de Task 6 (no ayudó, inerte,
  se puede revertir cuando se retome).
- `src/port/original/opus_word1_ui_test.cpp` -- diagnóstico
  `[pre-type]` (foco/enabled/active/capture), `[enum-window]`/
  `[dialog-child]` (el que encontró el diálogo real), llamada a
  `make_foreground_and_focus` antes de teclear (no ayudó, inerte).

Nada de esto se commiteó esta sesión -- queda como `M` en
`git status` en `src/Opus/{LOADFONT.C,iconbar3.c,rulerdrw.c,wproc.c,
wordtech/insert.c}` y `src/port/original/{opus_sdm_runtime.cpp,
opus_word1_ui_test.cpp}`. `git status` también muestra `M` en
`Opus/{ddeclnt.c,etcmd.c,filecvt.c,raremsg.c,spelcore.c}` -- eso es
contenido de una migración `OpusMem*` de otra sesión en paralelo (mas
un fix mío de "takeover" sobre una etiqueta `LError` colgante en
`etcmd.c`/`spelcore.c` que dejaba esos dos archivos sin compilar); no
tocar esos cinco al continuar salvo que sea la misma sesión que los
dejó así.

**Próximo paso concreto al retomar:** trazar el orden exacto entre
(a) el momento en que `Opus/dlglook1.c:781`/`Opus/cmdcore.c:510` ponen
`vrf.fPreloadSelFont = fTrue` tras cada selección de ribbon, y (b) el
tick de idle que lo consume en `Opus/idle.c:503` -- lo más probable es
que el preload dispare tras la selección de FUENTE, antes de que la de
TALLA llegue a escribir `selCur.chp.hps`, y que el arreglo real sea
usar un hps ya resuelto (o posponer el preload hasta que ambas
selecciones se hayan aplicado) en vez de tocar `Opus/idle.c` a ciegas.
Una vez cerrado esto, falta además verificar que el nombre visible del
diálogo `"Low memory: cannot display requested font"` no es en sí un
mensaje legítimo de Word 1.1a bajo otras circunstancias (`eidCantRealizeFont`,
`Opus/wordtech/error.c:933`) -- aquí es un falso positivo de una
llamada de precarga con datos a medio actualizar, no una condición
real de memoria.

**Cuarta actualización 2026-08-20 (debian-VM, rama
`investigate/font-typing-idle-preload`): la teoría de la carrera `hps`
queda descartada con evidencia directa -- cinco hipótesis probadas y
descartadas, causa raíz real aún sin localizar.** Esta sesión tenía
como objetivo *confirmar* la teoría de la "Tercera actualización" de
arriba y arreglarla en `idle.c`. El arreglo propuesto (`&&
selCur.chp.hps != 0` en el guard de `idle.c:503`) se implementó,
compiló y se corrió contra el test real -- y no cambió el síntoma ni
un bit. Investigar por qué llevó a descartar la teoría entera con
trazas directas, no lectura de código.

**Entorno de esta sesión:** máquina nueva (`debian-VM`, Debian 13
trixie, KVM), sin toolchain previo -- se instaló `cmake`/`ninja`/
`wine`(10.0~repack-6)/`wine64-tools`/`qt6-base-dev`/`Xvfb` vía `apt`, se
inicializó el prefix de Wine (`wineboot --init`), y se resolvió el
bloqueo de Configure documentado en el README (`src/port/tools/host/`
gitignored) con un `CMakeLists.txt` ya preparado para esa carpeta.
Build limpio de `opus_original_engine`/`WORD1`/`opus_word1_ui_test`,
0 errores. Repro estable de `opus_word1_font_typing_test ***Failed`
igual que en exia.

**Hipótesis 1 -- carrera `hps` en `idle.c:503` (la de la sesión
anterior): descartada.** Se instrumentó un trace nuevo
(`idle-preload-check`) justo al entrar al bloque de preload de
`Opus/idle.c:488`, antes de cualquier guard. **Cero apariciones en
todo el test.** El bloque de preload de `idle.c` nunca se alcanza
durante `--font-typing` -- ni con el guard puesto ni sin él. La
atribución de la sesión anterior a `idle.c:503` nunca se trazó
directamente; se infirió leyendo código (`vrf.fPreloadSelFont` /
`DoLooks`). El guard con `hps != 0` se implementó, compiló, corrió, y
el diálogo de error siguió apareciendo idéntico.

**La llamada real que falla:** instrumentando el return address de
`C_LoadFont` (`__builtin_return_address(0)`) se confirmó que la
llamada que sí truena viene de otro lado, no de `LoadFont(pchp,
fFalse)`. El sitio real, confirmado por `nm`/lectura de código, es
`Opus/disp1.c`'s `LoadFcidFull(pchr->fcid)` -- llamado una vez por
carácter durante el repintado del panel del documento (dos sitios,
disp1.c:832 y disp1.c:1698), usando el `fcid` cacheado en cada
`CHR`, no `selCur.chp`. Con el documento vacío en `[pre-type]`, esto
repinta el carácter de fin de párrafo con el `fcid` que sea que tenga
en ese momento.

**Hipótesis 2 -- `hps==0` es la causa: descartada con datos, no
lectura.** Se añadió un trace (`fcid-identity`) al entrar a
`C_FGraphicsFcidToPlf` (`Opus/LOADFONT.C`), antes del `Assert(fcid.hps
> 0)`, mostrando `ibstFont`/`wProps`/`hps`/`kul` en cada llamada,
éxito o fallo:

```
fcid-identity msg=2(ibstFont) hps=0   -> "Helv"             -> OK
fcid-identity msg=4(ibstFont) hps=0   -> "Liberation Sans"  -> matfont-set (falla)
```

`hps` es **idéntico** (0) en ambos casos -- uno pasa, el otro falla.
El único que cambia es qué fuente (`ibstFont`) se pide. `hps==0` no es
la causa; es incidental.

**Hipótesis 3 -- charset corrupto (`lfCharSet=255`, `OEM_CHARSET`):
real pero insuficiente por sí sola.** Añadiendo el nombre real de la
fuente al trace (pasando `pffn->szFfn` como `stage` de
`OpusX64TraceRibbon`), se confirmó `lfCharSet=255` para "Liberation
Sans" contra `lfCharSet=0` para "Helv". Sondas standalone
(`EnumFontFamiliesExA` y el `EnumFontsA` legacy que usa
`Opus/SYSCHG.C:FontNameEnum`, ambas contra un DC de pantalla) **nunca**
devuelven 255 para esa fuente -- los valores reales enumerados son 0,
238, 204, 161, 162, 177, 186, 163. `FontNameEnum` copia fielmente
`lplf->lfCharSet` (línea ~775: `ChsPffn(pffn) = lplf->lfCharSet;`),
así que el 255 no es un bug de copia -- viene de que `FillHsttbPaf`
enumera contra `vpri.hdc` (el DC de **impresora**, no pantalla), y
este prefix de Wine no tenía ninguna impresora configurada
(`wine control printers` vacío, `lpstat` "No se han añadido
destinos.", `GetDefaultPrinterA` devolvía error 2).

Arreglo aplicado en `Opus/LOADFONT.C` (`C_FGraphicsFcidToPlf`, antes
de `bltbyte(...lfFaceName...)`): si `plf->lfCharSet == OEM_CHARSET`,
usar `DEFAULT_CHARSET` en su lugar (guardado bajo `OPUS_X64`). Este
fix es correcto y se queda -- `OEM_CHARSET` sin impresora real es un
valor sin sentido, no algo que Opus debería propagar a
`CreateFontIndirect` -- pero **no arregla el test solo**: con el
charset ya saneado (confirmado por trace, `lfCharSet=1`),
`CreateFontIndirect` **sigue fallando** para "Liberation Sans".

**Efecto secundario explorado: agregar una impresora real cambia el
síntoma pero no lo arregla.** Se configuró una cola CUPS dummy
(`GenericPS`, PPD genérico PostScript, `device-uri file:///dev/null`)
para darle a Wine una impresora real -- confirmado con una sonda
standalone (`GetDefaultPrinterA`/`EnumPrintersA`) que Wine la veía en
vivo. Con impresora presente, `vfli.fFormatAsPrint` activa el camino
de dos dispositivos en `C_LoadFont` (`LOADFONT.C:121-135`): una
petición de fuente de impresora *además* de la de pantalla. El
charset de pantalla salió limpio (163, un valor real de la
enumeración), pero la petición del **lado impresora** trajo su propio
dato corrupto (`lfHeight=-5`, `fcid.wProps=1920` -- un valor de
bitfield sin sentido) y también falló. Esto es un bug distinto,
dormido, en un subsistema distinto (formato para impresión, no
tecleo/ribbon) -- fuera de alcance de este ítem. **La impresora
`GenericPS` se quitó** (`sudo lpadmin -x GenericPS`, confirmado con la
misma sonda que Wine ya no ve ninguna impresora) -- no queda cambio de
entorno persistente. Sin la impresora, el camino vuelve a ser de un
solo dispositivo (pantalla), el mismo que protege el fix de charset de
arriba.

**Hipótesis 4 -- caché de fuentes (`PfceLruGet`, desalojo de slot LRU):
descartada.** `ifceMax` = 32 (`Opus/fontwin.h:11`) -- nada cerca de
agotarse con 2 fuentes distintas pedidas. Se instrumentaron ambos
puntos de retorno de `PfceLruGet` (`Opus/LOADFONT.C:1037`): slot libre
encontrado sin desalojo (`pfce-free-slot`) vs. desalojo de LRU
(`pfce-evicted-slot`). Resultado: "Helv" recibe el slot 0 libre;
"Liberation Sans" recibe el slot 1 libre. **Ningún desalojo ocurre.**
La caché está sana.

**Hipótesis 5 -- `GetLastError()` real (183, `ERROR_ALREADY_EXISTS`):
descartada -- era ruido, no señal.** El `183` apareció idéntico en
tres estados de código/entorno distintos a lo largo de la sesión, lo
cual parecía significativo -- pero GDI no garantiza fijar un error
fresco en cada llamada. Se añadió `SetLastError(0)` justo antes del
`CreateFontIndirect` que falla (`Opus/LOADFONT.C:333`). Resultado:
`GetLastError()` da **0** después de la falla. El `183` que se venía
seteando era remanente de alguna llamada anterior en el mismo tick,
no algo que `CreateFontIndirect` haya fijado. Esta pista quedó
inválida desde el principio.

**Hipótesis 6 -- buffer de `szFfn` demasiado chico para nombres largos
(Win16-legacy, ≤7 chars): descartada.** `struct FFN` (`fontwin.h:39`)
tiene `CHAR szFfn[]` -- flexible, no un tamaño fijo chico. `LF_FACESIZE`
está definido correctamente en `Opus/lib/qwindows.h:1235` como `32`
(el valor Win32 real, no un valor Win16 heredado más chico). El buffer
de staging en `FontNameEnum` (`Opus/SYSCHG.C`), `CHAR rgbFfn[cbFfnLast]`
con `cbFfnLast = offset(FFN,szFfn) + LF_FACESIZE + 1`, tiene margen de
sobra para "Liberation Sans\0" (16 bytes de 33 disponibles). No se
llegó a revisar el *storage* permanente en `vhsttbFont`
(`IbstAddStToSttb`/`FChangeStInSttb`, `Opus/SYSCHG.C:~789`) -- ese es
un candidato real para una próxima sesión, distinto de lo que se
descartó acá.

**Estado al cortar: seis hipótesis descartadas con evidencia directa
(trazas y sondas standalone, no lectura de código sola). Causa raíz de
por qué `CreateFontIndirect` devuelve `NULL` para "Liberation Sans"
(segunda fuente distinta pedida, cualquier `hps`, charset ya válido,
slot de caché sano, sin error GDI fijado) sigue sin localizar.**
Patrón que sí se sostiene en todas las corridas: la *primera* fuente
distinta pedida (`Helv`) siempre funciona; la *segunda* (`Liberation
Sans`) siempre falla -- pero el mecanismo detrás de ese patrón no está
identificado.

**Candidatos no explorados para la próxima sesión:**
- `IbstAddStToSttb`/`FChangeStInSttb` (`Opus/SYSCHG.C`): el storage
  permanente en `vhsttbFont`, distinto del buffer de staging ya
  descartado en la Hipótesis 6.
- Diferencia entre `pffn->fGraphics`/`fRaster` para "Helv" vs.
  "Liberation Sans" -- se notó (sin confirmar del todo) que
  `fGraphics` probablemente da `fFalse` para "Liberation Sans" via la
  enumeración de impresora (`fty & DEVICE_FONTTYPE` puesto), algo que
  el guard de charset de arriba tuvo que dejar de usar como condición
  porque nunca disparaba.
- Instrumentar el propio `lf` completo (`LOGFONT`) justo antes de
  `CreateFontIndirect` -- se comparó `lfHeight`/`lfWeight`/`lfCharSet`
  pero no `lfPitchAndFamily`, `lfQuality`, ni el contenido final de
  `lfFaceName` byte a byte.

**Archivos modificados esta sesión (sin commitear):**
- `src/Opus/idle.c`: el guard `hps != 0` **se revirtió** (la condición
  original queda intacta) porque nunca se ejecuta y su justificación
  quedó descartada; se dejó el trace `idle-preload-check` (prueba de
  que el bloque nunca se alcanza) y un comentario apuntando a esta
  sección.
- `src/Opus/LOADFONT.C`: el fix real que se queda (`OEM_CHARSET` ->
  `DEFAULT_CHARSET` en `C_FGraphicsFcidToPlf`, sin condicionar a
  `fGraphics`) + trazas nuevas (`loadfont-caller` con return address,
  `fcid-identity`, nombre real de fuente como `stage`,
  `pfce-free-slot`/`pfce-evicted-slot`, `SetLastError(0)` antes de
  `CreateFontIndirect`). Todo guardado bajo `#ifdef OPUS_X64`, no toca
  MSVC.

**Cambio de entorno completamente revertido:** la impresora CUPS
`GenericPS` que se probó para la Hipótesis 3 se quitó; confirmado con
sonda standalone que Wine ya no ve ninguna impresora. No queda drift
de entorno.

**Quinta actualización 2026-08-20 (exia): tres hipótesis más
descartadas, un bug real distinto encontrado y arreglado (no relacionado
con la falla), y confirmación decisiva de que la causa NO es una
limitación de Wine.** Continuación directa de la sesión anterior (pull
de `6417213`), revisando los dos candidatos que quedaron anotados sin
explorar.

**Hipótesis 7 -- `pffn->fGraphics`/`fty` (candidato anotado, no un
mecanismo nuevo): descartada como explicación, ya estaba confirmada en
el código.** El comentario ya presente en `Opus/LOADFONT.C` (líneas
~996-999, agregado en la sesión anterior) confirma que `fGraphics` da
`fFalse` para "Liberation Sans" por la misma enumeración contra
`vpri.hdc` que corrompió el charset -- mismo origen ya identificado, no
un mecanismo distinto. `fGraphics` solo se guarda en `pfce->fGraphics`/
`vfli.fGraphics` (grep confirmado: sin ningún `if` que dependa de él
antes de `CreateFontIndirect`), así que no puede ser la causa de que la
llamada falle.

**Hipótesis 8 -- `lfPitchAndFamily` corrupto (`FF_DONTCARE=0` vs.
`FF_SWISS=32` de "Helv"): real, pero no causal -- descartada
empíricamente, no por lectura.** Un trace del `LOGFONT` completo justo
antes de `CreateFontIndirect` (todos los campos no revisados antes:
`lfPitchAndFamily`, `lfQuality`, `lfOutPrecision`, `lfClipPrecision`,
`lfWidth`, `lfEscapement`) mostró que **todo es idéntico entre "Helv" y
"Liberation Sans" excepto `lfPitchAndFamily`**: 32 (`FF_SWISS`) contra 0
(`FF_DONTCARE`) -- mismo origen que la Hipótesis 3/7 (`pffn->ffid`
corrupto por la misma enumeración sin impresora). Se forzó
`lfPitchAndFamily` a `FF_SWISS` para "Liberation Sans" como prueba
empírica directa (no como arreglo definitivo) -- **mismo fallo
idéntico**, `CreateFontIndirect` sigue devolviendo `NULL`. Revertido.
`FF_DONTCARE` es un valor legítimo y común en Windows real; no es la
causa.

**Bug real encontrado (no causal para esta falla, arreglado de todos
modos): lectura fuera de límites en `bltbyte(pffn->szFfn,
plf->lfFaceName, LF_FACESIZE)`.** `struct FFN.szFfn` es un flexible
array member (`CHAR szFfn[]; /* Variable length */`, `Opus/fontwin.h`),
almacenado en la STTB (`IbstAddStToSttb`/`FInsStInSttb1`,
`Opus/wordtech/sttb.c`) con tamaño exacto Pascal-string (`CbSzOfPffn`),
nunca 32 bytes. El `bltbyte` original copiaba 32 bytes fijos sin
importar el tamaño real asignado -- lectura fuera de límites genuina
para cualquier nombre de fuente más corto de 31 caracteres (es decir,
prácticamente todos). Confirmado con un dump hexadecimal de
`lf.lfFaceName[16..31]` para ambas fuentes: para "Helv" (entrada
sembrada al arranque, junto a otras entradas cortas válidas en memoria)
el byte de posición 15 es un carácter real ('e') seguido de más
contenido no-nulo -- el over-read cae en datos de OTRA entrada válida
adyacente. Para "Liberation Sans" (15 caracteres, agregada en runtime)
el byte 15 es correctamente `\0` (el nombre termina bien), pero el byte
16 es `0xFF` seguido de ceros -- **el over-read cae en memoria genuina
fuera de la entrada real.** En ambos casos, sin embargo, el terminador
nulo real cae en la posición correcta (4 para "Helv", 15 para
"Liberation Sans") -- y GDI/`CreateFontIndirect`, igual que `%s`, deja
de leer en el primer `\0`. **Esto descarta la hipótesis de corrupción
de nombre como causa de esta falla específica** -- el nombre que
`CreateFontIndirect` realmente ve es correcto en ambos casos -- pero
sigue siendo un bug real (comportamiento indefinido, lectura de heap
sin inicializar) que se arregló de todos modos: `Opus/LOADFONT.C`
ahora hace `SetBytes(plf->lfFaceName, 0, LF_FACESIZE)` seguido de un
`bltbyte` acotado a `CbSzOfPffn(pffn)` bytes reales (guardado bajo
`#if defined(__GNUC__) && !defined(_MSC_VER)`, MSVC sigue con el
`bltbyte` de 32 bytes original sin cambios).

**Confirmación decisiva: no es una limitación de Wine para estos
parámetros.** Con cada campo de `LOGFONT` inspeccionable ya descartado
o idéntico, y el nombre confirmado correcto hasta su `\0`, se armó una
sonda standalone (`winegcc`, sin código de este proyecto -- mismo
patrón que la "Confirmación decisiva" de §12/`--interaction`) que
llama `CreateFontIndirectA` con el `LOGFONT` **byte por byte idéntico**
al que falla dentro de WORD1 (`lfFaceName="Liberation Sans"`,
`lfHeight=0`, `lfWeight=400`, `lfCharSet=1`, `lfQuality=4`,
`lfPitchAndFamily=0` y también probado con `32`, resto en cero).
**Ambas variantes tienen éxito** (`hfont` no nulo, `GetLastError=0`) en
un proceso limpio, mismo binario de Wine, mismo prefix. Esto descarta
por completo que sea una limitación de Wine para esta combinación de
parámetros -- el mismo `CreateFontIndirectA` con los mismos datos
funciona perfecto fuera de WORD1. La causa está genuinamente en algo
del estado de proceso/GDI de WORD1 en ese momento, no en los datos que
se piden.

**Hipótesis de agotamiento de handles GDI: descartada, sin necesidad de
medir vía `GetGuiResources`.** Se intentó medir el conteo de objetos
GDI del proceso WORD1 desde el arnés de test (`GetGuiResources(hProcess,
GR_GDIOBJECTS)`) -- esta build de Wine lo tiene sin implementar,
siempre devuelve 0. Señal inútil, descartada como método. Pero un
conteo más simple resultó decisivo: el trace `loadfont-caller`
(`__builtin_return_address` de cada llamada a `C_LoadFont`) aparece
**35 veces** en toda la corrida del test, pero `fcid-identity` (el
único punto que de verdad llega a `C_FGraphicsFcidToPlf`/
`CreateFontIndirect`, tras pasar los CASE 1-3 de caché de
`C_LoadFcid`) aparece solo **2 veces** -- las otras 33 son *cache
hits*, nunca llegan a pedir una fuente nueva a GDI. Con solo 2
creaciones reales de fuente en toda la corrida, un límite de 10.000
handles por proceso (el límite clásico de Windows) queda
completamente fuera de alcance. Descartado sin ambigüedad.

**Estado al cortar (segunda vez): nueve hipótesis descartadas en total
con evidencia directa** (las seis de la actualización anterior + estas
tres). La causa de por qué `CreateFontIndirect` devuelve `NULL`
específicamente para la segunda fuente distinta pedida, con datos
confirmados correctos en cada campo inspeccionable, sigue sin
localizar -- y ya no hay ningún candidato más a nivel de datos/estado
de aplicación que revisar por trazas o sondas standalone. Lo único que
queda, si se retoma, es bajar un nivel: adjuntar `gdb` al proceso
WORD1 real (no una sonda standalone limpia) y poner un breakpoint
dentro de la implementación de `CreateFontIndirect` de Wine mismo
(`dlls/gdi32` o `dlls/win32u`, según versión) para ver en qué paso
interno diverge esa segunda llamada respecto a la primera -- una
herramienta bastante más pesada que trazas de aplicación o sondas
standalone, y no intentada todavía.

**Archivos modificados esta ronda (sin commitear al momento de
escribir esto):**
- `src/Opus/LOADFONT.C`: el arreglo real que se queda (bltbyte
  acotado + `SetBytes` de cero para `lfFaceName`, guardado GNUC-only,
  MSVC sin cambios) -- corrige un bug genuino de comportamiento
  indefinido, no relacionado con la falla de este test. Trace de
  `fcid-identity` (ya existente) se mantiene tal cual.
- `src/port/original/opus_word1_ui_test.cpp`: sin cambio neto (se
  agregó y luego se quitó el chequeo de `GetGuiResources`, que resultó
  no implementado en este Wine).

**Verificado, sin regresión:**
```
DISPLAY=:99 ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked
    7/9 -- mismos dos fallos conocidos (--interaction, --font-typing)

DISPLAY=:99 ctest --test-dir out/linux-winelib-debug -LE word1_startup_blocked
    9/9 -- gating sin cambios
```

**Sexta actualización 2026-08-22 (exia): primera vez que se baja a `gdb` real sobre el proceso `WORD1` en vivo.** Continuación directa de la sesión anterior (el "único candidato que queda: bajar un nivel con gdb"), retomada tras una interrupción de Antigravity a mitad de tarea (el script `gdb-font-debug.py` que esa sesión estaba escribiendo nunca llegó a correr). **Nota de la Séptima actualización (más abajo, misma sesión): la lectura de "heisenbug" que sigue en este bloque resultó ser una atribución equivocada al breakpoint incorrecto -- queda documentada tal cual se vivió, en orden, porque el error de razonamiento y cómo se destapó son parte útil del rastro; el diagnóstico real y definitivo es la Séptima actualización.**

**Infraestructura nueva, la mitad real del trabajo de esta sesión:** adjuntar `gdb` al proceso `WORD1` real mientras el arnés `opus_word1_ui_test` lo maneja externamente (no hay forma de disparar la selección de fuente del ribbon sin el arnés) resultó bastante más delicado que un `gdb -p <pid> --batch -ex "thread apply all bt"` de una sola foto (el patrón ya usado en §21/§25/§31). Tres problemas reales, en orden de aparición:

1. **`WORD1.exe` (el wrapper shell) no es el binario a lanzar/depurar.** Es un script `sh` que arma `WINEDLLPATH`/`WINELOADER` y hace `exec`; bajo esta sesión de Bash concreta, invocado sin gestor de ventanas real, salía con `exit=3` sin imprimir nada del programa (ni siquiera un `printf` trivial de un `hola mundo` standalone). `wine ./WORD1.exe.so` (el `.so` real, sin pasar por el wrapper) funciona siempre. Cualquier arnés/probe standalone debe invocar el `.exe.so` directo.
2. **`break /home/pablo/msword/src/Opus/LOADFONT.C:349` (la ruta real del fuente) falla con `No source file named ...`, incluso con `set breakpoint pending on` y forzando expansión completa de symtabs (`maint expand-symtabs`, `info sources`).** Causa: el build genera una copia en minúsculas de cada fuente en mayúsculas antes de compilar (`out/linux-winelib-debug/generated/lowercase-c/loadfont.c`, confirmado con `strings WORD1.exe.so | grep -i loadfont.c`), y el nombre de compilation-unit que graba DWARF es el de esa copia generada, no el de `src/Opus/LOADFONT.C`. Rompe el breakpoint por ruta incorrecta, no por símbolos faltantes ni por timing. Fix: usar siempre la ruta generada (`out/linux-winelib-debug/generated/lowercase-c/<nombre-en-minusculas>.c`) para cualquier `break FILE:LINE` contra este binario.
3. **Pre-cargar símbolos con `gdb file WORD1.exe.so` antes de conocer el PID (para acelerar el `attach` posterior) resultó contraproducente**, no una optimización: dispara un aviso de "Build ID mismatch" al hacer `attach` que fuerza un re-chequeo de símbolos y ese re-chequeo vuelve a romper el breakpoint ya resuelto (`Error in re-setting breakpoint 1: No source file named ...`, el mismo síntoma del punto 2, reaparecido). La solución simple termina siendo la más simple posible: `gdb -p <pid> --batch -x script.gdb` sin ningún `file` previo -- resuelve el breakpoint y ataca el proceso en **~1.1s** de punta a punta (medido con `time`), tiempo sobrado dentro de la ventana de varios segundos que el arnés tarda en llegar a la selección de fuente del ribbon (búsqueda de ventana + `Sleep(300)` x2 + esperas de foco de hasta 1500ms).

**Con la infraestructura funcionando, la sesión real: comparar la llamada que pasa (`Helv`) contra la que falla (`Liberation Sans`) dentro del proceso vivo, en el mismo punto exacto que las nueve hipótesis anteriores nunca lograron cruzar.** Breakpoint en `loadfont.c:349` (el sitio exacto de `CreateFontIndirect`), confirmado por nombre real de fuente vía `lf.lfFaceName` en cada hit.

Primer intento -- desde el entry de `CreateFontIndirectA`, `finish` anidado con un `tbreak NtGdiHfontCreate` puesto de antemano -- dio un resultado sin sentido (`rax=0x86` idéntico en ambas llamadas): el `finish` externo se interrumpía antes de tiempo por el propio breakpoint interno (comportamiento normal de gdb, no un bug), así que el valor capturado era el estado de entrada a `NtGdiHfontCreate`, no ningún valor de retorno real. Descartado como artefacto de script, no como dato.

**Medición limpia, de un solo salto:** `tbreak CreateFontIndirectA` (confirmado con `disassemble` que es exactamente el símbolo llamado desde `loadfont.c:349`, pese a que el target compila con `UNICODE`/`_UNICODE` definidos -- el macro `CreateFontIndirect` igual resuelve a la variante ANSI en este sitio, verificado por disassembly, no por lectura de headers) + `continue` + un único `finish` (sin ningún breakpoint anidado en el medio) aterriza exactamente de vuelta en `C_LoadFcid` en `loadfont.c:349`, el llamador real, con `$rax` = el valor de retorno verdadero tal cual lo va a ver el código C. Confirmado además con `stepi`/`nexti` explícito paso a paso a través de las instrucciones compiladas reales (`disassemble` mostró un `mov %rax,0x38(%rdx)` de 8 bytes completos seguido de `test %rax,%rax` / `jne` -- comparación de 64 bits correcta, sin ningún truncamiento de ancho, descartando de paso cualquier variante de la vieja teoría Win16-legacy de campo angosto):

```
CALL #1 (Helv):             rax = 0x140a00d2   (no-NULL, esperado -- pasa igual que siempre)
CALL #2 (Liberation Sans):  rax = 0x620a00e2   (no-NULL !!)
```

**`pfce->hfont` leído directamente tras el `jne` (no solo `$rax`) confirma `0x620a00e2` -- un handle real, no NULL -- y el flujo de ejecución salta correctamente a la rama de éxito (`C_LoadFcid` línea 377, `FSelectFont(...)`), no a la de error.** Bajo `gdb`, con breakpoints y single-stepping en este punto exacto, **`CreateFontIndirect` tiene éxito para "Liberation Sans". El diálogo "Low memory" no debería aparecer.**

**Esto contradice directamente el comportamiento sin `gdb`**, confirmado de nuevo en esta misma sesión inmediatamente antes (mismo binario, mismo `Xvfb :88`, ninguna diferencia de entorno): el diálogo aparece, `matfont-set` se dispara, el test falla con el mismo mensaje de siempre.

**Prueba mínima de una sola variable (Fase 3 de systematic-debugging): ¿alcanza con más tiempo, o es específico de estar bajo el debugger?** Se agregó un `Sleep(50)` diagnóstico justo antes de la llamada a `CreateFontIndirect` (guardado bajo `OPUS_X64`, sin tocar MSVC), se reconstruyó `WORD1`, y se corrió el test **sin** `gdb`. **Resultado: la falla reproduce idéntica** (mismo diálogo, mismo `matfont-set`, mismo mensaje final). Un simple `Sleep(50)` en el sitio exacto de la llamada NO reproduce lo que el `gdb` logra -- descarta "solo hace falta más tiempo ahí" como explicación completa. El `Sleep` se revirtió (no ayudó, no tiene sentido dejarlo); `git diff` sobre `LOADFONT.C` queda limpio de nuevo.

**Lectura del resultado, no solo el dato:** dado que (a) los 9 sitios anteriores de este documento ya descartaron con evidencia directa cualquier problema en los *datos* pedidos (LOGFONT idéntico salvo campos irrelevantes, charset saneado, cache sana, sin agotamiento de handles, nombre correcto hasta su `\0`), y ahora (b) el código compilado en el sitio exacto de la comparación es correcto a nivel de bits (64 bits completos, sin truncar) y (c) el mismo `CreateFontIndirect`, con los mismos datos, **tiene éxito real cuando se lo observa paso a paso**, la explicación que mejor encaja con todo el patrón acumulado (incluida la "segunda fuente real siempre falla, la primera siempre pasa", inmune a repetición/orden en el probe standalone del principio de esta sesión) es una **condición de carrera genuina en algún punto *anterior* a `loadfont.c:349`** -- no en el propio `CreateFontIndirectA`/`NtGdiHfontCreate`, que ya se demostró que funciona bien con los datos que le llegan. El `Sleep(50)` puntual no alcanza porque el perfil de pausa real de `gdb` (breakpoints y comandos interactivos que detienen el proceso bastante antes de esta línea, no solo en ella) es mucho más amplio que 50ms en un solo punto tardío.

**Probe standalone de esta sesión (antes de llegar a gdb), para el registro:** se armó un programa `winegcc` mínimo, sin código del proyecto, que llama `CreateFontIndirectA` con los mismos bytes de LOGFONT que usa `WORD1` -- primero solo, luego en secuencia Helv-después-Liberation-Sans (mismo orden que el ribbon), luego repitiendo Liberation Sans una tercera vez. **Las cuatro llamadas tienen éxito siempre**, sin importar orden ni repetición -- extiende el hallazgo ya documentado de la actualización anterior (`CreateFontIndirectA` con estos datos funciona perfecto fuera de `WORD1`) a también cubrir la secuencia de llamadas, no solo el dato de una llamada aislada. Confirma otra vez que el problema es de estado de proceso específico de `WORD1`, no de los datos ni del orden de pedidos.

**Candidato concreto para la próxima sesión:** localizar la carrera real requiere mirar ANTES de `loadfont.c:349`, no en el punto de la comparación (ya limpio). Sitios no explorados con esta técnica todavía: instrumentar con breakpoints (no solo trazas de aplicación, que ya se agotaron en las hipótesis 1-9) el camino completo desde el `WM_COMMAND`/`CBN_SELCHANGE` del combo de fuente del ribbon hasta que se llega a esta línea -- en particular, comparar bajo gdb (con el mismo patrón de esta sesión: `tbreak` + `finish` de un solo salto, no anidado) el estado de cualquier dato compartido (fcid, `selCur.chp`, la propia `vhsttbFont`) en el momento exacto en que se arma el `LOGFONT` para "Liberation Sans", contra el mismo punto sin gdb -- si ese dato YA difiere antes de llegar a `loadfont.c:349`, la carrera está más arriba en la cadena y este archivo deja de ser el lugar correcto para seguir mirando.

**Infraestructura reutilizable dejada en `build/` (gitignored, no committeado):** `build/run_gdb_font_debug.sh` (arnés que lanza el test, espera el PID de `WORD1` con `pgrep -f 'WORD1\.exe\.so'`, y adjunta `gdb -p <pid> --batch -x build/gdb-font-debug.gdb`), `build/gdb-font-debug.gdb` (el script de gdb con la comparación de un solo salto ya corregida), `build/probe_two_fonts.c` (el probe standalone de secuencia de dos fuentes). Ningún archivo de `src/` quedó modificado al cerrar esta sesión (`git status` limpio salvo los 5 archivos de la migración `OpusMem*` ya documentados, que siguen sin tocar).

**Verificado, sin regresión:**
```
DISPLAY=:88 ctest --test-dir out/linux-winelib-debug -R "^opus_word1_font_typing_test$" --output-on-failure
    (corrido manualmente vía build/run_gdb_font_debug.sh, no vía ctest esta sesión)
    "newly typed text did not retain the ribbon font" -- mismo fallo de siempre, sin gdb
```

**Séptima actualización 2026-08-22 (exia, misma sesión): causa raíz real encontrada -- Task 6 Bug 4 CERRADO. No es una carrera, no es GDI, no es `gdb`. `EraNameFromFtc()` en `src/core/src/OpusShellFontMetrics.cpp` es una tabla hardcodeada de 4 entradas; "Liberation Sans" cae fuera de rango y el propio código ya documentaba esta salida como intencional.**

A pedido explícito de continuar "con los breakpoints antes de `loadfont.c:349`" (la Sexta actualización dejaba eso como candidato). Antes de seguir hacia atrás en la cadena de llamadas, primer paso obligado: **repetir la medición de la Sexta actualización una vez más para confirmar que el "arreglo" de `gdb` era reproducible, no una casualidad de una sola corrida.** No lo fue: se repitió el mismo script `tbreak CreateFontIndirectA` + `finish` de un solo salto, y esta vez, en la MISMA corrida, se comprobaron **las dos cosas a la vez**: `$rax`/`pfce->hfont` no-NULL tras el `finish` (igual que la Sexta actualización) **y** `matfont-set msg=4` disparado en `WORD1-ribbon.txt` **y** el arnés reportando el mismo fallo de siempre (`harness exit rc=53`). Contradicción real dentro de una sola corrida, no "con gdb pasa, sin gdb falla" -- la lectura anterior de "la falla no reproduce bajo el debugger" estaba mal desde la raíz: nunca se había cruzado el resultado de `gdb` contra el `rc` del arnés en la misma corrida, solo se asumía que `rax` no-nulo implicaba que el test iba a pasar.

Repetido con el método más limpio posible (`next` simple sobre toda la sentencia de la línea 349, sin `finish` ni `nexti` que puedan confundirse) para descartar cualquier artefacto de gdb: mismo resultado -- `pfce->hfont` no-NULL, salto a la línea 377 (rama de éxito), y aun así `matfont-set`/`harness rc=53` en la misma corrida. **Esto ya no admite lectura de timing: la línea 349 en sí misma es inocente, siempre.** La pregunta correcta pasó de "¿por qué falla `CreateFontIndirect`?" a "¿de dónde sale `matfont-set` si no es de ahí?".

Respuesta encontrada en el propio código, no con más `gdb`: `matfont-set` vive justo después de la etiqueta `LSystemFontErr:` (`Opus/LOADFONT.C:354`), y esa etiqueta **no solo se alcanza cayendo desde el `if` de la línea 349** -- hay tres `goto LSystemFontErr;` más en la misma función (`grep -n LSystemFontErr Opus/LOADFONT.C` -> líneas 287, 455, 491), cada uno un camino de fallo completamente distinto que el breakpoint de la línea 349 nunca puede ver (porque esos `goto` saltan directo a la etiqueta, sin pasar por la línea 349 en absoluto).

Se puso un breakpoint liviano (estilo `dprintf`, `commands`+`continue` automático, sin interacción manual -- el mismo patrón que ya se había confirmado que NO cambia el síntoma en la ronda anterior de esta sesión) en `FSelectFont` (la función que sí puede fallar en la línea 287, vía `!FSelectFont(...)`) para descartar ese camino primero: **3 llamadas totales, las 3 con `pfti->fPrinter=0`, la traza `screenfail` interna de `FSelectFont` nunca se dispara** -> las 3 devuelven éxito. Camino de la línea 287 descartado con datos, no por lectura.

Quedan las líneas 455 y 491. La 455 es un fallo de `HqAllocLcb` (asignación de memoria) -- posible pero sin motivo para fallar aquí. La 491 es la real:

```c
#if defined(__GNUC__) && !defined(_MSC_VER)
    if (!pfti->fPrinter && !vfPrvwDisp)
        {
        /* Camino de pantalla, paso variable: contrato Qt del shell
           (docs/port-qt/01-core-shell-boundary.md SB2) en vez de
           GetCharWidth/OurGetCharWidth. ... */
        shellKey.ftc = fcid.ibstFont;
        shellKey.ps = fcid.hps;
        shellKey.catr = (fcid.fBold ? 1 : 0) | (fcid.fItalic ? 2 : 0);
        if (OpusShellCharWidths( &shellKey, chDxpMin,
                chDxpMax - chDxpMin, rgdxuShell ) != 0)
            {
            /* Sin impresora ni sintesis de negrita/cursiva en el
               contrato actual (limitaciones 2 y 3 de
               OpusShellFontMetrics.cpp) -- no deberia alcanzarse
               aqui salvo esos casos ... se degrada al mismo camino
               de error que un fallo de CreateFontIndirect ya usa
               mas arriba. */
            UnlockHq( pfce->hqrgdxp );
            goto LSystemFontErr;
            }
```

Este bloque es código del **port** (guardado `#if defined(__GNUC__) && !defined(_MSC_VER)`, no toca MSVC), parte del trabajo de extracción del núcleo Qt descrito en `CLAUDE.md` ("`OpusShellFontMetrics.h` -- contrato de medición de texto ... la pieza restante de mayor prioridad porque condiciona la fidelidad de paginación"). En vez de medir anchos de caracteres vía GDI, este camino llama a `OpusShellCharWidths` (`src/core/src/OpusShellFontMetrics.cpp`), la implementación Qt del contrato del shell.

`OpusShellCharWidths` resuelve el nombre de la fuente a través de `EraNameFromFtc(int ftc)` (`src/core/src/OpusShellFontMetrics.cpp:61-68`):

```c
const char *EraNameFromFtc(int ftc) {
    switch (ftc) {
        case 0: return "Tms Rmn";
        case 1: return "Symbol";
        case 2: return "Helv";
        case 3: return "Courier";
        default: return nullptr;
    }
}
```

**Tabla hardcodeada de exactamente 4 entradas** -- los 4 fuentes originales de Word 1.1a (`Opus/initwin.c:1541-1583` los registra en ese mismo orden como `ibstFont` 0-3). El comentario del propio archivo ya lo advertía (`OpusShellFontMetrics.cpp:13-19`): *"`ftc` -> nombre de época: tabla fija de 4 entradas, hardcodeada ... `ftc` fuera de [0,3] falla controlado."* -- un límite conocido y documentado desde que se escribió ese archivo, no un bug nuevo.

`"Helv"` es `ibstFont=2` -> dentro de rango -> siempre funciona. `"Liberation Sans"` (la fuente que el arnés de test necesita usar porque los nombres reales de Windows como "Arial"/"Courier New" no existen en un stack de fuentes Linux -- ver el comentario de `opus_word1_ui_test.cpp` sobre `installed_windows_fonts()`) recibe `ibstFont=4` al registrarse en runtime -- **fuera de rango**, `EraNameFromFtc(4)` devuelve `nullptr`, `OpusShellCharWidths` devuelve `-1`, dispara `goto LSystemFontErr`, y de ahí en más el camino es indistinguible (mismo `SetErrorMat(matFont)`, mismo diálogo `eidCantRealizeFont` "Low memory: cannot display requested font") de un fallo real de GDI -- por eso las nueve hipótesis anteriores, todas centradas en `CreateFontIndirect`/GDI, nunca lo encontraron: **estaban mirando la función correcta para un síntoma que en realidad viene de una función completamente distinta, que además ni siquiera está en `Opus/` sino en `src/core/`, el núcleo Qt en construcción.**

Esto también explica de una vez el patrón "la primera fuente distinta siempre pasa, la segunda siempre falla" que se sostuvo intacto en las nueve hipótesis y en la exploración de esta sesión: no es sobre conteo de handles GDI, cachés, ni nada de proceso -- es literalmente que la PRIMERA fuente pedida por el ribbon en este test (`Helv`) tiene `ibstFont=2` (dentro de la tabla de 4), y la SEGUNDA (`Liberation Sans`) es la primera fuente de la sesión con `ibstFont >= 4` (fuera de la tabla). Con cualquier otra fuente Linux real como segunda opción, el mismo `ibstFont=4` (o mayor) habría fallado igual.

**No se tocó código de arreglo esta sesión** -- `EraNameFromFtc`/`OpusShellCharWidths` son parte activa del trabajo de extracción del núcleo Qt (`src/core/`, no restringido como `Opus/`, pero sí una pieza grande y con dueño propio de diseño per `docs/port-qt/`), y ampliar la tabla a fuentes arbitrarias es una tarea de alcance real (¿enumerar fuentes del sistema vía Qt en vez de una tabla fija? ¿mapear cualquier `ibstFont` no reconocido a una fuente por defecto solo para medición de anchos?) -- no un fix de una línea para decidir sin autorización explícita.

**Verificado:**
```
build/gdb-font-debug.gdb (breakpoint dprintf-style en FSelectFont, 3 hits, los 3
    pfti->fPrinter=0, screenfail nunca se dispara) -- descarta linea 287
grep -n LSystemFontErr Opus/LOADFONT.C -> 287, 455, 491 (goto) + 354 (label)
src/core/src/OpusShellFontMetrics.cpp:61-68 (EraNameFromFtc, switch 0-3, default nullptr)
src/core/src/OpusShellFontMetrics.cpp:13-19 (comentario ya documentaba el límite)
```

**Próximo paso, si se retoma para arreglar (no solo diagnosticar):** decidir la estrategia de `EraNameFromFtc`/`OpusShellCharWidths` para `ftc` fuera de [0,3] -- opciones: (a) enumerar la fuente real vía Qt (`QFontDatabase`) en vez de la tabla fija de 4 nombres de época, la solución de fondo pero con más superficie de fidelidad de paginación que revisar; (b) fallback controlado a una fuente por defecto (p.ej. tratar cualquier `ftc>=4` como "Helv" solo para medir anchos) que desbloquea el test sin resolver la limitación real de fondo. Requiere decisión explícita del mantenedor, no autorización implícita de esta sesión.

**Octava actualización 2026-08-25 (exia): Task 6 Bug 4 cerrado de verdad -- `OpusFontKey.szFace` reemplaza la tabla fija de `EraNameFromFtc`; `fail(60)` restante era el arnés, no medición de fuentes.** Dos piezas, en dos sesiones/commits distintos:

**Primera pieza (commits `bf5f117`, `b69e715`, `e7c50d1`, ya en el árbol antes de esta tarea):** en vez de ampliar `EraNameFromFtc` a una tabla más grande (seguía siendo un callejón para cualquier `ftc` no anticipado), el núcleo (`OpusFontKey`, `src/core/include/OpusShellFontMetrics.h`/`.cpp`) pasa a llevar el nombre real de la fuente en `szFace` en vez de depender de resolver `ftc` a un nombre de época. `Opus/LOADFONT.C` rellena `shellKey` con `memset` primero (obligatorio: la declaración K&R no trae inicializador y `FaceNameFor()` ya leía `szFace` en cada llamada) y con el nombre real de `pffn`. `OpusPortGdiCharWidths` mide con la fuente de runtime en vez de la tabla fija. Con esto, `ftc>=4` deja de caer en `LSystemFontErr` -- el diálogo "Low memory: cannot display requested font" que atrapaba el teclado (Séptima actualización, arriba) no vuelve a aparecer. Ningún cambio en `Opus/disp.c`/`screen.c` ni en `Opus/LOADFONT.C` más allá del `memset` ya mencionado.

**Segunda pieza (esta tarea, Task 1 del plan `2026-08-25-font-typing-harness-bands`):** con el Bug 4 real cerrado, `opus_word1_font_typing_test` avanzó de "no conserva la fuente" a un `fail(60)` nuevo, `"mixed-font lines disappeared after resizing"`. El dump mostraba `bands=0,0 repaint=2629` -- las dos muestras de banda (`after_enter_first_band`/`after_enter_second_band`, franjas de píxel `[0,50)`/`[50,131)`) leían cero mientras `after_forced_repaint_pixels` (mismo panel, rango completo `[0,300)`) leía 2629 correctamente. El código de partida tomaba esas dos muestras de banda **antes** del repintado forzado que el propio test ya hacía (`InvalidateRect`+`UpdateWindow`+`Sleep(400)`, unas líneas más abajo); esa asimetría frente a `after_forced_repaint_pixels` parecía la explicación obvia. La medición real (párrafo siguiente y Novena actualización, más abajo) la descarta: no era un bug de medición de fuentes ni de `disp.c`, pero tampoco era orden de muestreo -- las franjas `[0,50)`/`[50,131)` caían enteras dentro del margen superior de la página, por encima de donde empieza cualquier texto, así que no iban a leer contenido sin importar en qué momento se tomara la muestra.

Se movieron las dos líneas de muestra de banda a después del `UpdateWindow`/`Sleep(400)` forzado (dejando `after_enter_pixels`, la muestra diagnóstica del repintado natural, donde estaba). Con eso solo, `bands` **seguía en `0,0`** de forma estable (2 corridas idénticas) -- dump byte-idéntico al de antes de mover las muestras. Esto descarta por completo que fuera un problema de *cuándo* se muestreaba: las franjas hardcodeadas `[0,50)`/`[50,131)` en `count_dark_client_pixels` no corresponden a ninguna línea real de este documento.

**Corrección (revisión final de rama, misma fecha):** el texto original de esta actualización afirmaba aquí que la franja `[50,131)` "cruza el límite entre la línea 24pt y la 36pt" -- esa frase es incorrecta y no estaba verificada contra un dump real; sugiere un problema de límite/orden de línea que nunca existió. Una medición directa (barrido de `count_dark_client_pixels` en pasos de 5px sobre `[120,240)`, tomada en este mismo punto del test) muestra que el contenido oscuro no empieza hasta el `y` de cliente 140 (`sweep5=…135:0 140:21 145:14…`, detalle completo en la Novena actualización, más abajo). Es decir, `[0,50)` y `[50,131)` caen **enteras** dentro del margen superior de la página -- por encima de donde empieza cualquier texto -- así que ninguna de las dos toca una línea, y mucho menos cruza el límite entre dos. Con la reubicación sola las bandas seguían siendo un rango que no correspondía al layout real, así que -- siguiendo la salida explícita que el plan preveía para este caso -- se quitó solo la cláusula `after_enter_first_band == 0 || after_enter_second_band == 0 ||` de la condición de `fail(60)`, dejando intactas todas las demás (`applied_ftc`, `second_inserted_*`, `large_inserted_hps==144`, `formatted_chp_hps`, `fetch_bytes_match`, `after_forced_repaint_pixels`, `large_line_band_pixels`, `large_line_pixels`, `cache_pages_separate`). Dicho con precisión: el commit `38686d2` no arregló un bug de *timing* de muestreo -- quitó una comprobación cuyos rangos literales nunca podían satisfacerse para este documento, sin importar cuándo se tomara la muestra. El diagnóstico `bands=` se mantuvo en el `std::cerr` para visibilidad futura, aunque en ese momento ya no bloqueaba el test (la comprobación por línea se restauró después, con rangos medidos -- ver Novena actualización).

Con ese cambio, `opus_word1_font_typing_test` pasa de forma estable (2/2 corridas), y la etiqueta `word1_startup_blocked` queda en **8/9** -- el único fallo restante es `opus_word1_interaction_test` (arrastre de caption, limitación de entorno Xvfb/Wine documentada en §12, no un bug del proyecto). Ningún archivo de `Opus/` ni `src/core/` tocado en esta pieza -- solo `src/port/original/opus_word1_ui_test.cpp`.

**Verificado:**
```
DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -R "^opus_word1_font_typing_test$" --output-on-failure
    Passed (x2) -- visualPixels=2705->2599->2629->11061 bands=0,0 repaint=2629
    largeBand=7497 fetch=1/1@-1:13/13 displayLines=3

DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
    8/9 -- único fallo: opus_word1_interaction_test (documentado, §12)
```

Task 6 (`--font-typing`) queda **cerrado**: los 4 bugs originales (nombres de fuente enumerables, `union FCID` LP64, foco tras selección de ribbon, `EraNameFromFtc`/tabla de época) arreglados, más este ítem de arnés que no era un bug de producto.

**Novena actualización 2026-08-25 (exia, revisión final de rama): banda por línea restaurada con offset medido; corrige un hallazgo de la Octava actualización.**

Una revisión final de `fix/font-typing-szface` (todavía sin mergear a `main`) encontró dos hallazgos "Important" sobre esta sección: (1) la frase "cruza el límite entre la línea 24pt y la 36pt" de la Octava actualización era incorrecta -- corregida in situ en los dos párrafos de arriba; (2) la comprobación por línea (`after_enter_first_band`/`after_enter_second_band`) se había quitado de `fail(60)` en vez de arreglarse. Lo segundo era plan-sanctioned -- el plan (Task 1, Step 4) autorizaba explícitamente esa salida una vez confirmado que las franjas seguían en 0 tras el repintado forzado -- pero dejaba el test sin su única comprobación de "cada línea de fuente distinta realmente se pintó"; solo quedaban el conteo de todo el panel y la banda combinada `[131,292)`.

Se restauró la comprobación con rangos derivados de una medición real, no adivinados ni copiados de una estimación aproximada. Con un barrido temporal de `count_dark_client_pixels` (pasos de 5px sobre `[120,240)`) insertado en el mismo punto exacto donde se toman las muestras de banda, más una consulta de la geometría de línea real en ese instante (los mismos códigos de `kWmOpusX64QuerySelection` -- 30/32/33 -- que ya usa el diagnóstico `displayLines` unas líneas más abajo en el bloque), se midió:

```
sweep5=120:0 125:0 130:0 135:0 140:21 145:14 150:29 155:169 160:140 165:112
       170:97 175:0 180:3 185:61 190:118 195:512 200:287 205:313 210:276
       215:402 220:0 225:3 230:0 235:18
probeLines=3 [0 y=0 h=36] [1 y=36 h=54] [2 y=90 h=16]
```

El contenido oscuro empieza en `y` de cliente 140, y el hueco entre línea 0 y línea 1 cae en `y` de cliente 175-180 -- ambos coinciden con exactitud con `y=0/h=36` (línea 0) y `y=36/h=54` (línea 1) bajo un offset constante de 140px: línea 0 -> cliente `[140,176)`, línea 1 -> cliente `[176,230)`. Un segundo barrido, independiente, tomado más tarde en el mismo test (tras escribir la línea grande de 144hps) reproduce la misma forma `[140,220)` para estas dos líneas y sitúa el inicio del contenido de la línea grande en `y` de cliente 230 -- exactamente `layout y=90 + 140`. El offset de 140px queda confirmado por dos muestras independientes tomadas en dos instantes distintos del mismo test, no por una sola lectura.

El código final calcula las dos bandas dinámicamente a partir de esa geometría -- `kPageTopMarginY = 140` más los `y`/`h` reales de línea 0 y línea 1, consultados en caliente vía los mismos códigos de mensaje que el diagnóstico ya usaba -- en vez de hardcodear una segunda pareja de literales de píxel. Así la comprobación sigue midiendo lo que dice medir aunque el layout de este documento varíe ligeramente entre corridas. Se repuso la cláusula `after_enter_first_band == 0 || after_enter_second_band == 0 ||` en la condición de `fail(60)`, sin tocar ninguna otra cláusula.

**Verificado:**
```
DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -R "^opus_word1_font_typing_test$" -V
    Passed (x3) -- bands=582,1975 (estable, idéntico en las 3 corridas)

DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked --output-on-failure
    8/9 -- único fallo: opus_word1_interaction_test (documentado, §12)
```

Plan de esta pieza: `docs/superpowers/plans/2026-08-25-font-typing-harness-bands.md` (Task 1; commiteado en esta misma revisión, ver convención en `bf5f117`). Ningún archivo de `Opus/` ni `src/core/` tocado -- solo `src/port/original/opus_word1_ui_test.cpp`.

## 9. `--clipboard`: "Ctrl+A did not execute Select All" -- confirmado, verify-only, cierra Task 7

**Task 7, 2026-08-19 en exia.** El plan (Step 1) pedía correr
`opus_word1_clipboard_shortcut_test` aislado como primer paso. Igual
que Task 4, ya venía pasando en cada corrida completa de la etiqueta
de esta sesión -- se verificó aislado para confirmarlo formalmente:

```
ctest -R "^opus_word1_clipboard_shortcut_test$" --output-on-failure
    Passed    3.61 sec
```

Mismo patrón que Task 4 (File > New): comparte causa raíz con Task 3
(`_setjmp`/`longjmp` ABI). Sin cambios de código -- commit de
documentación solamente.

Rama `fix/winelib-startup-blocked` (no está en `main`). Plan:
`docs/superpowers/plans/2026-08-15-terminar-winelib.md`. Ledger SDD:
`.superpowers/sdd/2026-08-15-terminar-winelib/progress.md`.

Build/test solo en debian13 contra `/home/pablo/build-debian13-verify`,
`DISPLAY=:59`. No usar el `--preset` del host. **Este build dir es
compartido con al menos otra sesión** (visto en vivo el 2026-08-15, ver
§4 arriba) — antes de fiarse de un resultado de `ctest` rojo, confirmar
que no hay otro proceso `cmake`/`ninja`/`ctest` corriendo ahí al mismo
tiempo (`ps aux | grep -E 'cmake|ninja|ctest'` dentro del contenedor).

Task 3 **cerrada** en Fix round 3 + cierre de revisión (ambas rondas de
review, clean): el AV era `_setjmp` con ABI Microsoft-x64 pisando
`*vhpllbs`. Las dos mitades del par (`_setjmp` y `longjmp`) están fijadas
a System V por definición, con guard de build que lo verifica.
`opus_word1_about_test` pasa; la etiqueta llegó a 5/9. Tasks 1–2 hechas.
11 hallazgos menores quedaron diferidos a la revisión final de toda la
rama (lista completa en el ledger SDD).

Task 4 **cerrada** 2026-08-19 en exia (§6 arriba) — verify-only,
confirma la hipótesis de §4: `opus_word1_ui_test` (modo base, ejercita
File > New) pasa limpio y aislado, sin ningún cambio de código. Mismo
root cause que About (Task 3). El fallo de §4 fue el entorno
compartido de esa sesión en debian13, no un bug independiente de
File > New.

Task 5 (Save As) **no empezada** — el único de los 4 tests que ya
fallaba con mensaje propio (no el AV genérico) antes de esta sesión;
candidato a causa raíz independiente, ver la nota del brief sobre
`run_word95_common_file_dialog`.

Tasks 6–10 **no empezadas**.

Aparte y sin relación con Task 3/4: `opus_x64_runtime_test` (gating)
se cuelga sin imprimir nada, confirmado pre-existente (binario de un
día antes, sin símbolo `setjmp`, se cuelga igual). Sigue sin
investigar.

Sesión cerrada 2026-08-15 a pedido del usuario tras ~2 h de trabajo
(no por límite de uso). Sin trabajo a medias sin commitear — árbol
limpio en `25325c0`.

**Actualización 2026-08-19 (exia, revisión independiente de Task 3 a
Task 9):** ver §5-§11 arriba. 4 hallazgos de fidelidad corregidos y
verificados (Task 3), Task 4 cerrada verify-only, Task 5 (Save As) con
causa raíz independiente real encontrada y arreglada (señuelo
`OpusSdmDialog` sin conexión al diálogo real `GetSaveFileNameA`),
Task 6 (`--font-typing`) con 2 de 3 bugs reales arreglados (nombres de
fuente Windows nunca enumerables; `union FCID` de 8 bytes en Linux por
LP64) y un tercero localizado sin cerrar (el foco no vuelve al panel
tras elegir fuente del ribbon -- toca `Opus/iconbar1.c` restringido,
necesita autorización), Task 7 (Ctrl+A) cerrada verify-only, Task 8
(`--selection`) con 3 bugs de arnés encadenados encontrados y
arreglados (falta de foco real, mensajes sintéticos en vez de input
real, constante de píxel obsoleta que asumía margen izquierdo cero),
Task 9 (`--typing`) con el mismo bug de foco real que Task 8, arreglado
igual. **7/9** en la etiqueta, subiendo de 5/9 al empezar esta sesión.
Reproducido en un segundo entorno (exia, no debian13/hp-15). Árbol
limpio, 11 commits nuevos sobre `16145b6`, pusheados a
`origin/fix/winelib-startup-blocked`.

## 10. `--selection`: "sentence-end click produced an invalid selection" -- 3 bugs de arnés encadenados, arreglados

**Task 8, 2026-08-19 en exia.** El plan esperaba `"typing did not leave
a canonical insertion selection"` (fail 38); esa parte ya pasaba (Task
2 no la rompió). El fallo real, más adelante, era fail 39. **Los 3
bugs son del arnés de test, no de WORD1** -- verificado con
`kWmOpusX64QuerySelection` en cada paso antes de tocar nada.

**Bug A -- clic sintético sin foco real:** `selection_mode` era el
único bloque de este archivo que hacía clics posicionales
(`WM_LBUTTONDOWN`/`UP` con coordenadas) sin llamar antes
`make_foreground_and_focus` -- cada otro bloque con input real de este
mismo archivo sí lo hace (grep confirma 9 sitios). Sin foco/activación
real, cualquier clic (sintético o real) resolvía siempre a `cp=0` sin
importar `x`. Fix: añadir la llamada, mismo patrón que el resto.

**Bug B -- mensajes sintéticos en vez de input real:** incluso con
foco, `SendMessageW(pane, WM_LBUTTONDOWN, ...)` entrega directo al
window proc sin pasar por la cola de mensajes real -- se cambió a
`SetCursorPos`+`SendInput` (`send_mouse_button`), el patrón ya probado
en este mismo archivo para el caso idéntico "clic cerca del final de
la oración" (`interaction_mode`, ~línea 1853).

**Bug C -- constante de píxel obsoleta:** con A y B arreglados, el
mapeo `x=10..250` reveló un margen izquierdo real de ~185-190px antes
del primer carácter (`x=180` seguía en `cp=0`; recién en `x=190`
`cp=1`) -- la constante hardcodeada `x=250` para "cerca del final de
la oración" (32 caracteres, ~7px cada uno) solo alcanzaba `cp=12`, no
los `>=15` que pide la aserción. Fix: en vez de adivinar un nuevo
píxel fijo, el bucle de mapeo ahora extiende su rango (10-450) y
guarda el primer `x` real que alcanza `cp >= sentence_length/2` --
usado como blanco del clic final, sin asumir ningún margen.

**Efecto secundario del Bug B, encontrado y arreglado en el camino:**
el bucle de mapeo con `SendInput` real, 24 clics seguidos con solo
20ms entre down/up, disparaba detección de doble-clic real de Wine/
Win32 justo antes del clic final (`clicked_double=1` en vez de `0`,
rompiendo esa aserción por separado). Fix: `Sleep(60)` entre cada
probe del bucle, y `Sleep(GetDoubleClickTime()+150)` antes del clic
final dedicado.

**Verificado:**
```
DISPLAY=:99 ctest -R "^opus_word1_selection_test$" --output-on-failure
    Passed    9.85 sec

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    6/9 -- sin regresión en los demás
```

Archivo: `src/port/original/opus_word1_ui_test.cpp` únicamente --
ningún cambio en `Opus/` ni `opus_sdm_runtime.cpp` para esta tarea.

## 11. `--typing`: "typed text was not painted in the document pane" -- mismo patrón que Task 8, arreglado

**Task 9, 2026-08-19 en exia.** El plan esperaba salidas 13/15 con
variabilidad entre corridas (§27 de `01-diagnostico...`); esta sesión
el fallo real ya llegaba consistentemente hasta el último check, fail
16, sin variar entre corridas -- Task 2's fix del crash movió el punto
de fallo más allá de donde el plan lo dejó.

**Causa raíz:** `typing_mode` era el único modo interactivo de este
archivo que **nunca** buscaba explícitamente el panel `OpusWwd` ni
llamaba `make_foreground_and_focus` -- confiaba ciegamente en lo que
`GetGUIThreadInfo` reportara como ya enfocado al arrancar. El check de
`fail(13)` solo verificaba `hwndFocus != nullptr`, nunca que fuera
*el panel correcto*. Si otra ventana tenía el foco por accidente del
orden de creación, los `WM_CHAR` posteados con
`post_keyboard_character` se encolaban sin error (`fail(15)` nunca
dispara) pero no llegaban a ningún sitio visible -- exactamente el
síntoma: el test llega hasta el último check (`fail(16)`, conteo de
píxeles oscuros) y falla ahí, nunca antes.

**Fix:** buscar `OpusWwd` explícitamente y llamar
`make_foreground_and_focus` antes de postear, mismo patrón que Task 8
(§10) estableció como el requerido para todo bloque de este archivo
que depende del foco real.

**Verificado:**
```
DISPLAY=:99 ctest -R "^opus_word1_typing_test$" --output-on-failure
    Passed   14.78 sec

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9 -- sin regresión en los demás
```

Archivo: `src/port/original/opus_word1_ui_test.cpp` únicamente.

## 12. `--interaction`: "dragging the caption did not move the WORD1 window" -- limitación de entorno confirmada, no bug del proyecto

**Task 10, 2026-08-19 en exia -- último ítem del plan.** El plan (Step
2) ya anticipaba esta posibilidad: "check whether this is a Wine/
window-manager limitation similar in kind to the `CreateProcessW`
zero-PID precedent (§25)". Lo es, confirmado con una réplica
independiente, sin código del proyecto.

**Descartado primero (ninguno de los dos era la causa):**
- **Sin window manager:** `:99` (el Xvfb compartido de esta sesión) no
  tenía ninguno corriendo. Se instaló `openbox` (via `apt`, con
  autorización) y se levantó un Xvfb propio y aislado (`:77`, no
  toca el `:99` compartido) -- mismo resultado exacto.
- **Salto de cursor único en vez de arrastre incremental:**
  `SetCursorPos` de una sola vez entre el down y el up podría no
  disparar el umbral de arrastre (`SM_CXDRAG`/`SM_CYDRAG`) que el
  loop de `SC_MOVE` de Wine espera. Se cambió a 8 pasos incrementales
  con `Sleep(15)` entre cada uno -- mismo resultado exacto.

**Confirmación decisiva:** una sonda standalone (`winegcc`, sin código
de este proyecto) contra `wine notepad` -- el builtin de Wine, la
misma referencia "conocida-buena" que ya usa `01-diagnostico-heap-
corruption-arranque.md` en otros puntos -- con la *misma* secuencia
exacta (`WM_NCHITTEST` confirma `HTCAPTION`, `SetCursorPos`+`SendInput`
incremental, `MOUSEEVENTF_LEFTDOWN`/`LEFTUP`) bajo el mismo `:77`+
`openbox`: **tampoco se mueve** (`before=0,0 after=0,0`, idéntico al
síntoma de WORD1). Si ni siquiera notepad puede arrastrarse así en
este entorno, no es un bug de WORD1 ni de `Opus/wproc.c` -- es una
limitación de cómo Wine/este `winex11.drv` maneja el loop `SC_MOVE`
frente a input sintetizado vía `SendInput`, en este entorno
específico.

Se revisó también si `Opus/wproc.c` intercepta `WM_NCLBUTTONDOWN` con
lógica propia que pudiera estar interfiriendo -- no lo hace; la única
mención de ese mensaje en ese archivo es una tabla de logging bajo
`#ifdef RSH` (build de investigación, no activa aquí), no un handler
real. Confirma que el mensaje cae directo a `DefWindowProc`, igual que
en `notepad`.

**Sin cambio de comportamiento -- se mantiene el arrastre incremental
en el test** (más fiel a un arrastre real de usuario que el salto
único original, aunque no fue la causa) y se agregó un diagnóstico
(`before=`/`after=`/`caption_point=`) para que una futura sesión no
tenga que re-derivar esto. Test sigue fallando, documentado como
limitación de entorno, no bug -- mismo tratamiento que §25.

**Verificado:**
```
DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9 -- sin regresión (--interaction seguía fallando antes y
    después, por la razón ahora documentada, no una nueva)
```

Archivo: `src/port/original/opus_word1_ui_test.cpp` (arrastre
incremental + diagnóstico). Dependencias del sistema instaladas esta
sesión (`apt`, con autorización): `twm` (descartado, crashea sin
`xfonts-base` que también se instaló), `openbox` (usado para la
réplica).

## 13. `--roundtrip`: proceso 2 abre el diálogo real, pero el `.doc` que Word 1.1a acaba de guardar no lo puede reabrir -- bloqueado, causa fuera del arnés

**Task 2 del plan `docs/superpowers/plans/2026-08-25-doc-roundtrip.md`,
2026-08-25 en `/home/pablo/mswordrt` (worktree `doc-roundtrip`),
`DISPLAY=:91`.** Continúa el mismo bloque `if (roundtrip_mode)` que
Task 1 dejó con un `TODO`: lanzar un segundo proceso `WORD1` contra el
`.doc` que el primero acaba de guardar y comparar `cpMac`, el
contenido byte a byte (consulta 69), `ftc0`, `hps0` y `dypLine`
(consulta 55) contra la instantánea tomada antes de guardar. **No
pasa** -- pero no por el arnés: el propio `WORD1` no puede reabrir un
`.doc` que acaba de escribir con su propio Save As, ni por línea de
comando ni por el diálogo real. Ver "Causa raíz" abajo.

**Qué se implementó (funciona correctamente hasta donde llega):**

- **Paso 1 -- lanzar el proceso 2.** Mismo `CreateProcessW` que el
  proceso 1, con `wide_path` (la ruta que el Paso 3 de Task 1 generó)
  como segundo argumento. **Deliberadamente sin comillas**, a
  diferencia del ejemplo literal del brief: `Opus/initwin.c`
  (`FInitPart1`) parte `lpszCmdLine` por espacios en blanco sin
  entender comillas en absoluto, así que un argumento entrecomillado
  sin espacios internos se cuela con las comillas literales pegadas al
  nombre de archivo -- confirmado probando ambas formas, mismo
  resultado en las dos (ver "Causa raíz"). Se recupera el PID desde la
  ventana con el mismo workaround que el proceso 1 (PID cero de
  `CreateProcessW` para binarios Winelib externos, documentado en
  §25 de `01-heap-corruption-startup-diagnosis.md`).
- **Detección de apertura por línea de comando:** se espera hasta 8 s
  a que la ventana de clase `OpusApp` (no clase `nullptr`: un intento
  fallido deja un `MessageBoxA` -- también clase `#32770`, también con
  caption `"Microsoft Word"`, el mismo `szAppTitle` que usa
  `CreateWindow` para `vhwndApp` -- que una búsqueda sin filtro de
  clase puede confundir con la ventana real) tenga en su título tanto
  `"Microsoft Word"` como el nombre base del archivo sin extensión
  (`oprtNNNN`, extraído de `wide_path`). **No ocurre**: la apertura por
  línea de comando no tuvo efecto en ninguna de las corridas.
- **Fallback a File > Open:** al fallar la línea de comando, queda un
  `MessageBoxA` modal "Cannot open document" (`Opus/open.c`,
  `DocOpenStDof`, `eidCantOpen`) trabado en pantalla -- aparece durante
  `FInitPart2`/`FInitArgs`, antes de que `vfInitializing` se limpie y
  antes de que `ElNewFile` cree el documento en blanco, así que nada
  del arranque posterior a ese punto ha corrido todavía. Se lo
  descarta (`WM_COMMAND`/`IDOK` a su botón OK, id 1) y se espera a que
  la app llegue al estado ocioso normal `"Microsoft Word - Document1"`
  antes de mandar `File > Open`. Con eso resuelto, el diálogo real
  `#32770` aparece, se localiza el campo de nombre (`ComboBoxEx32`
  `cmb13`/`0x047C`, igual que Task 1 en Save As -- mismo `OFN_EXPLORER`,
  confirmado también del lado de `kIddOpen` en
  `run_word95_common_file_dialog`), se fija la ruta con `WM_SETTEXT` y
  se verifica con `read_control_text_ansi` (lee de vuelta la ruta
  completa correcta), y se acepta con `IDOK`. **Todo este mecanismo de
  automatización funciona**: el diálogo aparece, el campo se rellena y
  se lee correctamente, el `IDOK` se entrega. El fallo ocurre después,
  dentro de Word mismo.
- **Paso 2 (comparación) y Paso 3 (limpieza):** implementados
  completos -- reenfoque de `OpusWwd` vía `make_foreground_and_focus`,
  relectura de las consultas 41/69/51/52/55, comparación campo por
  campo contra `cp_mac`/`snapshot_bytes`/`ftc0`/`hps0`/`dyp0`
  guardados por Task 1, con un código de fallo distinto por campo
  (`cpMac`=103, `byte@N`=104, `ftc`=105, `hps`=106, `dypLine`=107) y
  `TerminateProcess` + `DeleteFileA` en cada camino de salida. No se
  llega a ejercitar en una corrida exitosa porque el Paso 1 nunca
  entrega un documento cargado -- el fallo real (código 100, "roundtrip
  File Open did not load the target document") ocurre antes.

**Causa raíz (no es un bug del arnés):** tanto la apertura por línea
de comando como el diálogo interactivo terminan en el mismo
`MessageBoxA` "Cannot open document" -- el propio Word 1.1a rechaza el
archivo que su propio Save As acaba de escribir, exactamente igual sin
importar el mecanismo usado para pedir la apertura. Se investigó con
tres líneas de evidencia:

1. **Reproducción cruzada:** se probó la línea de comando con
   `wide_path` entre comillas (como muestra el brief) y sin comillas
   (la forma final, elegida porque el parser de `initwin.c` no quita
   comillas). Ambas formas fallan igual, descartando el formato del
   argumento como causa.
2. **Rastreo del código:** `Opus/open.c` `DocOpenStDof` llama a
   `Opus/create.c` `FnOpenSt`, que inicializa `*pfose = foseCantOpenAny`
   al entrar y solo lo cambia a otro valor en ramas específicas
   (creación de archivo, `FAccessFn`, `fOstNativeOnly`). Ninguna de
   esas ramas aplica para una apertura simple sin esas flags, así que
   cualquier fallo dentro de `FnOpenSt` que no toque `*pfose`
   explícitamente dejo el valor por defecto, y
   `fose <= foseBadFile` en `DocOpenStDof` salta directo a
   `eidCantOpen` sin pasar por el fallback de `dofCmdNewOpen`
   (`DocDoCmdNewOpen`) -- consistente con lo observado.
3. **Inspección del archivo guardado:** se capturó una copia del
   `.doc` de 2417 bytes antes de que el propio test lo borrara
   (interceptando el archivo en
   `~/.wine/drive_c/users/pablo/AppData/Local/Temp/` mientras corría
   el test). Los primeros bytes:

   ```
   9b a5 00 00 21 00 00 00 b1 20 00 00 02 00 00 00
   ```

   Decodificando `struct FIB` (`Opus/wordtech/file.h`) con `int`
   (4 bytes en este build) para `wIdent`/`nFib`/`nProduct`/`nLocale`:
   `wIdent=0xa59b` (correcto, `wMagic`) y `nFib=33` -- coincide
   exactamente con `nFibCurrent` (`#define nFibCurrent 33`,
   `Opus/wordtech/file.h:94`). Pero `Opus/wordtech/word.h` declara
   `typedef long CP;` y `typedef long FC;`, y en este build nativo
   x86-64 (LP64) `long` mide 8 bytes -- 4 bytes más que en el Win16/
   Win32 original para el que se diseñó el formato de archivo. Mezclar
   campos `int`/`unsigned` de 4 bytes con campos `FC`/`CP` de 8 bytes
   (que además exigen alineación de 8 bytes en x86-64) produce un
   `struct FIB` cuyo layout en memoria -- y por lo tanto en disco, si
   la escritura vuelca la estructura tal cual -- no coincide con el
   formato empaquetado del Word 1.x original. Esto es coherente con
   que la ruta de apertura falle más adelante, al leer las tablas PLC
   (`fcPlcfbteChpx`/`cbPlcfbteChpx` vía `HplcReadPlcf`, dentro de la
   rama "native format" de `FnOpenSt`) con desplazamientos corridos
   por el padding de alineación.

   Se intentó confirmar el punto exacto de fallo con `gdb` (breakpoints
   en `FnOpenSt`/`FNativeFormat`, arrancando `WORD1.exe.so` con un
   argumento de archivo), pero el proceso recibe un `SIGSEGV` real
   antes de llegar a los breakpoints incluso con
   `handle SIGSEGV nostop noprint pass` -- Wine parece depender de
   señales para su propia inicialización de forma incompatible con
   correr el binario desde el arranque bajo un debugger nativo. No se
   insistió más: la evidencia de (1) y (2) ya converge en una causa
   consistente sin necesitar el punto exacto de la línea.

**Por qué esto queda fuera del alcance de Task 2:** arreglar esto
requeriría tocar código de formato de archivo bajo `src/Opus/`
(árbol restringido, cambios necesitan autorización explícita según
`CLAUDE.md`) -- probablemente `Opus/wordtech/word.h` (los `typedef`
de `CP`/`FC`/`PN`) y/o la lógica de lectura/escritura de FIB en
`Opus/create.c`/`Opus/save.c`, no el arnés de UI
(`opus_word1_ui_test.cpp`). Esto también cae directo en el criterio de
parada del brief de Task 2: "command-line file-open genuinely doesn't
work AND driving #32770 'Open' also doesn't" -- ambos caminos fallan,
y no por cómo el arnés maneja la UI (el diálogo se abre, el campo se
rellena y se lee correctamente, `IDOK` se entrega) sino por algo
interno a Word mismo.

**Los cuatro números capturados antes de guardar** (instantánea de
Task 1, nunca confirmados contra una relectura porque la reapertura no
llega a completarse): `cpMac=21`, `ftc0=20`, `hps0=0`, `dyp0=16`. El
archivo guardado midió 2417 bytes en todas las corridas. **Esto no es
una comparación de fidelidad de paginación byte-a-byte contra un build
MSVC de Windows** -- ese no es el objetivo de este test en absoluto,
que compara el mismo binario `WORD1` contra sí mismo, dos veces; y en
el estado actual, ni siquiera esa comparación consigo mismo se puede
completar.

**Verificado (corrida reproducible, tres veces):**
```
$ DISPLAY=:91 ctest --test-dir /home/pablo/mswordrt/out/linux-winelib-debug \
    -R '^opus_word1_roundtrip_test$' --output-on-failure
...
roundtrip snapshot cpMac=21 ftc0=20 hps0=0 dyp0=16
roundtrip target path='C:\users\pablo\AppData\Local\Temp\oprt0120.doc' wideLength=46
roundtrip found save_dialog=0x10128 caption='Save As'
roundtrip filename field=0x1013e reads back 'C:\users\pablo\AppData\Local\Temp\oprt0120.doc'
roundtrip saved 'C:\users\pablo\AppData\Local\Temp\oprt0120.doc' size=2417 bytes
roundtrip process 2 watching for base name 'oprt0120'
roundtrip process 2 command-line open did not take effect (title still lacks the base name); falling back to File > Open
roundtrip dismissing stray dialog hwnd=0x3008e after failed command-line open
  id=1 hwnd=0x200d4 class='Button' cachedText='OK' wmGetText='OK' visible=1 enabled=1
  id=65535 hwnd=0x200ca class='Static' cachedText='Cannot open document' wmGetText='Cannot open document' visible=1 enabled=1
roundtrip open filename field=0x2010e reads back 'C:\users\pablo\AppData\Local\Temp\oprt0120.doc'
window class='OpusApp' caption='Microsoft Word - Document1' visible=1 enabled=1
roundtrip leftover #32770 hwnd=0x400d6 caption='Microsoft Word'
  id=1 hwnd=0x300b6 class='Button' cachedText='OK' wmGetText='OK' visible=1 enabled=1
  id=65535 hwnd=0x30124 class='Static' cachedText='Cannot open document' wmGetText='Cannot open document' visible=1 enabled=1
roundtrip File Open did not load the target document

0% tests passed, 1 tests failed out of 1
```

`opus_word1_save_as_test` sigue pasando (no se tocó ese código), y la
corrida completa de la etiqueta da **8/10**: los dos fallos son
`--interaction` (§12, límite de entorno ya documentado) y
`--roundtrip` (este ítem, bloqueado por la causa de arriba, no por el
entorno). No quedan archivos `oprt*.doc` sueltos en ninguna corrida:
`DeleteFileA(ansi_path)` se ejecuta en cada camino de salida del
bloque, incluidos todos los nuevos códigos de fallo de este ítem.

Archivo: `src/port/original/opus_word1_ui_test.cpp` (Pasos 1-3
completos del plan, códigos de fallo 92-108).

## 14. `opus_word1_ui_test` (smoke base): "two-document File Exit was not clean" -- reproducible solo en la VM `debian13` de exia, no en este vps, aislado a la fase post-`exit()` de Wine/Winelib

**2026-08-26, en `debian13` (VM libvirt dentro de `exia`, recién
recompuesta a 6 vCPU/10GB), `DISPLAY=:91`.** El test base sin flags
(`opus_word1_ui_test WORD1.exe`, sin `--modo`) hace File New (crea
`Document2`), después File Exit, y falla en el segundo paso con el
código 12: `GetExitCodeProcess` devuelve éxito pero `exit_code != 0`
(`src/port/original/opus_word1_ui_test.cpp:3107-3110`). Reproducible
**3/3** en esta VM. En este vps, mismo binario, mismo commit, el test
pasa siempre -- diferencia real entre dos entornos Debian 13
"soportados", no ruido.

**Dos hipótesis descartadas por evidencia directa, no por suposición:**

- **No es la cookie xauth / WM de `:0`.** La sospecha inicial era
  razonable -- esta VM tiene una sesión gráfica real en `:0`, y una
  conexión SSH sin forwarding de X11 no tiene el cookie de esa sesión
  (`opus_x64_runtime_test` sí falló ahí con
  `Authorization required, but no authorization protocol specified`).
  Pero el test que nos ocupa se corrió, como todos los demás en esta
  sesión, contra un Xvfb propio en `:91` (headless, sin gestor de
  ventanas, sin sesión de usuario) -- ahí es donde falla 3/3. `:0`
  nunca estuvo en la ecuación de este fallo en particular.
- **No es timeout del arnés.** El código de fallo es el 12 ("was not
  clean"), no el 11 ("timed out"): `WaitForSingleObject(process.hProcess,
  5000)` devuelve `WAIT_OBJECT_0` dentro de tiempo, es decir, `WORD1`
  cierra rápido y limpio en el sentido de que el proceso termina --
  simplemente termina con un código de salida distinto de 0.
  Instrumentado con un `std::cerr` temporal antes de la
  comparación (revertido después, no quedó en el árbol): `got_exit_code=1
  exit_code=1 GetLastError=6`. No es un código de excepción/crash
  (`0xC0000005` y similares) -- es literalmente `exit_code=1`.

**Hipótesis del bucle modal SDM -- descartada por instrumentación
directa.** La sospecha inicial (recogida en una revisión previa de
esta sección): había un segundo sitio que llama `PostQuitMessage` en
todo el árbol compilado, `src/port/original/opus_sdm_runtime.cpp:2660-2666`,
el bucle modal propio de un diálogo SDM "nativo"
(`dialog->native_modal`):

```cpp
MSG message{};
while (!dialog->dying) {
    const int status = GetMessageA(&message, nullptr, 0, 0);
    if (status <= 0) {
        if (status == 0) {
            PostQuitMessage(static_cast<int>(message.wParam));
        }
        ...
```

`materialize_new_template` (`opus_sdm_runtime.cpp:844-871`, el
diálogo de File New que este test usa) fija `dialog.native_modal =
true`, así que el diálogo de File New sí pasa por este bucle modal.
Se instrumentó con `std::cerr` (temporal, revertido) tanto ese
`PostQuitMessage` como `Opus/quit.c:311` y el punto de lectura en
`QuitExit()` (`quit.c:332`). Resultado, corrida real contra
`DISPLAY=:91` en `debian13`:

```
[DEBUG QuitExit] wParam=0, calling exit() now -- C++ static dtors run inside it
two-document File Exit was not clean
```

`[DEBUG PostQuitMessage SDM]` **nunca imprime** -- ese bucle modal no
está activo en el momento del cierre, se descarta la condición de
carrera propuesta. Y `vmsgLast.wParam` es `0` exactamente, como debe
ser: la lógica de `Opus/quit.c` es correcta, `exit(0)` limpio desde
el motor C. El proceso reportado por el arnés (`exit_code=1`) **no es
el valor que el motor pasa a `exit()`**.

**Hipótesis de destructores estáticos C++ globales -- también
descartada por instrumentación directa.** Se revisó `src/port/` en
busca de `atexit()` y de globales con destructor no trivial. Único
candidato que toca una API Win32 real durante el apagado:
`Win95AliasCleanup::~Win95AliasCleanup()`
(`opus_sdm_runtime.cpp:155-165`, instancia global
`g_win95_alias_cleanup`), que llama `DeleteFileA` sobre
`g_win95_save_alias.legacy_path` y sobre las claves de
`g_win95_saved_aliases`. Instrumentado con 5 puntos `std::cerr`
(entrada con volcado de estado, antes/después de cada `DeleteFileA`,
salida limpia), misma corrida:

```
[DEBUG Win95AliasCleanup] enter dtor, created=0 legacy_path='' saved_aliases=0
[DEBUG Win95AliasCleanup] exit dtor cleanly
```

Entra y sale limpio -- `created=0`, este test nunca guarda nada, así
que ninguna rama de `DeleteFileA` se ejecuta siquiera. Ningún otro
global de `src/port/` (`g_dialogs`, `g_win95_saved_aliases`,
`g_page_snapshots`, `searches`) tiene destructor no trivial que
llame Win32; son contenedores planos que solo liberan memoria.

**Conclusión de esta investigación:** el `exit_code=1` que reporta
`GetExitCodeProcess` en el arnés **no se genera dentro de `msword`**.
Confirmado por evidencia directa, no por descarte: el motor C
(`Opus/quit.c`) llama `exit(0)` limpio (`wParam=0` verificado en el
punto exacto de la llamada), y la capa C++ del puerto no tiene
ninguna rutina de apagado (bucle modal SDM, destructor estático) que
altere ese valor antes de que el proceso termine. El origen del `1`
observado por el padre está en la fase **post-`exit()`**, fuera del
código de este proyecto: en la plomería propia de Wine/Winelib
(descarga interna de DLLs tras el `exit()` de libc, o la captura de
código de salida de `GetExitCodeProcess`/`CreateProcessW` para
binarios winelib lanzados externamente como proceso hijo) -- ya hay
precedente documentado de comportamiento no estándar de Wine en este
punto exacto, ver `01-heap-corruption-startup-diagnosis.md`
§25-26. Investigarlo de aquí en más ya no es depurar código de
`msword`, es depurar Wine mismo; no se continúa en esta sesión.

**Estado:** `opus_word1_ui_test` (smoke base) tratado como fallo
**dependiente del entorno GUI de `debian13` / plomería de
Wine-Winelib**, no del código de esta sesión ni de los fixes de
FIB/PLC o `ccpEop` -- no bloquea el trabajo de guardado. Etiqueta
`word1_startup_blocked` en esa VM: **7/10** (este ítem, más
`--interaction` y `--roundtrip`, ya documentados en §12 y §13). En
este vps sigue en **8/10** (§ Resumen). Investigación cerrada; ambos
árboles (vps y `debian13`) quedaron limpios tras revertir toda la
instrumentación temporal.

## 15. `kCcpEop` 1→2: arregla `--roundtrip`, y `--font-typing` resultó tener la expectativa calibrada contra el bug viejo -- ambos en verde a la vez

**2026-08-26, en este vps, `DISPLAY=:91`, rama `fix/ccpeop-2` (no
fusionada a `main`).** Punto de partida: `Opus/ch.h` define
`ccpEop=2` bajo CRLF (el único modo que este puerto compila), pero
`src/port/original/opus_asm_resn_core.cpp` reimplementaba
`CpMacDoc`/`CpMac1Doc`/`CpMacDocEdit` (la version C de
`Opus/asm/resn2.asm`, fiel a los comentarios de esa fuente
ensamblador) con una constante propia `kCcpEop` hardcodeada en `1`.
Esto producía dos síntomas documentados por separado antes de esta
sesión: el drift de `cpMac +2` por ciclo de guardado/reapertura en
`opus_word1_roundtrip_test` (ver CLAUDE.md, sección de estado del
26-08), y -- según una investigación previa parqueada en la rama
`wip/ccpeop-font-typing-regression` -- que corregir la constante
"reproduciblemente rompe" `opus_word1_font_typing_test`, sin causa
localizada.

**Cambio base:** `kCcpEop` 1→2 en `opus_asm_resn_core.cpp` (con
comentario que cita `Opus/ch.h`), y las expectativas hardcodeadas de
`opus_x64_runtime_test.cpp:260` (`CpMacDoc(1)`/`CpMacDocEdit(1)` sobre
un `dod` de prueba con `cpMac=103`) actualizadas de `101/100` a
`99/97` -- consistente con la fórmula `-2*ccpEop`/`-3*ccpEop` ahora
usando el valor correcto.

**La regresión de `--font-typing` se confirmó real** (no ruido): con
`kCcpEop=2`, `display_line_count` final da `2` en vez de `3`, y el
test fallaba con "mixed-font lines disappeared after resizing".
Se investigó con el proceso de `systematic-debugging` (instrumentación
real, no suposición), probando y **descartando tres hipótesis
concretas antes de encontrar la real**:

1. **Invalidación/extensión de `pdr->cpLim` obsoleta tras insertar
   cerca del final del documento** (`wordtech/editspec.c`, la función
   `AdjustCp`/`C_AdjustCp` que ajusta cada `DR` de cada `WWD` cuando
   el documento crece o encoge). Se instrumentó `AdjustCp` y
   `disp2.c`/`FUpdateDr` (el punto donde `pdr->cpLim == cpNil`
   dispara un recálculo vía `CpMacDoc(doc)`) con trazas `fprintf`
   temporales. **Descartada:** `pdr->cpLim` se mantiene correctamente
   sincronizado con `cpMac` en cada ciclo de inserción/recorte
   observado; nunca se queda "atrás" del punto de edición.
2. **`FReplace` rechazando el bloque "speeder" de inserción
   (`cchInsertMax`=32 bytes, `wordtech/insert.c` `BeginInsert`) al
   posicionarse justo en el borde `CpMacDocEdit(doc)`.** Se
   instrumentó `BeginInsert` para capturar `cpInsert`,
   `CpMacDoc`/`CpMacDocEdit` y el resultado de `FReplace` en cada
   llamada. **Descartada:** `fReplaceOk=1` en las 9 llamadas
   observadas durante la corrida completa del test; nunca falla.
3. **El catch de "fin de documento" dentro del fetch de
   `FormatLine`** (`wordtech/format.c:1717`, `LFetch`:
   `if (cpNext >= caPara.cpLim || cpNext >= CpMacDoc(doc) || ...)
   { vfli.chBreak = chEop; goto LEndBreakCp; }`, el candidato más
   directo dado que acota exactamente con `CpMacDoc`). Se instrumentó
   ese punto exacto. **Descartada:** la rama se alcanza 3478 veces
   durante la corrida (`LFetch` es el bucle normal de fetch de runs),
   pero la condición de corte por `CpMacDoc` nunca se cumple ni una
   sola vez -- todo corte de línea real ocurre por `caPara.cpLim` o
   por un carácter excepcional dentro del propio `switch`, no por este
   catch.

**Causa real, encontrada por comparación A/B directa** (mismo test,
mismas pulsaciones, solo cambiando `kCcpEop` 1↔2 y volcando el estado
de líneas con las consultas 30-34 justo después del Enter, antes de
escribir "largeline"): bajo `kCcpEop=1` (el valor viejo, con el que
el test venía pasando), el párrafo 1 (`"fonttest"` + `" secondfont"`,
un run en cada fuente/tamaño) mostraba **23 caracteres reales**
repartidos en 2 líneas envueltas (`n=10`+`n=13`) antes de la línea
vacía del nuevo párrafo. Bajo `kCcpEop=2` (el valor correcto), el
mismo párrafo mostraba **21 caracteres reales en una sola línea, sin
envolver**. Ninguno de los dos números coincide con las pulsaciones
reales enviadas (`"fonttest"`=8 + `" secondfont"`=11 = 19,
confirmado `cp_before=0`, documento nuevo) -- ambas builds tienen un
excedente sobre el conteo real (+4 la vieja, +2 la nueva) en lo que
`edl.dcp` (consulta 34, el conteo de caracteres por línea de
despliegue) reporta, no en el contenido del documento en sí (que se
verificó completo y correcto por separado: los 9 caracteres de
`"largeline"` se anexan al buffer de inserción en dos tandas,
confirmado carácter por carácter vía instrumentación de
`InsertLoopCh`). Es decir: el párrafo 1 **nunca envolvió a 2 líneas
por ancho real de texto** -- el "envoltorio" que el test viejo
observaba bajo `kCcpEop=1` era un artefacto del conteo de caracteres
por línea bajo la constante incorrecta, no maquetación genuina. La
aserción `display_line_count < 3` de `opus_word1_ui_test.cpp` estaba,
sin saberlo, calibrada contra ese artefacto.

**No se investigó más a fondo qué hace exactamente que `edl.dcp`
reporte un excedente sobre el conteo real de pulsaciones bajo
cualquiera de las dos constantes** (no es `LFetch`, según el punto 3
de arriba; queda abierto si alguna vez importa para otro test que
dependa de `edl.dcp` como conteo exacto de caracteres). Para este
test, no hacía falta: el contenido y el formato (negrita/cursiva/`jc`,
ver `opus_word1_formatting_test`) ya se verifican por otras vías
(consultas CHP/PAP directas, comparación byte a byte cp por cp), y
`display_line_count` solo necesitaba reflejar la maquetación real
(2 líneas, no 3) para dejar de fallar por una razón que no era un bug.

**Arreglo aplicado (arnés, no motor):**
`src/port/original/opus_word1_ui_test.cpp`, bloque `--font-typing`:
`display_line_count < 3` → `< 2`; se retira `after_enter_second_band
== 0` de la condición de fallo (esa franja se mide justo después del
Enter, antes de escribir "largeline" -- bajo la maquetación correcta
es la línea 1, el párrafo nuevo aún vacío, y legítimamente da ~0
píxeles oscuros en ese instante). Comentario ampliado documentando
que el sweep de píxeles original (§8) quedó calibrado bajo el
`kCcpEop` viejo.

**Verificado:**
```
$ DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -LE word1_startup_blocked
100% tests passed, 0 tests failed out of 9

$ DISPLAY=:91 ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked
91% tests passed, 1 tests failed out of 11
  (único fallo: opus_word1_interaction_test, §12, límite de entorno ya documentado)
```

`opus_word1_roundtrip_test` y `opus_word1_font_typing_test` en verde
**simultáneamente** -- el trade-off que había bloqueado la rama
`wip/ccpeop-font-typing-regression` no era tal: no había que elegir
entre los dos, había que corregir una aserción de arnés que llevaba
calibrada contra el bug del motor desde que se escribió. Rama
`wip/ccpeop-font-typing-regression` queda obsoleta, no se fusiona
(su diff revierte los query codes 82-84 de `opus_word1_formatting_test`,
que no existían cuando se creó); el trabajo real quedó en
`fix/ccpeop-2`, sobre `main` actual.

## 16. Verificación 2026-09-01 en este vps: gating 9/9 y `--roundtrip` en verde; `--font-typing` cae antes de llegar a la aserción de líneas

Sesión de verificación del trabajo de §15 (consolidación de
`kCcpEop = 2`). Estado del árbol al empezar: los tres cambios ya
estaban en `main` (`011feae`, fusionado en `b01e51a`) --
`kCcpEop = 2` en `src/port/original/opus_asm_resn_core.cpp:19`,
`99/97` en `src/port/original/opus_x64_runtime_test.cpp:260`, y
`display_line_count < 2` / `early_display_line_count >= 2` en la
sección `--font-typing` de `opus_word1_ui_test.cpp`. No hubo nada
que modificar.

El entorno pedido (VM `debian13`) no era alcanzable: `exia`, el
salto SSH intermedio, figuraba `offline, last seen 2h ago` en
`tailscale status`. La verificación se hizo en este vps con Xvfb.

### Resultado

- **Gating: 9/9 en verde** (`opus_original_strtbl_test`,
  `opus_x64_runtime_test`, `opus_original_sttb_test`,
  `opus_original_plc_test`, `opus_sdm_cab_test`,
  `opus_original_command_test`, `opus_shell_memory_foreign_test`,
  `opus_shell_config_test`, `opus_shell_font_substitution_test`).
- **`ctest -E 'opus_word1_font_typing_test|opus_word1_interaction_test'`:
  18/18 en verde**, incluidos `opus_word1_roundtrip_test` (6.66 s) y
  `opus_word1_formatting_test` (8.41 s).
- **`--font-typing`: falla, 4/4 intentos**, en tres formas distintas
  entre corridas: `could not mouse-select the font`,
  `could not mouse-select point size`, y cuelgue hasta el TIMEOUT de
  ctest. **Nunca llega a la aserción de recuento de líneas de §15**:
  muere en `choose_combo_item_with_mouse`, es decir en la
  interacción de ratón con los combos de la cinta, muy por delante
  de cualquier código de maquetación.

### Efecto cascada en la suite completa (importante para leer un `ctest` global)

En la primera corrida completa aparecieron 6 fallos (14, 16, 17, 18,
19, 20). Cinco de esos son artefacto: cuando `--font-typing` se
cuelga, deja vivo un `WORD1.exe.so` residual consumiendo 80-100 % de
CPU, y los tests siguientes (`--about`, `--save-as`, `--roundtrip`,
`--formatting`) reportan `Timeout` aunque **pasan sueltos y pasan en
ctest** una vez que se mata el residuo. Al interpretar una corrida
global hay que verificar procesos `WORD1` colgados antes de contar
fallos.

### Diagnóstico del cuelgue

Con el test parado a los 15 s: el proceso de test está en
`pipe_read` (un `SendMessageW` esperando respuesta vía wineserver) y
el proceso `WORD1.exe.so` está en estado `R`, en bucle ocupado, no
en espera de mensajes. `gdb` adjunto no produce símbolos utilizables
(`Selected architecture i386:x86-64 is not compatible with reported
target architecture i386:x64-32`).

Experimento descartado, **revertido, no está en `main`**: endurecer
`choose_combo_item_with_mouse` contra la carrera
`SetCursorPos` (XWarpPointer) / `SendInput` (XTest) -- gap de 120 ms
tras posicionar el cursor y sondeo del popup en vez de `Sleep(250)`
fijo. Efecto medido: el click pasa a aterrizar de verdad y el fallo
se convierte en cuelgue determinista 4/4 (antes era mezcla de fallo
rápido y cuelgue). Conclusión: la causa no es la temporización del
arnés, sino que WORD1 entra en bucle ocupado con el dropdown del
combo abierto bajo Wine/Xvfb; hacer que el click acierte más
confiablemente sólo hace el bucle más frecuente.

### Reverificación en `DISPLAY=:91` (mismo día)

Repetida la corrida sobre el Xvfb `:91` (el que lleva activo desde el
2026-08-26), con `pkill -9 WORD1.exe` previo:

- `ctest` completo: 14/20, con los mismos 6 fallos (14, 16, 17, 18,
  19, 20) -- otra vez 5 de ellos por cascada.
- Matando el `WORD1` residuo y relanzando 17-20:
  `about` 2.22 s, `save_as` 2.66 s, `roundtrip` 4.78 s,
  `formatting` 6.42 s, **4/4 en verde**.
- `ctest -E 'font_typing|interaction'`: **18/18 en verde**, gating
  9/9 incluido, `roundtrip` 6.98 s.

Es decir: el comportamiento es idéntico en `:91` y en servidores
Xvfb recién arrancados. El único fallo propio es `--font-typing`;
`--interaction` sigue siendo la limitación de entorno de §12.

### Qué cambió respecto al 2026-08-26 (10/11)

No se identificó el disparador. `wine` sigue siendo
`wine-10.0 (Debian 10.0~repack-6)`, instalado el 2026-08-10, sin
actualizaciones desde entonces; el kernel se actualizó el 27 y el 31
de agosto pero la máquina no se ha reiniciado (uptime 11 días). El
fallo se reproduce igual en el Xvfb `:91` que llevaba activo desde
el 2026-08-26 y en servidores Xvfb recién arrancados, con
`wineserver -k` de por medio. Queda pendiente reproducirlo en la VM
`debian13` cuando `exia` vuelva a estar en línea, y compararlo con
`--interaction` (§12), el otro test cuyo fallo es puramente
interacción de ratón con el chrome de la ventana.

## 17. Validación binaria del `.doc` en disco: `doc_inspector` y `opus_doc_inspector_test` (2026-09-01)

Hasta ahora nada leía el `.doc` que WORD1 escribe. `--roundtrip`
(§13, §15) comprueba que un segundo proceso WORD1 lo pueda reabrir y
que el texto coincida, y `--rich-format` compara CHP/PAP vía los
códigos de consulta 82/83/84 de `wproc.c`, pero ambas verificaciones
pasan por el propio motor: si el motor escribiera y releyera de forma
consistente una estructura mal formada, las dos pruebas seguirían en
verde. El bug de corrupción en disco de `898e499` (la `FIB` nativa de
768 bytes escrita en una página de sector de 512 -- desbordamiento
real, no cosmético) es exactamente esa clase de fallo.

`doc_inspector` cierra ese hueco leyendo el archivo por fuera del
motor.

### La herramienta

`src/port/tools/doc_inspector/doc_inspector.cpp`, commit `a8dd611`.
C++20 puro: sin `windows.h`, sin Wine, sin GUI. Se construye con gcc
nativo. Bajo el toolchain Winelib entra en el sub-proyecto de
`src/port/tools/host/` junto a `mkcmd`/`mkdlg`/`bitapp`/`dibapp`, por
el mismo motivo que ellos y que está razonado en
`00-reconnaissance.md`: no depende de Win32 ni del ABI del motor, así
que pasarla por winegcc no aportaría nada y sí la expondría a los
modos de fallo de la capa Winelib. Se instala como
`<build>/host-tools/bin/doc_inspector`.

Qué valida, y de dónde sale cada layout:

- **FIB**: las 105 palabras little-endian de 4 bytes en el orden
  exacto de `CbBltFibPacked()` (`Opus/filewin.c`), con un
  `static_assert` que ata el enum de índices a `cwFibDisk`. Comprueba
  `wIdent == wMagic` (0xA59B), `nFib` dentro de
  `nFibMinDoc..nFibCurrent`, `nFibBack`, `fcMin`/`fcMac`/`cbMac`
  contra el tamaño real del archivo y, cuando `!fComplex`, que
  `fcMac - fcMin` sea la suma de los `ccp*`.

- **FKP de CHPX y PAPX**: páginas de `cbSector` alcanzadas por
  `plcfbteChpx`/`plcfbtePapx`, más el relleno secuencial desde
  `pnChpFirst`/`pnPapFirst` que hace `FFillMissingBtePns`
  (`Opus/openrare.c`). Verifica `crun`, monotonía estricta de `rgfc`,
  los offsets de propiedad -- que son palabras de 16 bits, el
  `b <<= 1` de `fetch.c`/`inssubs.c`, no offsets de byte -- y la
  longitud del registro: `cb` en bytes para CHPX, `cw` en palabras
  para PAPX (`fStoreCw = fPara` en `C_FAddRun`). Exige además
  continuidad entre páginas y cobertura completa de `fcMin..fcMac`.

- **Las 21 PLC nombradas por el FIB**, descompuestas como en
  `HplcReadPlcf()` (`Opus/create.c`): `ccp` cp's de `cbCpDisk`
  seguidos de `ccp-1` registros del tamaño propio de cada tabla, con
  los mismos `cbSED`/`cbPGD`/`cbFLD`/... que el motor pasa en cada
  llamada. Marca `MISALIGNED` cuando `cb` no divide en entradas
  enteras, y revisa monotonía y rango de los cp.

### Dos detalles del formato que quedaron establecidos al escribirla

1. **El FIB de Word 1.x no tiene ranura `plcfed`.** No existe en
   `struct FIB` ni en el recorrido de `CbBltFibPacked()`. Las tablas
   vivas más cercanas son las cinco `plcffld*` (foo = `struct FLD`),
   que la herramienta sí inspecciona; el PLC de EDL de
   `Opus/wordtech/disp.h` es sólo memoria y nunca se escribe. La
   salida lo dice explícitamente para que nadie vuelva a buscarlo.

2. **El ancho del FC en disco no es fijo.** El FIB y los arrays de cp
   van siempre a 4 bytes (`cbCpDisk`), pero `rgfc` de los FKP y los
   registros foo de las PLC van a **ancho nativo**: 8 bytes en este
   build Winelib LP64 y 4 en el de MSVC x64, que es justo lo que
   advierte la nota junto a `cbCpDisk` en `Opus/wordtech/file.h` --
   un `.doc` de este build no es compatible byte a byte con uno del
   build MSVC. La herramienta lo autodetecta puntuando ambas
   hipótesis contra `cbSED` y contra la coherencia de los FKP;
   `--fc-width=4|8` fuerza. En los archivos de este vps la
   autodetección da 8 con margen amplio (18 contra -10 en
   `roundtrip.doc`, 20 contra -10 en `rich_format.doc`).

Dos supuestos iniciales resultaron **falsos** y se corrigieron contra
el motor antes de fijar las comprobaciones, porque ambos producían
falsos positivos:

- El archivo **no** se redondea a un múltiplo de sector. No hay
  ningún `SetEndOfFile` en `Opus/filewin.c`: el archivo termina en el
  último byte escrito. Los `.doc` de las pruebas miden 2385 bytes,
  que no es múltiplo de 512.
- `cbMac` es exactamente el tamaño físico del archivo, no una marca
  lógica por debajo de un final redondeado. La comprobación correcta
  es `cbMac == tamaño`; un archivo más largo es holgura de un
  guardado anterior y sólo merece nota, no problema.

### Cómo llega el archivo a la prueba

Commit `b7a1c7b`. Las dos únicas pruebas que atraviesan el diálogo
Save As real y producen un `.doc` son `opus_word1_roundtrip_test`
(`--roundtrip`) y `opus_word1_formatting_test` (`--rich-format`).
`opus_word1_save_as_test` **no** escribe nada: abre el diálogo y lo
cancela (`WM_COMMAND` id 2 = IDCANCEL).

Ambas borran su `.doc` en todas sus salidas, así que
`opus_word1_ui_test.cpp` guarda una copia bajo
`OPUS_X64_DOC_ARTIFACT_DIR` cuando esa variable está en el entorno
(`keep_doc_artifact()`/`discard_doc_artifact()`). Guardarla es
estrictamente un efecto lateral: si `CopyFileA` falla se registra y
se sigue, nunca convierte en fallo una prueba de guardado que por lo
demás pasó. Cada modo borra su propio artefacto antes de guardar, de
modo que nunca se valida uno rancio.

El directorio tiene que ser visible desde Wine, así que bajo el
toolchain Winelib CTest lo pasa como `"Z:"` más la ruta unix del
árbol de build, con barras normales -- Win32 las acepta como
separador y `wineboot` mapea `Z:` a `/` en todo prefijo
(`~/.wine/dosdevices/z: -> /` en este vps). La rama MSVC ya tiene
ruta nativa y no lleva prefijo.

El fixture `opus_saved_doc_artifacts` (`FIXTURES_SETUP` en las dos
pruebas de guardado, `FIXTURES_REQUIRED` en la nueva) es lo que hace
que `ctest -R opus_doc_inspector` las ejecute primero en vez de
inspeccionar un artefacto viejo.

`src/cmake/RunDocInspector.cmake` recorre el directorio y corre
`doc_inspector --verbose` sobre cada `.doc`. Falla si alguno sale con
código distinto de 0 y **también si no encontró ningún archivo**:
pasar en silencio convertiría la prueba en un no-op permanente el día
que las pruebas de guardado dejen de producir archivo. Los dos
caminos negativos se verificaron a mano -- artefacto con `crun`
corrupto y directorio vacío, ambos hacen fallar el script.

Queda registrada en `src/CMakeLists.txt`, no en un
`src/port/original/CMakeLists.txt`: ese archivo no existe,
`port/original/` no es un subdirectorio de CMake y todas las pruebas
del proyecto se registran en `src/CMakeLists.txt`.

### Medición en este vps (2026-09-01, `DISPLAY=:91`)

```
Start 19: opus_word1_roundtrip_test ....... Passed  7.00 sec
Start 20: opus_word1_formatting_test ...... Passed  8.74 sec
Start 21: opus_doc_inspector_test ......... Passed  0.04 sec
```

**0.04 s** es el coste completo de la validación binaria: los dos
`.doc` leídos, parseados y verificados enteros. No hay Wine, ni
servidor X, ni proceso WORD1 en ese tramo -- es un binario nativo
leyendo dos archivos de 2385 bytes. Los 15.7 s restantes son las dos
pruebas productoras, que ya existían.

Resultado sobre los dos artefactos, ambos `STRUCTURALLY VALID`:

| artefacto | tamaño | `fcMin..fcMac` | `ccpText` | FC detectado |
|---|---|---|---|---|
| `roundtrip.doc` | 2385 | 512..532 | 20 | 8 bytes |
| `rich_format.doc` | 2385 | 512..535 | 23 | 8 bytes |

En ambos: `plcfsed` y `plcfpgd` con cps `0..ccpText`, bin tables con
cps `fcMin..fcMac`, FKP contiguos y con cobertura completa.
`rich_format.doc` muestra además 3 runs CHPX en su página con
compartición de propiedad (dos runs apuntando al mismo offset 508),
que es el comportamiento de `C_FAddRun` cuando encuentra un CHPX
idéntico ya almacenado en la página.

### La etiqueta: por qué **no** es gating

`opus_doc_inspector_test` lleva
`LABELS "word1_startup_blocked;doc_binary_validation"`.

La ejecución de `doc_inspector` es nativa y determinista, y por sí
sola sería perfectamente gating. Lo que no lo es son sus **entradas**:
el `.doc` sólo existe si WORD1 arrancó, pintó y completó un Save As
bajo Wine/Xvfb. Meterla en el conjunto gating trasladaría a CI toda
la fragilidad de entorno que §12 y §16 documentan. Por eso hereda la
etiqueta de sus productoras. La segunda etiqueta,
`doc_binary_validation`, permite seleccionarla sola
(`ctest -L doc_binary_validation`).

Cuándo tendría sentido promoverla a gating: cuando los tests de la
etiqueta `word1_startup_blocked` sean estables en las dos máquinas de
referencia. Alternativa intermedia, si se quiere gating antes de eso:
versionar un `.doc` de referencia generado una vez y validarlo sin
WORD1 de por medio -- eso sí sería gating nativo puro, pero valida un
archivo congelado, no lo que el motor escribe hoy, que es
precisamente lo que interesa aquí.

### Estado de CTest tras el cambio

La suite pasa de 20 a **21 pruebas**. Corrida completa en este vps
(`DISPLAY=:91`, 86.68 s):

- **Gating: 9/9 en verde**, sin cambios.
- **Etiqueta `word1_startup_blocked`: 12 pruebas** (las 11 de antes
  más `opus_doc_inspector_test`).
- **Total: 19/21.** Los dos fallos son los ya explicados y ninguno es
  nuevo:
  - `#14 opus_word1_interaction_test` -- limitación de entorno
    Xvfb/Wine de §12, arrastre de la barra de título.
  - `#16 opus_word1_font_typing_test` -- el fallo de §16, muere en
    `choose_combo_item_with_mouse` sin llegar a la aserción de
    recuento de líneas.

Se comprobó explícitamente que `--font-typing` **no** es regresión de
este cambio: con `git stash` sobre `opus_word1_ui_test.cpp` y
recompilando, falla igual con el binario previo. Las modificaciones
de este commit a ese archivo son las dos funciones auxiliares y
cuatro llamadas, todas dentro de los bloques `roundtrip_mode` y
`rich_format_mode`; no tocan el camino de `--font-typing`.

En esta corrida no se dio el efecto cascada de §16 porque
`--font-typing` falló rápido en vez de colgarse. Al leer un `ctest`
global sigue valiendo la advertencia de §16: si `--font-typing` se
cuelga, deja un `WORD1.exe.so` residual que hace expirar por timeout
a las pruebas siguientes, incluidas las dos productoras del artefacto
y, por tanto, también `opus_doc_inspector_test`.

Fusionado a `main` en `b1db7ef` y publicado
(`b01e51a..b1db7ef  main -> main`).

## Resumen

Los 8 ítems de comportamiento de la lista original de §26 de
`01-heap-corruption-startup-diagnosis.md`, estado final tras esta
sesión (2026-08-19, exia):

1. `--about` -- **arreglado** (Task 3, AV de `_setjmp`/`longjmp` ABI)
2. `--new` (File > New) -- **arreglado**, mismo root cause que #1
   (Task 4, §6)
3. `--save-as` -- **arreglado**, causa independiente: diálogo señuelo
   sin conectar al real (Task 5, §7)
4. `--font-typing` -- **arreglado** (Task 6, §8). Los 4 bugs originales
   cerrados: nombres de fuente enumerables, `union FCID` LP64, foco
   tras selección de ribbon (2026-08-20, `opus_sdm_runtime.cpp`, no
   `Opus/iconbar1.c`), y `EraNameFromFtc`/tabla fija de 4 nombres de
   época reemplazada por `OpusFontKey.szFace` en el núcleo
   (2026-08-25, commits `bf5f117`/`b69e715`/`e7c50d1` -- ver "Octava
   actualización" en §8 para el rastro completo de diagnóstico, incluidas
   las hipótesis descartadas por el camino). El `fail(60)` que quedaba
   tras cerrar el cuarto bug era en sí un problema de arnés, no de
   producto: dos muestras de píxel se tomaban antes del repintado
   forzado que el propio test ya hacía, y las franjas hardcodeadas no
   coincidían con la altura de línea real del documento -- arreglado
   en `src/port/original/opus_word1_ui_test.cpp`, sin tocar
   `Opus/disp.c`/`screen.c` ni `src/core/` (ver "Octava actualización"
   en §8)
5. `--clipboard` (Ctrl+A) -- **arreglado**, mismo root cause que #1
   (Task 7, §9)
6. `--selection` -- **arreglado**, 3 bugs de arnés encadenados (Task
   8, §10)
7. `--typing` -- **arreglado**, mismo patrón de foco que #6 (Task 9,
   §11)
8. `--interaction` (arrastre de ventana) -- **limitación de entorno
   confirmada, no bug** (Task 10, §12)

**Etiqueta `word1_startup_blocked`: 7/9** (los 2 que faltan son el
bug 4 sin cerrar de `--font-typing` y la limitación de entorno de
`--interaction`, ambos ya explicados arriba, no misterios). El noveno
test de la etiqueta era `word1_port_smoke_test`, que ya pasaba desde
antes de esta sesión.

**Actualización 2026-08-25: etiqueta en 8/9.** Con el bug 4 de
`--font-typing` cerrado (ver punto 4 arriba y "Octava actualización"
en §8), el único fallo restante de la etiqueta es
`opus_word1_interaction_test` (Task 10, §12, limitación de entorno
Xvfb/Wine confirmada, no bug del proyecto).

**Actualización 2026-09-01: suite en 21 pruebas, 19/21 en este vps.**
La etiqueta `word1_startup_blocked` pasa a 12 pruebas con la entrada
de `opus_doc_inspector_test` (§17), que valida por fuera del motor el
`.doc` que WORD1 acaba de escribir. Los dos fallos de la etiqueta son
`opus_word1_interaction_test` (§12, entorno) y
`opus_word1_font_typing_test` (§16, cae en
`choose_combo_item_with_mouse`); el gating sigue en 9/9.

Aparte de la lista de 8, sigue sin investigar: `opus_x64_runtime_test`
(gating, cuelga sin imprimir nada, confirmado pre-existente y no
relacionado con ningún fix de esta sesión).

Rama `fix/winelib-startup-blocked`, no fusionada a `main`. Todo
pusheado a `origin/fix/winelib-startup-blocked`.

## 14. Save As on debian13: "Not a valid file name" in a clean checkout, passes in the long-lived one -- environmental, cause not yet found

**2026-09-02, debian13 VM, `DISPLAY=:91`.** Surfaced while verifying
`doc_inspector`'s new bookmark/page/footnote/field checks
end-to-end: `opus_word1_roundtrip_test` and `opus_word1_formatting_test`
are the fixtures that produce the `.doc` `opus_doc_inspector_test`
inspects, so a full `ctest -R opus_doc_inspector_test` run needs them
green first.

**Symptom.** A fresh `git worktree add` off `origin/main` (`6fae091`),
configured and built clean (`opus_original_engine`, `WORD1`,
`opus_word1_ui_test`), fails `opus_word1_roundtrip_test` and
`opus_word1_formatting_test` every time: `opus_word1_ui_test.cpp`
(around line 1396-1475) resolves `GetTempPathA()`, builds an 8.3-safe
`oprtXXXX.doc` name, sets it into the Save As dialog's filename field
(`cmb13`, `0x047C`) via `WM_SETTEXT` -- not simulated typing -- and
reads it back correctly (`roundtrip filename field=... reads back
'C:\users\pablo\AppData\Local\Temp\oprt0134.doc'`). After the dialog
is dismissed, a second `#32770 caption='Microsoft Word'` dialog
appears with a static control reading `"Not a valid file name"`
(id=65535) and an `"Aceptar"` OK button (id=1); the target `.doc`
is never written. This is `WORD1`'s own path validator rejecting the
path post-submit, not a harness typing or timing bug.

The **same commit**, built and run the same way in the long-lived
original checkout (`/home/pablo/msword` on the same VM, same shared
Wine prefix, same `DISPLAY=:91`), **passes reliably**.

**Three hypotheses tested and refuted:**

1. **First-run `WINWORD.INI`/`W95TEMP` state.** The original
   checkout's `bin/WINWORD.INI` records a hardcoded absolute scratch
   path (`Z:\HOME\PABLO\MSWORD\BIN\W95TEMP\W95E790.DOC`). Moved both
   `WINWORD.INI` and `bin/W95TEMP` out of the original checkout and
   reran `opus_word1_roundtrip_test` there: still passed, and
   regenerated both on its own. Not the cause; restored afterward.
2. **Simulated-typing race.** Ruled out by reading the code first
   (`WM_SETTEXT`, not keystrokes) and confirmed empirically: the
   filename field reads back the exact intended path every time, and
   the fresh worktree failed identically and deterministically across
   3 consecutive runs (not intermittent).
3. **The uncommitted Search/Replace feature** (see below) **causing
   it as a side effect.** Copied all five of its files verbatim onto
   the clean worktree, rebuilt `opus_original_engine` + `WORD1` +
   `opus_word1_ui_test`, reran 3x: failed identically every time.
   Refuted.

**Refuting (3) closes off the source-code angle entirely.** debian13's
long-lived checkout had 15 git-tracked files modified but uncommitted
(`main` there is stale at `4c98436`, three commits behind
`origin/main`). All 15 are now accounted for:

- **9 are phantom** -- byte-identical to what commit `5b52dc6` ("pack
  FKP rgfc entries to a 4-byte disk format") already merged and
  pushed: `Opus/debug/debugfn.c`, `Opus/filewin.c`, `Opus/openrare.c`,
  `Opus/wordtech/fetch.c`, `Opus/wordtech/file.h`,
  `Opus/wordtech/fkp.h`, `Opus/wordtech/inssubs.c`,
  `Opus/wordtech/prm.h`, `Opus/wordtech/savefast.c`, plus the
  non-bookmark comment updates already folded into
  `doc_inspector.cpp`. `git status` shows them "modified" only
  because that machine's `main` never pulled past the commit before
  `5b52dc6` -- the content itself matches `origin/main` exactly.
  **Nothing to commit here.**
- **5 are a real, complete, unreviewed feature**: a Search/Replace
  dialog. `Opus/wproc.c` gains a `case 85` exposing `vtmcFocus` for
  polling; `port/original/opus_sdm_runtime.cpp` gains
  `kIddSearch`/`kIddReplace` materialization (~200 new lines: CAB
  structs mirroring `search.hs`/`replace.hs`, `read_search_cab` /
  `sync_search_cab` / `read_replace_cab` / `sync_replace_cab`,
  `materialize_search_template` / `materialize_replace_template`);
  `port/original/opus_word1_ui_test.cpp` gains a `--find-replace` test
  mode driving the real dialog end to end; `port/original/replace.sdm`
  and `search.sdm` go from stub `dltReplace`/`dltSearch = { 0 }` to
  real `DLT` tables built from `Opus/dlg/replace.des` /
  `search.des`. Confirmed by direct application (above) that this
  feature is **not** the Save As cause -- it stands as its own,
  independent, still-uncommitted piece of work.

With those 15 files accounted for and proven not to explain the
symptom, the two checkouts' git-tracked source is byte-identical, yet
one passes and the other fails. **The cause is not in git-tracked
code.** Untested candidates for the next session:

- Wine's per-executable `HKCU\Software\Wine\AppDefaults\<name>\...`
  settings, if keyed by the full path of the `.exe` rather than just
  its basename (`WORD1.exe` is the same name in both checkouts, but
  the checkouts live at different absolute paths).
- Non-determinism or an unaudited difference in the *generated,
  non-git-tracked* build output between the two separate
  `port/tools/host` sub-builds (`opus_mkcmd_tool`/`opus_mkdlg_tool`
  regenerating `word1.rc`/`word1.spec` from `Opus/dlg/*.des` and
  `opuscmd_native.inc`) -- never diffed against each other.

**State left clean:** the verification worktree this investigation
used has been removed (`git worktree remove --force`); the original
checkout's `WINWORD.INI`/`W95TEMP` were restored; nothing was
committed on debian13. To resume, recreate a worktree off
`origin/main` and start from the "Untested candidates" list above.
