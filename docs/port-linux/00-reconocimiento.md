# Reconocimiento para el port a Winelib (ELF nativo)

Fecha original: 2026-08-09 · Rama `main` @ `ac5472e`. Actualizado el mismo día tras
completar la Fase 0 del plan.

Todas las afirmaciones de este documento están respaldadas por comandos ejecutados en
esta máquina. Los experimentos de reconocimiento se hicieron en un directorio temporal
fuera del repositorio; los cambios de la Fase 0 (`src/cmake/`,
`src/CMakeLists.txt:79-89`) sí están en el árbol y se detallan en la sección «Fase 0 —
ejecutada el 2026-08-09» más abajo.

---

## 0. Resumen ejecutivo

El port es **más viable de lo que sugiere el `CMakeLists.txt`**. Con tres banderas de
compilador (`-std=gnu89 -funsigned-char -fms-extensions -fpermissive`), un directorio
de compatibilidad de mayúsculas para 12 cabeceras, y las cabeceras generadas por las
herramientas de Microsoft, **156 de las 207 unidades de traducción del motor
(`opus_original_engine`) compilan sin tocar una sola línea de `src/Opus/`** (186
errores restantes, repartidos en 51 archivos). La cifra original de reconocimiento era
154/207 con 202 errores; la Fase 0 sumó dos módulos (`menuhelp.c`, `eldlg.c`) al
resolver el ítem 2.

Los cinco obstáculos reales, en orden de riesgo:

| # | Obstáculo | Estado |
|---|---|---|
| 1 | 427 comandos de Word se resuelven con `GetProcAddress` sobre la tabla de exportación del propio `.exe`, generada por `#pragma comment(linker,"/export:")` | **Resuelto en principio**: verificado que un `.spec` de winebuild expone símbolos vía `GetProcAddress` en un ELF Winelib |
| 2 | `src/cmake/` no existía en ningún commit; el build referenciaba dos archivos ausentes | **Resuelto en la Fase 0** (`src/cmake/GenerateMenuHelpHeader.cmake`, `src/cmake/GenerateElxInfoHeader.cmake`). Ya no bloquea. |
| 3 | Sistema de archivos sensible a mayúsculas: 12 cabeceras, 202 archivos afectados | Mecánico |
| 4 | `long` es de 8 bytes en Linux LP64 y de 4 en Windows LLP64 | Acotado, pero requiere auditoría |
| 5 | libstdc++ es inutilizable para cadenas anchas bajo el `-fshort-wchar` de winegcc | Acotado a ~5 archivos, todos en `src/port/` |

---

## 1. Inventario de construcciones específicas de MSVC

Ámbito: el árbol que efectivamente se compila — `src/Opus/**` (207 TUs del motor),
`src/port/original/**`, `src/port/tools/**`, y las 4 herramientas de
`src/OpusEtAl/tools/src/` que el build usa (`mkcmd.c`, `bitapp.c`, `mkdlg.c`,
`mergeelx.c`). Se excluyen `Opus/asm/` (59 `.asm` de referencia, no compilados),
`Opus/sdm/`, `Opus/tools/`, `Opus/program/`, `OpusEtAl/tools/src/draw/`, `convtest/`,
`opustlbx/` y `OpusProg/`.

### 1.1 Palabras clave de segmentación y convención de llamada

| Construcción | Ocurrencias | Situación |
|---|---:|---|
| `PASCAL` | 210 | **No es problema.** `Opus/lib/qwindows.h:105` la define como `pascal`, y `port/original/opus_x64_compat.h:143` define `pascal` a vacío. |
| `FAR` | 557 | Ídem vía `qwindows.h:112` → `far` → vacío (`opus_x64_compat.h:126`). |
| `HUGE` | 556 | Ídem vía `opus_x64_compat.h:152`. |
| `NEAR` | 45 | `qwindows.h:113` la define a vacío. |
| `__stdcall` / `_stdcall` | 2 | Sólo en `port/original/opus_asm_resn2_sttb.cpp:8,13`. winegcc lo define como `__attribute__((ms_abi))`. Sin efecto práctico en x86-64. |

**Conclusión:** el autor original ya neutralizó todo el vocabulario segmentado en
`opus_x64_compat.h`. No hay trabajo pendiente aquí.

### 1.2 `__declspec`

3 ocurrencias, todas viables:

- `Opus/RTFTBL.H:128` — `__declspec(selectany)`. winegcc lo traduce a
  `__attribute__((weak))` (verificado en la línea de comandos que genera: `-D__declspec_selectany=__attribute__((weak))`).
- `port/original/opus_asm_resn2_sttb.cpp:8,13` — `__declspec(dllimport)` →
  `__attribute__((dllimport))`.

**Sin trabajo pendiente.**

### 1.3 `#pragma`

| Pragma | Archivos | Situación |
|---|---|---|
| `#pragma pack(push/pop)` | `Opus/cmdtbl.h:65,208`; `port/original/opus_asm_movecmds.c:12,48`; `OpusEtAl/tools/src/bitapp.h:37,64`; `OpusEtAl/tools/src/mkcmd.c:18,405` | GCC lo soporta idénticamente. Sin trabajo. |
| `#pragma once` | 9 archivos, todos en `port/original/` | Soportado. Sin trabajo. |
| `#pragma comment(linker,"/export:…")` | Generado por `mkcmd.c:1415` en `opuscmd_native.inc`, consumido por `port/original/opus_asm_movecmds.c:162` | **El problema central del port.** Ver §1.7. |
| `#pragma warning` / `intrinsic` / `optimize` | 0 | — |

### 1.4 Intrínsecos, ensamblador en línea y cabeceras exclusivas

- `__asm` / `_asm`: **0 ocurrencias reales.** La única coincidencia
  (`Opus/prompt.h:99`) es texto dentro de un comentario. El autor original ya tradujo
  todo el ensamblador a C/C++ en `port/original/opus_asm_*.{c,cpp}`.
- `<intrin.h>`, `__debugbreak`, `_BitScan*`, `__popcnt`, `_rotl`: **0**.
- `__int8/16/32/64`: **0**.
- `__inline`: 12 ocurrencias (6 en `src/Opus/`, 6 en `opus_x64_compat.h`). GCC lo
  acepta como sinónimo de `inline`. Sin trabajo.
- Cabeceras propias de MSVC en uso: `<direct.h>` (`opus_asm_file2.cpp:3`),
  `<malloc.h>` (`opus_x64_compat.h:19`, `mergeelx.c:7`), `<process.h>`, `<io.h>`
  (sólo en herramientas no compiladas). **Todas existen en `/usr/include/wine/msvcrt/`**,
  pero ver §5.3 sobre por qué ese directorio no es utilizable de forma global.

### 1.5 CRT seguro de Microsoft

Sólo 6 sitios, todos en `src/port/` (territorio modificable):

- `_snprintf_s` × 3 — `port/original/opus_startup_diagnostics.cpp:14,32,60`
- `_vsnwprintf_s`, `_countof` — `port/original/opus_original_startup_probe.cpp:161`
- `_stricmp` — `port/original/opus_sdm_runtime.cpp` (7 sitios),
  `port/original/opus_word1_ui_test.cpp` (3 sitios)
