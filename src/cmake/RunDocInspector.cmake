# RunDocInspector.cmake
#
# Driver de opus_doc_inspector_test: corre la herramienta nativa doc_inspector
# (port/tools/doc_inspector/) sobre cada .doc que dejaron las pruebas de
# guardado y falla si alguno no cierra estructuralmente.
#
# Los .doc no estan versionados: los escribe WORD1 de verdad, a traves del
# dialogo Save As real, durante opus_word1_roundtrip_test (--roundtrip) y
# opus_word1_formatting_test (--rich-format). Esas dos pruebas guardan una
# copia en ARTIFACT_DIR cuando opus_word1_ui_test.cpp ve
# OPUS_X64_DOC_ARTIFACT_DIR en el entorno; el fixture opus_saved_doc_artifacts
# de CTest es lo que garantiza que hayan corrido antes que esta prueba.
#
# doc_inspector no toca Wine ni Win32: esta parte de la validacion es nativa y
# se ejecuta con el binario que instala el sub-proyecto de port/tools/host/.
#
# Codigos de salida de doc_inspector: 0 valido, 1 problemas estructurales,
# 2 uso o E/S. Cualquiera distinto de 0 hace fallar la prueba.

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED DOC_INSPECTOR)
    message(FATAL_ERROR "RunDocInspector.cmake: falta -DDOC_INSPECTOR")
endif()
if(NOT DEFINED ARTIFACT_DIR)
    message(FATAL_ERROR "RunDocInspector.cmake: falta -DARTIFACT_DIR")
endif()
if(NOT EXISTS "${DOC_INSPECTOR}")
    message(FATAL_ERROR
        "RunDocInspector.cmake: no existe el binario ${DOC_INSPECTOR}")
endif()

file(GLOB doc_artifacts "${ARTIFACT_DIR}/*.doc")
list(SORT doc_artifacts)

# Sin artefactos no hay nada que validar, y pasar en silencio convertiria esta
# prueba en un no-op permanente el dia que las pruebas de guardado dejen de
# producir archivo. Es un fallo.
if(NOT doc_artifacts)
    message(FATAL_ERROR
        "RunDocInspector.cmake: no hay ningun .doc en ${ARTIFACT_DIR}; "
        "las pruebas de guardado (opus_word1_roundtrip_test, "
        "opus_word1_formatting_test) no dejaron artefacto")
endif()

set(failed_artifacts "")
foreach(doc_artifact IN LISTS doc_artifacts)
    get_filename_component(doc_name "${doc_artifact}" NAME)
    file(SIZE "${doc_artifact}" doc_size)
    message(STATUS "doc_inspector: ${doc_name} (${doc_size} bytes)")
    execute_process(
        COMMAND "${DOC_INSPECTOR}" --verbose "${doc_artifact}"
        RESULT_VARIABLE inspector_result
        OUTPUT_VARIABLE inspector_output
        ERROR_VARIABLE inspector_errors
    )
    message("${inspector_output}")
    if(inspector_errors)
        message("${inspector_errors}")
    endif()
    if(NOT inspector_result EQUAL 0)
        list(APPEND failed_artifacts "${doc_name} (exit ${inspector_result})")
    endif()
endforeach()

list(LENGTH doc_artifacts doc_artifact_count)
if(failed_artifacts)
    list(JOIN failed_artifacts ", " failed_summary)
    message(FATAL_ERROR
        "doc_inspector rechazo ${failed_summary}")
endif()

message(STATUS
    "doc_inspector: ${doc_artifact_count} archivo(s) .doc estructuralmente "
    "validos")
