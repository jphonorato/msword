#!/bin/sh
# Reproduce la verificación de round-trip de handle documentada en
# docs/port-qt/01-frontera-nucleo-shell.md ("Verificación B3: round-trip
# de handle real"). Requiere opus_core_build ya compilado
# (cmake --build build/linux-winelib-debug --target opus_core_build).
set -e

CORE_LIB="../../../../build/linux-winelib-debug/opus_core_build-prefix/src/opus_core_build-build"

cd "$(dirname "$0")"

echo "== compilar y enlazar con wineg++ (toolchain real de WORD1) =="
wineg++ -I../../../../src/core/include handle_check.c \
    -L"$CORE_LIB" -lopus_shell_memory \
    -o handle_check

echo "== round trip completo (alloc/lock/escribir/unlock/lock/verificar/free/lock-tras-free) =="
./handle_check.exe
RC=$?
if [ "$RC" -ne 0 ]; then
    echo "FALLÓ el round trip (exit $RC)" >&2
    exit 1
fi

echo
echo "== doble liberación: se espera terminación anormal (abort), no un exit limpio =="
set +e
./handle_check.exe --double-free
RC=$?
set -e
echo "exit code del proceso de doble liberación: $RC"
# 134 = 128 + SIGABRT(6): abort() bajo Wine/Linux. Cualquier salida que NO
# sea 0 confirma que no continuó como si nada -- 134 confirma específicamente
# que fue abort(), no otra clase de fallo.
if [ "$RC" -eq 0 ]; then
    echo "FALLO DE CONTRATO: el proceso de doble liberación terminó limpio (exit 0)" >&2
    exit 1
fi

rm -f handle_check handle_check.exe handle_check.exe.so

echo
echo "OK -- round trip completo, mas doble liberacion y lock-tras-free fallan controlado."
