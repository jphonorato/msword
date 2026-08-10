#!/bin/sh
# Reproduce la verificación de enlace cross-toolchain documentada en
# docs/port-qt/01-frontera-nucleo-shell.md ("Verificación de la frontera
# física"). Requiere opus_core_build ya compilado
# (cmake --build build/linux-winelib-debug --target opus_core_build).
set -e

CORE_LIB="../../../../build/linux-winelib-debug/opus_core_build-prefix/src/opus_core_build-build"
QT_LIB_DIR="/usr/lib/x86_64-linux-gnu/qt6"

cd "$(dirname "$0")"

echo "== compilar link_check.c con winegcc (mismo compilador que las .c de opus_original_engine) =="
winegcc -c -I../../../../src/core/include link_check.c -o link_check.o

echo "== enlazar con wineg++ (el driver real que usa WORD1, por su requisito cxx_std_20) =="
wineg++ link_check.o -L"$CORE_LIB" -lopus_shell_config -L"$QT_LIB_DIR" -lQt6Core -o link_check_wineg

echo "== enlazar con winegcc puro (requiere -lstdc++ explícito: winegcc no lo enlaza por defecto) =="
winegcc link_check.o -L"$CORE_LIB" -lopus_shell_config -L"$QT_LIB_DIR" -lQt6Core -lstdc++ -o link_check_winegcc

echo "== ejecutar ambos =="
./link_check_wineg.exe
./link_check_winegcc.exe

rm -f link_check.o link_check_wineg link_check_wineg.exe link_check_wineg.exe.so \
      link_check_winegcc link_check_winegcc.exe link_check_winegcc.exe.so

echo "OK -- ambos enlazan y la llamada real a OpusShellProfileString/Write funciona."
