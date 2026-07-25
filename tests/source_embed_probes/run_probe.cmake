if(NOT DEFINED PROBE_TARGET)
    message(FATAL_ERROR "PROBE_TARGET is required")
endif()

if(NOT DEFINED PROBE_BINARY_DIR)
    message(FATAL_ERROR "PROBE_BINARY_DIR is required")
endif()

if(NOT DEFINED PROBE_BINARY)
    message(FATAL_ERROR "PROBE_BINARY is required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${PROBE_BINARY_DIR}" --target "${PROBE_TARGET}"
    RESULT_VARIABLE build_result
    COMMAND_ECHO STDOUT
)

if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Failed to build probe target: ${PROBE_TARGET}")
endif()

execute_process(
    COMMAND "${PROBE_BINARY}"
    RESULT_VARIABLE run_result
    COMMAND_ECHO STDOUT
)

if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "Probe target failed at runtime: ${PROBE_TARGET}")
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    if(NOT DEFINED PROBE_NM OR NOT EXISTS "${PROBE_NM}")
        message(FATAL_ERROR "PROBE_NM must name a valid nm executable on ELF hosts")
    endif()

    execute_process(
        COMMAND "${PROBE_NM}" --defined-only "${PROBE_BINARY}"
        RESULT_VARIABLE nm_result
        OUTPUT_VARIABLE symbols
    )

    if(NOT nm_result EQUAL 0)
        message(FATAL_ERROR "Failed to inspect probe symbols: ${PROBE_TARGET}")
    endif()

    if(symbols MATCHES "_GLOBAL__sub_I")
        message(FATAL_ERROR "Source embedding introduced a global initializer: ${PROBE_TARGET}")
    endif()
endif()
