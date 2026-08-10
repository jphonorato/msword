cmake_minimum_required(VERSION 3.25)
project(opus_core CXX)

# Núcleo Qt (rama Qt-2), no el motor Winelib: siempre se construye con el
# compilador nativo, aunque el proyecto principal esté bajo el toolchain
# winegcc/wineg++. No depende de windows.h ni de la capa de compatibilidad
# de port/ -- ver docs/port-qt/01-frontera-nucleo-shell.md.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

# -fPIC: verificado como necesario en docs/port-qt/scripts/link-check/ --
# winegcc siempre enlaza el binario final como -shared (así arma los .exe.so
# de Winelib, incluso para lo que conceptualmente es un ejecutable), y un
# .a estático sin código independiente de posición no puede entrar ahí
# ("relocation ... can not be used when making a shared object"). Aplica a
# esta biblioteca y a cualquier otra que se agregue aquí para los tres
# contratos OpusShell* restantes (memoria, espina, medición de texto):
# ninguna tiene implementación todavía, pero cuando la tengan deben
# heredar esta propiedad, no descubrirlo de nuevo.
add_library(opus_shell_config STATIC
    src/OpusShellConfig.cpp
)
set_target_properties(opus_shell_config PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(opus_shell_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(opus_shell_config PUBLIC Qt6::Core)

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