- `_wcsicmp` — `port/original/opus_word1_ui_test.cpp` (3 sitios)

También `_stricmp` aparece 6 veces en `OpusEtAl/tools/src/mkcmd.c` (herramienta de
host; `strcasecmp` es sustituto directo).

### 1.6 Constructos de C anteriores a ANSI que GCC rechaza

Éste es el inventario que el enunciado no pedía explícitamente pero que domina el
esfuerzo real. Cifras obtenidas compilando las 207 TUs del motor con winegcc
(ver §6 para la metodología):

| Clase de error | Ocurrencias | Archivos |
|---|---:|---|
| *cast as lvalue* — `*((int *)p)++` | 21 | `Opus/style.c` (6), `Opus/ffread.c` (3), `Opus/interp/exp.c` (2), `OpusEtAl/tools/src/mkcmd.c` (5), + 5 en variantes de asignación |
| Inicializador no calculable en tiempo de carga | 90 | **una sola cabecera**: `Opus/keys.h:287-…`, expandida por `Opus/iconbar3.c:710` y similares |
| Definición K&R en conflicto con prototipo ANSI | 13 | `Opus/help.h:229` vs `port/original/about.sdm:5`, `Opus/help.c:316`, etc. |
| `static` tras declaración no-`static` | 10 | dispersos |
| Llamada con más argumentos que el prototipo K&R vacío | 9 | punteros a función declarados `()` |
| `struct` anidada con etiqueta y sin nombre de miembro (extensión MS) | ~0 tras `-fms-extensions` | `Opus/wordtech/props.h:229` (`struct PAP` / `struct PAPS`) — **resuelto con `-fms-extensions`** |

Nota importante: `struct PAP` contiene `struct PAPS { … };` sin nombre de miembro.
Ese es el *nameless struct* de MSVC, y `-fms-extensions` lo habilita en GCC. Antes de
añadir la bandera, ese solo patrón hacía fallar `search.c`, `layout.c`, `layout1.c`,
`layout2.c` y otros; después, desaparece por completo.

### 1.7 La dependencia arquitectónica: exportaciones del ejecutable

`port/original/opus_asm_movecmds.c:174-177`:

```c
HMODULE module = GetModuleHandleW(NULL);
return (OPUS_PFN)(uintptr_t)GetProcAddress(module, name);
```

Word resuelve **cada comando** por nombre contra la tabla de exportación de su propio
ejecutable. Esa tabla la puebla `opuscmd_native.inc`, generado por MKCMD, con una línea
`#pragma comment(linker, "/export:NAME")` por función.

Ejecuté MKCMD en Linux (§3) y conté: **427 exportaciones, 427 nombres únicos.**

GCC ignora `#pragma comment` con una advertencia, de modo que la compilación *pasaría*
y el binario fallaría en tiempo de ejecución al resolver el primer comando. Bajo
Winelib el equivalente es un archivo `.spec` de winebuild.

**Verificado experimentalmente** (`exp.c` + `exp.spec` → `winegcc -o exp.exe exp.c exp.spec`):

```
module=0x7f5812880000 CmdHelp=0x7f581289042e -> 4242  CmdFoo=0x7f5812890439
```

`GetProcAddress(GetModuleHandleW(NULL), "CmdHelp")` resuelve correctamente en un ELF
Winelib cuando el símbolo está declarado en el `.spec`. **El supuesto más riesgoso del
port queda validado.**

---

## 2. Estado de `src/port/tools/`

El directorio contiene tres archivos:

| Archivo | Tipo | Portabilidad |
|---|---|---|
| `opus_dibapp_tool.cpp` | C++20 ISO puro (`<cstdint> <fstream> <iomanip> <iostream> <string> <vector>`) | **Totalmente portable.** Verificado: `g++ -std=c++20` lo compila sin errores, y lo ejecuté para regenerar los 37 `.hb` desde `Opus/resource/*.dib`. |
| `opus_cabi_tool.cpp` | C++20 **+ `opus_x64_compat.h` → `<windows.h>` + `sdm.h`** | **No es una herramienta de host portable.** Incluye 78 cabeceras `.hs` y calcula `sizeof`/alineación de las estructuras SDM. Su salida depende del ABI del objetivo, así que debe compilarse con el mismo compilador que el motor. Bajo Winelib eso significa construirlo como ejecutable Winelib y ejecutarlo a través de su stub. Nótese que ya hace `#undef native` en la línea 3 — el autor ya se topó con el problema descrito en §5.4. |
| `make_win95_toolbar_sprite.ps1` | PowerShell | No participa del build (`word95-toolbar.bmp` está versionado). Sin impacto. |

### Las cuatro herramientas de `OpusEtAl/tools/src/` que sí se compilan

Éstas son las que realmente determinan si la cadena de generación puede correr en
Linux. Resultado de compilarlas con `gcc` nativo:

| Herramienta | `-std=gnu17` (por defecto en GCC 16) | `-std=gnu89` | Notas |
|---|---|---|---|
| `bitapp.c` | falla | **compila** | `#define FAR` ya está guardado con `OPUS_X64_TOOL` (`bitapp.h:18`) |
| `mkdlg.c` | falla | **compila** | `main()` K&R sin tipo de retorno |
| `mergeelx.c` | falla | **compila** | declaraciones implícitas |
| `mkcmd.c` | falla | **falla** | 5 sitios de *cast as lvalue* + `_stricmp` |

`mkcmd.c` es la única que necesita edición. Con un parche mecánico de 5 líneas
(macros `OPUS_PI_RD`/`OPUS_PI_WR`) y `-D_stricmp=strcasecmp`, compila y **corre
correctamente en Linux**, produciendo `opuscmd.h`, `IBCM.H`, `RGBCM.H`, `OPUSCMD2.H`,
`OPUSMENU.H`, `MENUHELP.TXT`, `opuscmd.asm` y `opuscmd_native.inc` (§3).

### Un problema de convención POSIX en BITAPP

`OpusEtAl/tools/src/bitapp.c:59`:

```c
if (*(rgszArg[cCurArg]) == '/')
    SetFlag(*(rgszArg[cCurArg]+1));
```

Cualquier argumento que empiece con `/` se interpreta como conmutador estilo DOS. Como
CMake pasa rutas absolutas, **toda invocación de BITAPP falla en Linux** con
`Invalid Switch on command line.` La solución no requiere tocar `bitapp.c`: basta
invocarlo con rutas relativas desde un `WORKING_DIRECTORY`. Verificado: con
`./mk/bitapp res/8hdr.bmp gen/8hdr.hb` el análisis de argumentos ya pasa.

Queda un fallo secundario sin diagnosticar (`Unexpected End Of File Reached in Input
file!`) que casi con seguridad es §4.1: `bitapp.h:29` declara `typedef unsigned long
DWORD;`, de modo que `sizeof(struct BITMAP)` cambia en LP64 y `fread(&bm, sizeof(BITMAP), 1, …)`
lee un número incorrecto de bytes. Es la evidencia más limpia de que el problema de
`long` es real y no teórico.

---

## 3. Cadena de recursos

### 3.1 Qué archivos existen

