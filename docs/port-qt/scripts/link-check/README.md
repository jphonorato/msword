# Verificación de enlace cross-toolchain (previa a B3)

Programa de un solo uso que resolvió empíricamente si un binario
Winelib (winegcc/wineg++, el toolchain de `opus_original_engine`/`WORD1`)
puede enlazar contra `opus_shell_config` (gcc/g++ nativo + Qt6) y llamar
con éxito a una función real ya implementada. No forma parte del build.

Ver "Verificación de la frontera física" en
`docs/port-qt/01-core-shell-boundary.md` para el veredicto completo y
los dos ajustes que hicieron falta.

| Archivo | Qué hace |
|---|---|
| `link_check.c` | Llama a `OpusShellProfileWrite` y `OpusShellProfileString` reales, verifica que el valor escrito vuelve intacto |
| `run.sh` | Compila con winegcc, enlaza con wineg++ y con winegcc puro, ejecuta ambos |

Requiere `opus_core_build` ya compilado:

```sh
cmake --build build/linux-winelib-debug --target opus_core_build
docs/port-qt/scripts/link-check/run.sh
```

Los mensajes de Wine sobre `wine32`/multiarch faltante y "no driver
could be loaded" son ruido del entorno (el programa no crea ventanas ni
necesita 32 bits); no afectan el resultado.
