include(FetchContent)

if(PNM_CORO_ENABLE_EXECUTION)
    FetchContent_Declare(
        stdexec
        GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
        GIT_TAG 00648922a30caa7872c6dae372018ee13a2b545f
        # Consume the headers without configuring upstream tooling and dependencies.
        SOURCE_SUBDIR include
    )
    FetchContent_MakeAvailable(stdexec)
    add_library(pneumo_execution_dependency INTERFACE)
    target_include_directories(pneumo_execution_dependency SYSTEM INTERFACE ${stdexec_SOURCE_DIR}/include)
    target_compile_definitions(pneumo_execution_dependency INTERFACE STDEXEC_PARALLEL_SCHEDULER_HEADER_ONLY)
endif()

if(PFMT_ENABLE_JSON OR PFMT_ENABLE_YAML OR PFMT_ENABLE_TOML)
    FetchContent_Declare(
        glaze
        GIT_REPOSITORY  https://github.com/stephenberry/glaze.git
        GIT_TAG         v9.0.0
        GIT_SHALLOW     TRUE
    )
    FetchContent_MakeAvailable(glaze)

    add_library(glaze_defines INTERFACE)
    target_compile_definitions(glaze_defines INTERFACE PFMT_ENABLE_GLAZE)

    if(PFMT_ENABLE_JSON)
        target_compile_definitions(glaze_defines INTERFACE PFMT_ENABLE_JSON)
    endif()
    if(PFMT_ENABLE_YAML)
        target_compile_definitions(glaze_defines INTERFACE PFMT_ENABLE_YAML)
    endif()
    if(PFMT_ENABLE_TOML)
        target_compile_definitions(glaze_defines INTERFACE PFMT_ENABLE_TOML)
    endif()
endif()

if(PNM_BUILD_TESTS)
    FetchContent_Declare(
        GTest
        GIT_REPOSITORY  https://github.com/google/googletest.git
        GIT_TAG         v1.17.0
        GIT_SHALLOW     TRUE
    )
    FetchContent_MakeAvailable(GTest)
endif()