- **`.rc`**: exactamente tres. Sólo uno participa del build:
  - `src/port/word1.rc` — **el único referenciado por CMake** (línea 857). Contiene
    `RT_MANIFEST`, un `BITMAP` (201), dos `ICON` (301/302) y un bloque `VS_VERSION_INFO`.
  - `src/Opus/resource/misc.rc` — el script de recursos original de Word 1.1a
    (iconos, cursores, bitmaps, con `#include "winrc.h"`). **No se compila hoy.**
  - `src/OpusEtAl/tools/src/draw/mustang.rc` — de una herramienta no compilada.
- **`.dlg`**: **ninguno.** El enunciado asume su existencia; el archivo no está.
- **`.des`**: 86 archivos en `src/Opus/dlg/`. Son las fuentes del Dialog Editor.
- **`.elx`**: **ninguno**, pese a que `src/Opus/dlg/elx.txt` lista 86 nombres `.elx` y
  `CMakeLists.txt:391` los busca con `file(GLOB)`. El comentario en `CMakeLists.txt:74-76`
  lo reconoce: el archivo carece del compilador del Dialog Editor que convertía `.des`
  en `.elx`. **El objetivo `opus_generated_dlgcheck` opera sobre un conjunto vacío.**
- **`.hs` / `.sdm`**: 78 pares en `src/port/original/` — reconstrucciones del autor que
  sustituyen la salida ausente del Dialog Editor.

### 3.2 Cómo los consume CMake hoy

`project(… LANGUAGES C CXX RC)` (línea 3) declara el lenguaje RC, y `port/word1.rc`
entra como fuente del ejecutable `WORD1`. Todo el resto de la "cadena de recursos" no
pasa por `rc.exe` en absoluto: son **cabeceras C generadas** e incrustadas por
`#include`, producidas por BITAPP y DIBAPP desde `.bmp`/`.cur`/`.ico`/`.dib`.

Verifiqué que las 52 entradas de `OPUS_BITAPP_BITMAP_HEADERS` +
`OPUS_BITAPP_FIGURE_HEADERS` y los 7 cursores tienen su archivo fuente presente en
`Opus/resource/` **con la mayúscula/minúscula correcta**, y que hay 37 entradas `.dib`.
No hay archivos faltantes en esa parte.

### 3.3 Qué puede asumir `wrc`

**`wrc` compila `port/word1.rc` tal cual, sin modificaciones.** Verificado:

```
$ wrc -I src/port -o word1.res src/port/word1.rc
FYI: Starting parse
FYI: Writing .res-file      → 28140 bytes
```

Maneja `#include <windows.h>`, `RT_MANIFEST`, `VS_VERSION_INFO`, y **también las rutas
con barra invertida** (`"assets\\word95-toolbar.bmp"`).

Dos restricciones descubiertas:

1. **`winegcc` rechaza archivos `.rc` en su línea de órdenes**:
   `winegcc: Can't compile .rc file at the moment`. El flujo obligatorio es
   `wrc → .res → winegcc`. Verificado que `winegcc -mwindows -o smoke.exe smoke.c word1.res`
   enlaza correctamente y produce el par `.exe` (stub sh) + `.exe.so` (ELF).
2. **`project(… LANGUAGES RC)` falla en Linux**:
   `No CMAKE_RC_COMPILER could be found.` El preset de Ninja debe declarar sólo
   `C CXX` y manejar `wrc` con `add_custom_command`.

---

## 4. Supuestos LP64 vs LLP64 incrustados en el código

Éste es el punto que no aparece en ningún inventario de "constructos MSVC" y que sin
embargo es el de mayor alcance.

### 4.1 `long` cambia de tamaño

```
$ winegcc -o sz.exe sz.c && ./sz.exe
long=8 LONG=4 DWORD=4 ptr=8 int=4 wchar_t=2
```

Wine hace lo correcto: `LONG` y `DWORD` son de 4 bytes. Pero **`long` desnudo es de 8
bytes**, contra 4 bajo MSVC x64. En el árbol compilado hay **1254 apariciones de `long`
repartidas en 206 archivos**. Los concentradores son
`port/original/opus_asm_native_adapters.cpp` (54), `Opus/CLIPBRD2.C` (37),
`Opus/wordtech/plc.c` (29), `Opus/wordtech/savefast.c` (25), `Opus/wordtech/file.h` (25).

Peor: dos cabeceras redefinen los tipos de Windows con `long` propio:

- `Opus/lib/qwindows.h:119` — `typedef unsigned long DWORD;`
- `OpusEtAl/tools/src/bitapp.h:29` — `typedef unsigned long DWORD;`

`qwindows.h` **sí es alcanzable** desde el motor: `Opus/interp/sym.c:6` hace
`#include <qwindows.h>` y `sym.c` está en la lista de fuentes.

No todas las 1254 apariciones son peligrosas — muchas son variables locales de conteo.
Las que importan son las que aparecen en `struct`s serializadas al formato de archivo
de Word, en las tablas PLC, y en los cálculos de `sizeof`. Requiere auditoría dirigida,
no un `sed` global.

### 4.2 `wchar_t` de 2 bytes rompe libstdc++

winegcc siempre pasa `-fshort-wchar`. libstdc++ y glibc están compilados con
`wchar_t` de 4 bytes. Los símbolos enlazan (el *mangling* de `wchar_t` no depende del
tamaño), de modo que **el error es silencioso en tiempo de enlace y visible sólo en
ejecución**:

```cpp
std::wstring w = L"abc";
std::cout << w.size();     // imprime 175, no 3
```

(verificado ejecutando el binario Winelib resultante)

Alcance: acotado y enteramente dentro de `src/port/`. Los sitios que usan funciones
anchas de la CRT/STL son `opus_win95_chrome.cpp` (13), `opus_word1_ui_test.cpp` (~21),
`opus_original_startup_probe.cpp` (3). El uso de `WCHAR`/`L""` con APIs `…W` de Wine
es correcto y no requiere cambios.

---

## 5. Supuestos del generador de Visual Studio

### 5.1 En `CMakePresets.json`

| Línea | Supuesto | Impacto |
|---|---|---|
| `"generator": "Visual Studio 17 2022"` | Generador multiconfiguración | Los tres presets son inservibles en Linux. Hay que **añadir** presets Ninja sin borrar éstos. |
| `"architecture": {"value":"x64","strategy":"set"}` | Sólo VS/Xcode aceptan `architecture` | Ninja lo ignora o falla. |
| `"CMAKE_INTERMEDIATE_DIR_STRATEGY": "SHORT"` | Variable de caché específica de VS | Inocua. |
| `"binaryDir": "${sourceDir}/../out"` | — | Portable. |
| `buildPresets` con `"configuration": "Debug"/"Release"` | Multiconfiguración | Los presets Ninja necesitan `CMAKE_BUILD_TYPE`. |

### 5.2 En `src/CMakeLists.txt` — bloqueantes duros

