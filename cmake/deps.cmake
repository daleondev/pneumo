include(FetchContent)

if(PFMT_ENABLE_JSON OR PFMT_ENABLE_YAML OR PFMT_ENABLE_TOML)
    FetchContent_Declare(
        glaze
        GIT_REPOSITORY  https://github.com/stephenberry/glaze.git
        GIT_TAG         v7.0.2
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
