cmake_minimum_required(VERSION 3.25)
project(opus_core CXX)

# Núcleo Qt (rama Qt-2), no el motor Winelib: siempre se construye con el
# compilador nativo, aunque el proyecto principal esté bajo el toolchain
# winegcc/wineg++. No depende de windows.h ni de la capa de compatibilidad
# de port/ -- ver docs/port-qt/01-frontera-nucleo-shell.md.
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

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(opus_shell_config STATIC
    src/OpusShellConfig.cpp
)
target_include_directories(opus_shell_config PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(opus_shell_config PUBLIC Qt6::Core)

# Contrato de memoria Win16 (B3). No depende de Qt: es malloc/realloc/free
# nativo detrás de un handle opaco con contador de fijación -- ver
# docs/port-qt/01-frontera-nucleo-shell.md §B3 y el comentario de cabecera
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