1. **Línea 5-7** — `if(NOT WIN32) message(FATAL_ERROR …)`. Bloqueo explícito.
2. **Línea 3** — `LANGUAGES … RC`. Falla en Linux (§3.3).
3. **Líneas 59-64** — `$ENV{ProgramFiles(x86)}` + `${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}`
   para localizar `Windows.h` del Windows Kits SDK, con `FATAL_ERROR` si no existe.
   `CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION` **sólo la define el generador de VS**.
   Alimenta `port/original/opus_windows_sdk.h.in`, cuyo único contenido es
   `#include "@OPUS_WINDOWS_SDK_HEADER@"`.
   El mecanismo es sano: `port/original/windows.h` (con `#pragma once`) hace de
   pantalla para que `Opus/windows.h` no eclipse el SDK moderno, y redirige a
   `opus_windows_sdk.h`. **Para Winelib basta que la plantilla apunte a
   `/usr/include/wine/windows/windows.h`.** Lo verifiqué: con esa sustitución la
   cadena de inclusión funciona (sin ella se produce un ciclo silencioso vía
   `#pragma once` que deja `HMODULE`, `BOOL`, `FARPROC` sin definir).
4. **Líneas 79-89** — `add_custom_command` que invoca
   `powershell -File cmake/GenerateElxStid.ps1`.
5. **Línea 349** — `-P cmake/GenerateMenuHelpHeader.cmake`.

### 5.3 `src/cmake/` no existe

```
$ git log --all -- 'src/cmake/*'      → (vacío)
$ git ls-files | grep -iE '\.(ps1|cmake)$'
src/port/tools/make_win95_toolbar_sprite.ps1
```

**Ni `GenerateElxStid.ps1` ni `GenerateMenuHelpHeader.cmake` existen en ningún commit
de este repositorio.** El build tal como está versionado no puede generarse ni siquiera
en Windows. Esto debe resolverse antes de cualquier trabajo de port — y de paso elimina
la dependencia de PowerShell, porque ambos scripts habrá que reescribirlos.

> **Actualizado tras la Fase 0:** sí existen fuera del repositorio, en un pull request
> no fusionado del upstream. Ver «Nota sobre el origen de los dos scripts» al final.

### 5.4 Supuestos inocuos

`FOLDER`, `VS_GLOBAL_VcpkgApplocalDeps`, `VS_DEBUGGER_WORKING_DIRECTORY` y los bucles
`RUNTIME_OUTPUT_DIRECTORY_${config_upper}` son propiedades ignoradas por Ninja. No
requieren acción y deben conservarse para no romper los presets de VS.

### 5.5 Dos trampas de la línea de órdenes de winegcc

- **`winegcc` descarta `-x`.** Verificado: `winegcc -v -c -x c t0.c` no propaga la
  bandera a gcc. Esto importa porque el motor contiene **17 fuentes con extensión `.C`
  mayúscula** (`CLIPBORD.C`, `CLIPBRD2.C`, `CREATE2.C`, `EDIT.C`, `DDESRVR.C`,
  `DLBENUM.C`, `FILE2.C`, `FIELDCMD.C`, `GRSPEC.C`, `LOADFONT.C`, `PIC2.C`, `RTFIN.C`,
  `RTFOUT.C`, `RTFRARE.C`, `SCREEN2.C`, `SYSCHG.C`, `SPELL.C`), que gcc trata como C++.
  `CMakeLists.txt:670` ya las marca `LANGUAGE C` para MSVC, pero eso sólo elige el
  compilador, no el front-end. Sin remedio, esas 17 TUs generan ~2800 errores.
  La solución practicable es materializar enlaces simbólicos en minúscula dentro del
  árbol de compilación (paso de `src/port/`).
- **`-mno-cygwin` (cabeceras msvcrt) es incompatible con libstdc++.** En ese modo
  `_stricmp`, `_snprintf_s`, `<direct.h>`, `<io.h>` están disponibles, pero
  `#include <string>` falla con conflictos en `<cwchar>`. Como `src/port/` usa la STL
  ampliamente (31 archivos), **hay que quedarse en el modo glibc por defecto y aportar
  los ~6 símbolos de la CRT de Microsoft desde `src/port/`.**
- **`#define native` de `opus_x64_compat.h:135` envenena `<bit>` de libstdc++**
  (`enum class endian { little, big, native }`). Es la causa exclusiva del fallo de
  `opus_asm_file2.cpp`, `opus_asm_filewin.cpp` y `opus_asm_misc.cpp`. El autor ya usó
  el remedio en `port/tools/opus_cabi_tool.cpp:3` (`#undef native`). Lo mismo aplica a
  `#define string` y `#define sys`.

---

## 6. Verificación del entorno

```
$ winegcc --version        gcc (GCC) 16.1.1 20260515 (Red Hat 16.1.1-2)
$ winebuild --version      winebuild version 11.0
$ wrc --version            Wine Resource Compiler version 11.0
$ cmake --version          cmake version 4.3.0
$ ninja --version          1.13.2
$ wine --version           wine-11.0 (Staging)
```

Rutas: `/usr/bin/winegcc`, `/usr/bin/winebuild`, `/usr/bin/wrc`, `/usr/bin/cmake`,
`/usr/bin/ninja`. Paquetes `wine-devel-11.0-3.fc44.x86_64` y `wine-core-11.0-3.fc44.x86_64`.

Cabeceras confirmadas: `/usr/include/wine/windows/` (windows.h, commdlg.h, shellapi.h,
**dbghelp.h**) y `/usr/include/wine/msvcrt/` (direct.h, malloc.h, io.h, process.h).
`dbghelp` importa porque `WORD1` enlaza contra él y
`port/original/opus_original_startup_probe.cpp` usa `SymInitialize`, `SymFromAddr`,
`StackWalk64`, `SymGetLineFromAddr64` y `CaptureStackBackTrace` (13 sitios). Wine
provee la biblioteca; la resolución de símbolos sobre un `.exe.so` ELF es de calidad
incierta, pero es código de diagnóstico y no bloquea.

### 6.1 CMake + Ninja + winegcc funciona sin capa de adaptación

```
$ cmake -G Ninja -DCMAKE_C_COMPILER=winegcc -DCMAKE_CXX_COMPILER=wineg++ -S . -B build
-- Check for working C compiler: /usr/bin/winegcc - works
-- Check for working CXX compiler: /usr/bin/wineg++ - works
$ cmake --build build
[2/2] Linking C executable smoke.exe
    build/smoke.exe      POSIX shell script  (stub)
    build/smoke.exe.so   ELF 64-bit LSB shared object, x86-64
```

Nota: CMake reporta `Detecting C compiler ABI info - failed`, así que no puede deducir
las bibliotecas implícitas de enlace. Conviene un archivo de *toolchain* que lo
suprima. `$<TARGET_FILE:…>` apunta al stub `.exe`, que es ejecutable, de modo que
`ctest` seguirá funcionando sin cambios en las 15 pruebas registradas.

### 6.2 Censo de compilación del motor

Metodología: las 207 fuentes de `add_library(opus_original_engine STATIC …)`,
compiladas individualmente con winegcc/wineg++, con las cabeceras generadas ya
producidas (MKCMD parcheado + BITAPP + DIBAPP), un directorio de compatibilidad de
mayúsculas para las 12 cabeceras de §7, `opus_windows_sdk.h` apuntando a Wine, y
enlaces en minúscula para las 17 fuentes `.C`.

