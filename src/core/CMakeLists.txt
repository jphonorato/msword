cmake_minimum_required(VERSION 3.25)
project(opus_core CXX)

# Núcleo Qt (rama Qt-2), no el motor Winelib: siempre se construye con el
# compilador nativo, aunque el proyecto principal esté bajo el toolchain
# winegcc/wineg++. No depende de windows.h ni de la capa de compatibilidad
# de port/ -- ver docs/port-qt/01-core-shell-boundary.md.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# -fPIC para TODO lo que se agregue en este directorio: verificado como
# necesario en docs/port-qt/scripts/link-check/ -- winegcc siempre enlaza
# el binario final como -shared (así arma los .exe.so de Winelib, incluso
# para lo que conceptualmente es un ejecutable), y un .a estático sin
# código independiente de posición no puede entrar ahí ("relocation ...
# can not be used when making a shared object"). A nivel de directorio,
# no por target, para que cada contrato OpusShell* nuevo lo herede sin
# repetir el set_target_properties.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(opus_shell_config STATIC
    src/OpusShellConfig.cpp
)
target_include_directories(opus_shell_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(opus_shell_config PUBLIC Qt6::Core)
install(TARGETS opus_shell_config ARCHIVE DESTINATION lib)

# Contrato de sustitución de fuentes de época (§B2.6). Tabla de datos
# estática, medida contra el oráculo Winelib -- ver
# docs/port-qt/01-core-shell-boundary.md §B2.6. No depende de Qt.
add_library(opus_shell_font_substitution STATIC
    src/OpusShellFontSubstitution.cpp
)
target_include_directories(opus_shell_font_substitution PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
install(TARGETS opus_shell_font_substitution ARCHIVE DESTINATION lib)

if(OPUS_CORE_BUILD_TESTS)
    add_executable(opus_shell_font_substitution_test
        src/OpusShellFontSubstitution_test.cpp
    )
    target_link_libraries(opus_shell_font_substitution_test PRIVATE
        opus_shell_font_substitution
    )
    add_test(NAME opus_shell_font_substitution_test
             COMMAND $<TARGET_FILE:opus_shell_font_substitution_test>)
    install(TARGETS opus_shell_font_substitution_test RUNTIME DESTINATION bin)
endif()

# Contrato de medición de texto (B2, §B2.2-§B2.3). Primera implementación
# real -- ver el comentario de cabecera de OpusShellFontMetrics.cpp para
# las limitaciones explícitas de este corte (sin síntesis negrita/
# cursiva, 96 ppp fijo, tabla ftc->nombre hardcodeada a los 4 conocidos).
add_library(opus_shell_font_metrics STATIC
    src/OpusShellFontMetrics.cpp
)
target_include_directories(opus_shell_font_metrics PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(opus_shell_font_metrics PRIVATE
    opus_shell_font_substitution Qt6::Gui
)
install(TARGETS opus_shell_font_metrics ARCHIVE DESTINATION lib)

if(OPUS_CORE_BUILD_TESTS)
    add_executable(opus_shell_font_metrics_test
        src/OpusShellFontMetrics_test.cpp
    )
    target_link_libraries(opus_shell_font_metrics_test PRIVATE
        opus_shell_font_metrics Qt6::Gui
    )
    add_test(NAME opus_shell_font_metrics_test
             COMMAND $<TARGET_FILE:opus_shell_font_metrics_test>)
    install(TARGETS opus_shell_font_metrics_test RUNTIME DESTINATION bin)

    # Prueba de fidelidad amplia: 2660 puntos de dato capturados del
    # oráculo Winelib real (docs/port-qt/scripts/fidelity/capture.py),
    # no solo el punto único de opus_shell_font_metrics_test. Ver
    # OpusShellFontMetrics_fidelity_test.cpp.
    add_executable(opus_shell_font_metrics_fidelity_test
        src/OpusShellFontMetrics_fidelity_test.cpp
    )
    target_include_directories(opus_shell_font_metrics_fidelity_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(opus_shell_font_metrics_fidelity_test PRIVATE
        opus_shell_font_metrics Qt6::Gui
    )
    add_test(NAME opus_shell_font_metrics_fidelity_test
             COMMAND $<TARGET_FILE:opus_shell_font_metrics_fidelity_test>)
    install(TARGETS opus_shell_font_metrics_fidelity_test RUNTIME DESTINATION bin)
endif()

# Contrato de espina de mensajes/ventanas (B4) -- solo los dos fragmentos
# con firma concreta de §B4.3 (error.c, editspec.c/undo.c). La inversión
# de control completa (paso 7 de la secuencia Qt-2) no se implementa
# aquí -- ver OpusShellSpine.h.
add_library(opus_shell_spine STATIC
    src/OpusShellSpine.cpp
)
target_include_directories(opus_shell_spine PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(opus_shell_spine PRIVATE Qt6::Widgets)
install(TARGETS opus_shell_spine ARCHIVE DESTINATION lib)

if(OPUS_CORE_BUILD_TESTS)
    add_executable(opus_shell_spine_test
        src/OpusShellSpine_test.cpp
    )
    target_link_libraries(opus_shell_spine_test PRIVATE
        opus_shell_spine Qt6::Widgets
    )
    add_test(NAME opus_shell_spine_test
             COMMAND $<TARGET_FILE:opus_shell_spine_test>)
    install(TARGETS opus_shell_spine_test RUNTIME DESTINATION bin)
endif()

# Contrato de memoria Win16 (B3). No depende de Qt: es malloc/realloc/free
# nativo detrás de un handle opaco con contador de fijación -- ver
# docs/port-qt/01-core-shell-boundary.md §B3 y el comentario de cabecera
# de OpusShellMemory.h.
add_library(opus_shell_memory STATIC
    src/OpusShellMemory.cpp
)
target_include_directories(opus_shell_memory PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(OPUS_CORE_BUILD_TESTS)
    enable_testing()
    add_executable(opus_shell_config_test
        src/OpusShellConfig_test.cpp
    )
    target_link_libraries(opus_shell_config_test PRIVATE
        opus_shell_config Qt6::Core
    )
    add_test(NAME opus_shell_config_test
             COMMAND $<TARGET_FILE:opus_shell_config_test>)
    install(TARGETS opus_shell_config_test RUNTIME DESTINATION bin)
endif()

# Instalado siempre (no solo bajo OPUS_CORE_BUILD_TESTS): la sonda de
# round-trip de handle en docs/port-qt/scripts/handle-check/ se enlaza
# con wineg++ desde fuera de este build, contra la biblioteca instalada,
# igual que docs/port-qt/scripts/link-check/ hizo con opus_shell_config.
install(TARGETS opus_shell_memory ARCHIVE DESTINATION lib)

# Primer binario Qt real de la rama Qt-2 -- QApplication/QMainWindow, no
# QCoreApplication como los tests de arriba. Andamiaje deliberadamente
# vacío de motor de documento (docs/port-qt/01-core-shell-boundary.md,
# "Secuencia recomendada para Qt-2", paso 7 sigue siendo el último);
# existe para probar que el grafo de enlace con Widgets de por medio
# funciona antes de colgarle encima medición de texto (B2). Se construye
# siempre, no solo bajo OPUS_CORE_BUILD_TESTS -- es un ejecutable de
# producto embrionario, no una prueba.
add_executable(opus_qt_shell
    src/opus_qt_shell_main.cpp
)
target_link_libraries(opus_qt_shell PRIVATE
    opus_shell_config
    opus_shell_memory
    opus_shell_font_substitution
    opus_shell_font_metrics
    opus_shell_spine
    Qt6::Widgets
)
install(TARGETS opus_qt_shell RUNTIME DESTINATION bin)