| Configuración | Compilan | Fallan |
|---|---:|---:|
| `-std=gnu89 -funsigned-char` (muestra de 20) | 8 / 20 | 12 |
| `+ -fms-extensions -fpermissive` (misma muestra) | 12 / 20 | 8 |
| `+ -fms-extensions -fpermissive` (árbol completo) | **154 / 207** | 53 |

Los 53 fallos restantes producen **202 errores en total**, concentrados como se
describe en §1.6. Noventa de esos 202 provienen de una única cabecera (`Opus/keys.h`).

---

## 7. Sensibilidad a mayúsculas

Análisis de todas las directivas `#include` del árbol compilado contra el contenido
real del disco:

| `#include` | Archivo en disco | Archivos afectados |
|---|---|---:|
| `debug.h` | `Opus/DEBUG.H` | **202** |
| `screen.h` | `Opus/SCREEN.H` | 111 |
| `rareflag.h` | `Opus/RAREFLAG.H` | 50 |
| `resource.h` | `Opus/RESOURCE.H` | 49 |
| `pic.h` | `Opus/PIC.H` | 29 |
| `rtf.h` | `Opus/RTF.H` | 6 |
| `rtftbl.h` | `Opus/RTFTBL.H` | 4 |
| `spell.h` | `Opus/SPELL.H` | 2 |
| `pic3.c` | `Opus/PIC3.C` | 1 |
| `rtfin2.c` | `Opus/RTFIN2.C` | 1 |
| `saveFast.h` | `Opus/wordtech/savefast.h` | 1 |
| `Windows.h` | `port/original/windows.h` | 1 |

A esto se suman las cabeceras **generadas**: MKCMD emite `IBCM.H`, `RGBCM.H`,
`OPUSCMD2.H`, `OPUSMENU.H`, `MENUHELP.TXT` en mayúsculas, mientras que las fuentes
incluyen `ibcm.h` (3 sitios), `rgbcm.h` (1), `menuhelp.h` (1). Como ese directorio es
territorio del port, se arregla en el paso de generación.

Renombrar en `src/Opus/` violaría la restricción del proyecto. La solución limpia es un
directorio `${CMAKE_CURRENT_BINARY_DIR}/generated/case-shim/` con enlaces simbólicos,
antepuesto a la ruta de inclusión. No toca ni un byte del árbol original.

---

## 8. Plan por fases

Cada fase tiene un criterio de éxito verificable por orden. Ninguna fase empieza sin
que la anterior cumpla el suyo.

### Fase 0 — Reconstruir lo que falta del build (bloqueante)

Escribir `src/cmake/GenerateMenuHelpHeader.cmake` (a partir de `MENUHELP.TXT` que
MKCMD ya produce) y sustituir `GenerateElxStid.ps1` por un script CMake equivalente que
extraiga la carga de pantalla oculta de `OpusEtAl/tools/src/mergeelx.c` hacia
`elxinfo.h`. Referencia: `src/port/original/elxinfo.h` versionado muestra el formato
esperado. Sin PowerShell.

> **Éxito:** `cmake -S src -B out --preset x64-debug` avanza más allá de la resolución
> de dependencias en una máquina Windows *o* la configuración Ninja de la Fase 2 no
> reporta reglas faltantes. Ambos scripts existen y son invocables desde `cmake -P`.

### Fase 1 — Herramientas de host en Linux

Parche mínimo y guardado de `mkcmd.c` (5 sitios de *cast as lvalue*, `#ifdef __GNUC__`)
más un `#define _stricmp strcasecmp` aportado desde `src/port/`. Envolver BITAPP para
que reciba rutas relativas desde un `WORKING_DIRECTORY`, y diagnosticar el fallo de EOF
como el problema `unsigned long` de `bitapp.h:29`.

> **Éxito:** `mkcmd`, `mkdlg`, `mergeelx`, `bitapp`, `opus_dibapp_tool` compilan con
> `gcc`/`g++` nativos y, ejecutados, producen byte a byte las mismas 9 cabeceras de
> comandos, 52 cabeceras BITAPP, 7 cursores y 37 `.hb` de DIBAPP que ya sé que produce
> la cadena parcial. Comparación con `sha256sum` contra una ejecución de referencia.

### Fase 2 — Andamiaje CMake/Ninja para Winelib

Presets `linux-winelib-debug` / `linux-winelib-release` con Ninja. Un archivo de
*toolchain* que fije `winegcc`/`wineg++`, elimine `RC` de `project()` cuando
`NOT WIN32`, y sustituya la puerta `FATAL_ERROR` de la línea 5 por una que acepte
Winelib. Localización del SDK: `opus_windows_sdk.h.in` apuntando a Wine. Directorio de
compatibilidad de mayúsculas (12 enlaces) y enlaces en minúscula para las 17 fuentes
`.C`. Banderas del motor: `-std=gnu89 -funsigned-char -fms-extensions -fpermissive`.
Los presets de VS quedan intactos.

> **Éxito:** `cmake --preset linux-winelib-debug` configura sin errores, y
> `ninja opus_x64_runtime` enlaza la biblioteca estática. Los presets `x64-debug` /
> `x64-release` siguen presentes y sintácticamente válidos.

### Fase 3 — Compilar el motor

Cerrar los 186 errores restantes en 51 archivos (cifra tras la Fase 0; ver la sección
«Fase 0 — ejecutada el 2026-08-09»). Prioridad por rendimiento: primero `Opus/keys.h`
(90 errores de un solo golpe), luego los 21 *cast as lvalue*, luego las ~22
discordancias de prototipos K&R, y la discordancia de tipos de
`Opus/elxprocs.c:82,89` que dejó pendiente la Fase 0. Los `#undef native` /
`#undef string` en los tres
`.cpp` de `port/original/` son de `src/port/`, sin justificación necesaria. Todo cambio
en `src/Opus/` se aísla con `#if defined(__GNUC__) && !defined(_MSC_VER)` y se registra
en `docs/port-linux/01-cambios-en-opus.md` con su justificación.

> **Éxito:** `ninja opus_original_engine` produce `libopus_original_engine.a` con las
> 207 TUs. El recuento de líneas modificadas dentro de `src/Opus/` y `src/OpusEtAl/`
> es inferior a 60 y cada bloque está guardado.

### Fase 4 — Enlace, recursos y exportaciones

Generar un `.spec` de winebuild con las 427 exportaciones a partir de la misma pasada
de MKCMD que hoy produce `opuscmd_native.inc` (misma fuente de verdad, un emisor
adicional). Compilar `port/word1.rc` con `wrc` en un `add_custom_command` y pasar el
`.res` al enlace.

> **Éxito:** existe `WORD1.exe.so` como ELF x86-64, y `WORD1.exe` como stub ejecutable.
> `winedump`/`nm` muestran los 427 símbolos `Cmd*` exportados. Un arranque
> instrumentado confirma que `ResolveCommandAddress("CmdHelp")` devuelve un puntero no
> nulo.

### Fase 5 — Auditoría LP64

Recorrer los usos de `long` que participan de estructuras serializadas, tablas PLC y
cálculos de `sizeof`: `Opus/wordtech/plc.c`, `savefast.c`, `file.h`, `CLIPBRD2.C`,
`port/original/opus_asm_native_adapters.cpp`, y la redefinición de `DWORD` en
`qwindows.h:119`. Sanear la barrera de cadenas anchas de libstdc++ en los ~5 archivos
de `src/port/`.

> **Precedente concreto, no advertencia genérica:** `OpusEtAl/tools/src/bitapp.h:29`
> ya rompió en la práctica por este patrón exacto — `typedef unsigned long DWORD;` sin
> condicionar por arquitectura, 4 bytes bajo MSVC x64 (LLP64) pero 8 bajo GCC x64
> (LP64). No fue una deducción teórica: al ejecutar BITAPP en la Fase 1, `sizeof(BITMAP)`
> pasó de 14 a 18 bytes, `fread()` leyó 4 bytes de más en cada recurso, y 46 de 51
> cabeceras de mapa de bits salieron corruptas antes de fallar con «Unexpected End Of
> File». Se corrigió allí (`DWORD` → `uint32_t` sólo bajo `OPUS_X64_TOOL` + GCC, MSVC
> intacto) — ver la sección «Fase 1» más abajo. `Opus/lib/qwindows.h:119` es la misma
> construcción textual (`typedef unsigned long DWORD;`), en el árbol que sí importa
> tocar con más cuidado por su alcance (`Opus/interp/sym.c` la alcanza). Buscar primero
> cualquier campo `DWORD`/`long` desnudo que participe de un `fread`/`fwrite` o de un
> `sizeof` comparado contra una constante de formato de archivo — ese fue exactamente
> el mecanismo de falla en BITAPP, y es más probable que se repita en `plc.c` y
> `savefast.c` (tablas PLC serializadas) que en código que sólo cuenta o itera.

> **Éxito:** una prueba de aserciones estáticas nueva en `src/port/` verifica el
> `sizeof` de las estructuras de formato de archivo de Word contra los valores del
> Win16 original, y pasa bajo Winelib. La suite `opus_x64_runtime_test` y
> `opus_original_plc_test` corren bajo `ctest`.

### Fase 6 — Pruebas y arranque

> **Éxito:** `ctest` ejecuta las 15 pruebas. Las 7 que no dependen de la interfaz
> (`strtbl`, `x64_runtime`, `sttb`, `plc`, `sdm_cab`, `command`, `word1_port_smoke`)
> pasan. Las 8 pruebas de interfaz compilan y se registran; su ejecución requiere
> sesión gráfica y se documenta como tal, no se declara aprobada sin evidencia.

---

## Fase 0 — ejecutada el 2026-08-09

### Entregables

| Archivo | Qué hace |
|---|---|
| `src/cmake/GenerateMenuHelpHeader.cmake` | Reconstruye `menuhelp.h` desde `MENUHELP.TXT`. Define `OPUS_X64_MENU_HELP_STRING(iidstr)`, que `Opus/menuhelp.c:287` referencia y que ninguna cabecera del árbol definía. |
| `src/cmake/GenerateElxInfoHeader.cmake` | Reconstruye `elxinfo.h` extrayendo la carga de pantalla oculta de `OpusEtAl/tools/src/mergeelx.c`. Sustituye a `GenerateElxStid.ps1`. Sin PowerShell. |
| `src/CMakeLists.txt` (líneas 79-89) | Único cambio: la invocación de `powershell -File …GenerateElxStid.ps1` pasa a `${CMAKE_COMMAND} -P …GenerateElxInfoHeader.cmake`, y su `DEPENDS` deja de nombrar un archivo inexistente. |

`src/Opus/` y `src/OpusEtAl/` no fueron tocados.

### Verificación del criterio de éxito

**«Ambos scripts existen y son invocables desde `cmake -P`.»** Sí. Ambos se ejecutan
de forma independiente y producen sus cabeceras. Se comprobaron además cuatro rutas de
error: `INPUT` ausente, `INPUT` inexistente, entrada sin bloque STID, y línea de datos
corrupta en `MENUHELP.TXT`. Las cuatro abortan con un mensaje accionable en lugar de
degradar en silencio — lo que importa en `MENUHELP.TXT`, donde descartar una línea
desplazaría todos los índices `iidstr` posteriores.

**«La configuración Ninja de la Fase 2 no reportará reglas faltantes por causa de estos
dos archivos.»** Verificado con un banco de pruebas que reproduce literalmente las dos
reglas `add_custom_command` del `CMakeLists.txt` real, configurado con `-G Ninja`:
ambas cabeceras se generan y una segunda corrida informa `no work to do`. Se ejecutó
también un **control negativo**: apuntando la regla al `.ps1` ausente, Ninja falla con
`missing and no known rule to make it` — es decir, la prueba detecta exactamente el
fallo que se quería descartar. La escritura condicional de ambos scripts preserva la
marca de tiempo cuando el contenido no cambia, de modo que tocar `mergeelx.c` no
arrastra recompilaciones aguas abajo ni deja la regla permanentemente sucia.

### Fidelidad de la extracción, comprobada por ejecución

El bloque STID se contrastó contra el MERGEELX real: se compiló `mergeelx.c` con su
`main` neutralizado y un controlador que llama a `WriteElxInfo()` directamente. El
`ELXINFO.H` resultante y el generado por el script tienen **102 líneas cada uno y son
idénticos byte a byte salvo la única línea documentada**:

```
< csconst char rgksp [] = StringMap("SUPO", 0, 1);
> csconst char rgksp [] = "SUPO";
```

`StringMap` era una extensión del compilador C de 16 bits y no es un inicializador
estático válido en C nativo; es la misma traducción que el autor del port ya aplicó en
`Opus/renum.c:77`, `Opus/FILE2.C:560` y `Opus/automcr.h:15`. Que la forma correcta sea
`sz` (y no la contada) se confirmó descifrando la carga: `sizeof(rgksp)` es 5, el
disparador es `'S'` y el acorde `'U' 'P' 'O'`, coherente con el bucle de
`Opus/eldlg.c:890-893`. Aplicando el XOR `'9'` de `eldlg.c:907` sobre `mpstiderc` con
los desplazamientos de `rgstid` se obtiene texto legible
(`* Microsoft Word for Windows *`), lo que valida la transcripción de las tres tablas y
el alineamiento de los índices, no sólo su copia literal.

Para `menuhelp.h`, las 208 cadenas producidas coinciden exactamente con las que genera
el script del upstream (ver la nota siguiente).

### Efecto medido sobre la compilación del motor

Mismo censo y mismas banderas de §6.2:

| | Compilan | Fallan | Errores |
|---|---:|---:|---:|
| Antes de la Fase 0 | 154 / 207 | 53 | 202 |
| Después de la Fase 0 | **156 / 207** | 51 | **186** |

Los dos módulos que pasan a compilar son exactamente `Opus/menuhelp.c` y
`Opus/eldlg.c`, los dos consumidores de estas cabeceras. Sin regresiones.

`Opus/elsubs.c` **sigue fallando**, pero pasa de 9 errores a 1. Los 8 que desaparecen
eran símbolos no declarados (`rgeldi`, `rgichName`, `rgchElkNames`, `mpelkistName`); el
que queda es una discordancia de tipos preexistente en el código de Microsoft —
`Opus/elxprocs.c:82` declara `int far * pist` mientras MERGEELX emite
`csconst unsigned mpelkistName []`, y `elxprocs.c:89` resta ambos punteros. MSVC lo
tolera; GCC no. Es un elemento ordinario de la Fase 3 y toca `src/Opus/`, fuera del
alcance de esta fase.

### Nota sobre el origen de los dos scripts

**Sí existieron fuera del repositorio, y fue posible determinarlo.**

- Dentro de este repositorio no hay rastro: `git log -S` sobre todas las referencias
  sitúa las menciones únicamente en el commit inicial `a1c4a1f` (una importación
  aplanada de 1113 archivos), y ningún blob de la historia contiene los archivos. No
  existe configuración de CI de ningún tipo — ni `.github/`, ni YAML, ni AppVeyor, ni
  Azure Pipelines.
- El upstream `jmarshall23/msword` tiene su `main` en `ac5472e`, **el mismo SHA que el
  HEAD local**, de modo que su árbol publicado tampoco los contiene.
- Sin embargo, el pull request **#3** de ese repositorio, abierto y sin fusionar,
  aporta precisamente `src/cmake/GenerateElxStid.ps1` (189 líneas) y
  `src/cmake/GenerateMenuHelpHeader.cmake` (104 líneas), más un stub de enlace en
  `src/port/original/opus_x64_runtime_test.cpp`.

La conclusión es que **el árbol publicado nunca compiló tal como está**, y que la
corrección estaba propuesta pero no integrada. Es un patrón que este repositorio ya
había mostrado: el commit `ac5472e` («Added missing file.») añadió a mano
`opus_word1_ui_test.cpp`, otro archivo que el `CMakeLists.txt` referenciaba y que la
importación inicial había dejado fuera.

Lo que **no** fue posible determinar es si el autor original disponía de estos scripts
en su máquina antes de que el PR los propusiera.

Consecuencias para el trabajo entregado:

1. La versión PowerShell se ejecutó y se comparó. Mi generador de `menuhelp.h` produce
   las **mismas 208 cadenas, byte a byte**. La extracción del bloque STID coincide en
   enfoque, en la traducción de `StringMap` y en las comprobaciones de los cuatro
   símbolos.
2. El PR resolvía algo que yo había descartado por considerarlo fabricación de datos:
   emitir tablas EL **neutras** bajo `#ifdef elkAppMac`, dimensionadas por
   `elxdefs.h` y con contenido cero, rodeando el miembro flexible de `ELDI` con un
   registro de igual disposición y una macro `#define rgeldi`. Es correcto y no inventa
   datos de diálogo. **Lo incorporé**, con atribución en el encabezado del script; de
   ahí la mejora de `elsubs.c` de 9 errores a 1.
3. También adopté del PR la comprobación de rango en `OPUS_X64_MENU_HELP_STRING`.
4. Se conservan dos divergencias deliberadas: el generador de `elxinfo.h` es CMake y no
   PowerShell (requisito del port), y la tabla de ayuda se tipa como `CHAR` en lugar de
   `char`, porque `CchCopySz` recibe `const CHAR *` y con GCC la diferencia produce
   aviso de puntero incompatible. Verificado: ambas cabeceras compilan sin avisos.

**Queda a su criterio** si prefiere tomar el PR #3 del upstream tal cual en lugar de
esta implementación. Mi recomendación es conservar la versión CMake, porque el
requisito «sin PowerShell» es del port y el PR no cambia esa dependencia; pero la
decisión es suya y el PR es la propuesta del autor original.

Un detalle para la Fase 5: la carga STID incluye `csconst long rgrgb []`, que en LP64
ocupa 8 bytes por entrada frente a 4 bajo MSVC. Se emite con fidelidad a MERGEELX. Los
valores caben en 32 bits y los sitios de uso (`Opus/eldlg.c:1132,1166`) convierten a
`COLORREF`, así que funciona; queda anotado para la auditoría de §4.1.

---

## Fase 1 — ejecutada el 2026-08-09

### Alcance y entregables

Las cinco herramientas de host —`mkcmd`, `mkdlg`, `mergeelx`, `bitapp`,
`opus_dibapp_tool`— compilan y se ejecutan como binarios Linux nativos, con `gcc`/`g++`
directos (sin CMake todavía; el andamiaje Ninja es Fase 2).

| Archivo | Cambio | Alcance |
|---|---|---|
| `src/OpusEtAl/tools/src/mkcmd.c` | 5 sitios de *cast as lvalue* reemplazados por las macros `OPUS_POSTINC_READ`/`OPUS_POSTINC_WRITE`, definidas bajo `#if defined(__GNUC__) && !defined(_MSC_VER)` con reproducción exacta del idioma original de Microsoft para MSVC. Un `#include "opus_host_compat.h"` guardado igual. | `src/OpusEtAl/`, aislado |
| `src/OpusEtAl/tools/src/mkdlg.c` | Un `#include` guardado + `#define strcmpi _stricmp`, mismo patrón que ya usaba `mkcmd.c` para el mismo hueco de la CRT de Microsoft. | `src/OpusEtAl/`, aislado |
| `src/OpusEtAl/tools/src/bitapp.h` | `DWORD` pasa a `uint32_t` bajo `#if defined(OPUS_X64_TOOL) && defined(__GNUC__) && !defined(_MSC_VER)`; MSVC conserva `unsigned long` sin cambios. | `src/OpusEtAl/`, aislado |
| `src/port/tools/opus_host_compat.h` | Nuevo. Define `_stricmp` como `strcasecmp` para GCC. | `src/port/` |

`src/Opus/` no fue tocado. `src/OpusEtAl/mergeelx.c` y `opus_dibapp_tool.cpp` no
necesitaron ningún cambio: compilan y corren tal cual.

### Un problema no anticipado: `mkdlg.c` tenía el mismo hueco de `_stricmp` que `mkcmd.c`, con otro nombre

El plan aprobado sólo mencionaba `_stricmp` en `mkcmd.c`. Al enlazar `mkdlg` apareció
`referencia a 'strcmpi' sin definir`: `mkdlg.c:718` llama a `strcmpi()` directamente,
confiando en que el CRT de MSVC lo declare como alias obsoleto de `_stricmp()` — cosa
que hace sin que el código fuente lo pida. GCC no tiene ninguno de los dos nombres. Es
el mismo hueco de la CRT de Microsoft, en otra grafía. Se resolvió con el mismo patrón
que ya usaba `mkcmd.c` (`#define strcmpi _stricmp`), y `opus_host_compat.h` quedó
diseñado para exponer únicamente el primitivo real de Microsoft (`_stricmp`), dejando
que cada archivo mapee su propia grafía sobre él — así se evita que dos archivos que
incluyen la misma cabecera compartida se pisen la macro `strcmpi` entre sí.

### Un bug real encontrado y corregido: `bitapp.h:29`, no sólo diagnosticado

La Fase 0 había señalado `typedef unsigned long DWORD;` (`bitapp.h`, entonces línea 29)
como sospechoso pero sin confirmar. Al ejecutar BITAPP de verdad sobre los 51 recursos
de mapa de bits, **46 de 51 fallaron** con `Unexpected End Of File Reached in Input
file!`, produciendo además datos corruptos (no vacíos) antes de fallar — una regresión
silenciosa peligrosa si no se hubiera detectado por código de salida.

Causa confirmada: `struct BITMAP` (`bitapp.h:40-51`) serializa `bmBits` como `DWORD`,
campo Win16 de 4 bytes que el código siempre descarta. Bajo MSVC x64 (LLP64), `long`
sigue siendo de 4 bytes y el `typedef` es exacto. Bajo GCC x64 (LP64), `long` es de 8
bytes: `sizeof(BITMAP)` pasaba de 14 a 18 bytes, y `fread(&bm, sizeof(BITMAP), 1,
fpOrig)` leía 4 bytes de más en cada recurso, desalineando todo lo que venía después.

Corrección: `DWORD` pasa a `uint32_t` sólo bajo `OPUS_X64_TOOL` **y** GCC no-MSVC — el
build MSVC x64 (Windows) queda byte por byte sin cambios, porque ahí el bug no existe.

**Verificación independiente, sin depender de BITAPP mismo:** se re-leyeron los 46
`.bmp` con Python usando `struct.unpack` directo sobre los bytes crudos del archivo
(`<hhhhBB` en el desplazamiento 2, es decir el layout Win16 real de 14 bytes), y se
comprobó que `2 (firma) + 14 (cabecera) + bmWidthBytes×bmHeight` coincide **exactamente**
con el tamaño de cada uno de los 46 archivos — cero discrepancias. Esta prueba no usa
BITAPP en absoluto; deriva la corrección desde el formato de archivo documentado en el
propio código de `DumpBitmapParameters`. Se hizo lo mismo para los 5 íconos/cursores de
figura (`RCI`, 12 bytes, todos campos `short`, sin `DWORD` — nunca tuvieron el bug) y
para los 7 cursores de `Opus/resource/*.cur`, confirmando que el consumo de bytes de
`DumpFigureParameters` + dos llamadas a `DumpBits` (máscaras AND/XOR) no excede el
tamaño de archivo en ninguno de los 12 casos.

### Por qué la comparación sha256 contra la Fase 0 no cubre BITAPP igual que el resto

Al revisar qué había quedado en el directorio de reconocimiento de la Fase 0, resultó
que **no era una referencia válida para BITAPP**: contenía 38 archivos con nombres
truncados que no coinciden con ninguno de los 58 nombres reales que exige
`CMakeLists.txt` (p. ej. `8iparal3.hb` en vez de `8paralig.hg`), y el único nombre que
sí coincidía (`8hdr.hb`) resultó ser precisamente una salida generada por el mismo
build defectuoso — 1418 bytes de datos corruptos, no la salida correcta. Se confirmó
reconstruyendo un binario `bitapp` desde el `bitapp.h` anterior al parche de esta fase
(`git show HEAD:...`) y ejecutándolo contra el mismo `.bmp`: produce el mismo archivo
corrupto de 1418 bytes, byte a byte idéntico al que había en el directorio de
reconocimiento. Es decir, la Fase 0 nunca llegó a generar una ejecución de referencia
limpia para BITAPP — sólo para MKCMD, MENUHELP y el bloque STID de MERGEELX, donde la
comparación sha256 sí es válida y se reconfirmó sin regresión (ver más abajo).

Por eso, para BITAPP la verificación de esta fase no es "coincide con la Fase 0" sino
las tres pruebas independientes ya descritas: derivación byte a byte desde el formato
de archivo documentado (arriba), y reproducibilidad determinista (abajo).

### Verificación del criterio de éxito

**9 cabeceras de comandos (MKCMD + `menuhelp.h`):** las 9 —`opuscmd.h`, `IBCM.H`,
`RGBCM.H`, `OPUSCMD2.H`, `OPUSMENU.H`, `MENUHELP.TXT`, `opuscmd.asm`,
`opuscmd_native.inc`, `menuhelp.h`— dan **sha256 idéntico** contra la ejecución de
referencia de la Fase 0. El parche de los 5 sitios *cast as lvalue* no alteró un solo
byte de la salida.

**58 archivos de BITAPP (51 cabeceras + 7 cursores):** los 58 se generan sin fallos
(antes del fix: 46/51 cabeceras de mapa de bits fallaban). Verificados por derivación
independiente desde el formato de archivo crudo (arriba) y por reproducibilidad: dos
ejecuciones completas desde binarios independientes dan `diff -rq` vacío.

**37 `.hb` de DIBAPP:** los 37 se generan sin fallos, sin necesidad de ningún cambio en
`opus_dibapp_tool.cpp`. Dos ejecuciones dan `diff -rq` vacío. No hay una herramienta
DIBAPP original de Microsoft con la que comparar — el archivo nunca la incluyó
(§2 de esta misma sección de reconocimiento); `opus_dibapp_tool.cpp` es una
reimplementación del autor del port, no un port de código heredado.

**MKDLG:** compila y enlaza; invocado sin argumentos imprime su mensaje de uso y sale
con código 1, comportamiento correcto. Su ejecución funcional completa (`mkdlg @elx.txt
dlgcheck.h`) sigue bloqueada por la ausencia de los `.elx` de entrada — el mismo hueco
documentado en §3.1, no un problema nuevo de esta fase.

**MERGEELX:** reconstruido y ejecutado con el mismo arnés de verificación de la Fase 0;
el bloque STID que produce coincide con el de `GenerateElxInfoHeader.cmake` salvo la
traducción de `StringMap` ya documentada. Sin regresión.

### Estado del árbol tras esta fase

```
 M src/OpusEtAl/tools/src/bitapp.h   (+15/-0, aislado bajo __GNUC__)
 M src/OpusEtAl/tools/src/mkcmd.c    (+36/-6, aislado bajo __GNUC__)
 M src/OpusEtAl/tools/src/mkdlg.c    (+9/-0,  aislado bajo __GNUC__)
?? src/port/tools/opus_host_compat.h (nuevo)
```

`src/Opus/` sin cambios. Las tres ediciones a `src/OpusEtAl/` están justificadas en
esta sección, aisladas con guardas de preprocesador, y no alteran el binario que
produciría MSVC.

---

## Apéndice: qué no está en el árbol

Para evitar que se busque lo que no existe:

- `src/cmake/GenerateElxStid.ps1`, `src/cmake/GenerateMenuHelpHeader.cmake` — nunca
  versionados en este repositorio; existen en el PR #3 del upstream y fueron
  reconstruidos en la Fase 0 (ver «Nota sobre el origen de los dos scripts»)
- `src/Opus/dlg/*.elx` — ninguno; `elx.txt` lista 86 que no están
- Archivos `.dlg` — ninguno en todo el repositorio
- El ejecutable del Dialog Editor que convertía `.des` → `.elx`
- DIBAPP original de Microsoft (sustituido por `port/tools/opus_dibapp_tool.cpp`)
