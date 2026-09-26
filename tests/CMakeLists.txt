add_executable(
    common_tests
    common_tests.cpp
)

target_link_libraries(
    common_tests
    PRIVATE
    GTest::gtest_main
    pneumo::common
    pneumo::compiler_warnings
)

add_executable(
    meta_tests
    meta_tests.cpp
)

target_link_libraries(
    meta_tests
    PRIVATE
    GTest::gtest_main
    pneumo::meta
    pneumo::compiler_warnings
)

add_executable(
    meta_source_single_tu_probe
    EXCLUDE_FROM_ALL
    source_embed_probes/single_tu.cpp
)

add_executable(
    meta_source_multi_tu_probe
    EXCLUDE_FROM_ALL
    source_embed_probes/multi_tu_main.cpp
    source_embed_probes/multi_tu_first.cpp
    source_embed_probes/multi_tu_second.cpp
)

add_executable(
    meta_source_manual_probe
    EXCLUDE_FROM_ALL
    source_embed_probes/manual.cpp
)

target_link_libraries(
    meta_source_single_tu_probe
    PRIVATE
    pneumo::meta
    pneumo::compiler_warnings
)

target_link_libraries(
    meta_source_manual_probe
    PRIVATE
    pneumo::meta
    pneumo::compiler_warnings
)

target_link_libraries(
    meta_source_multi_tu_probe
    PRIVATE
    pneumo::meta
    pneumo::compiler_warnings
)

add_executable(
    formatting_tests
    formatting_tests.cpp
)

target_link_libraries(
    formatting_tests
    PRIVATE
    GTest::gtest_main
    pneumo::formatting
    pneumo::compiler_warnings
)

add_executable(
    units_tests
    units_tests.cpp
)

target_link_libraries(
    units_tests
    PRIVATE
    GTest::gtest_main
    pneumo::units
    pneumo::compiler_warnings
)

add_executable(
    logging_tests
    logging_tests.cpp
)

target_link_libraries(
    logging_tests
    PRIVATE
    GTest::gtest_main
    pneumo::pneumo
    pneumo::compiler_warnings
)

# Stacktrace tests need debug information and preserved caller frames in all build types.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(logging_tests PRIVATE -g -fno-optimize-sibling-calls)
endif()

add_executable(
    coroutines_tests
    coroutines_tests.cpp
)

target_link_libraries(
    coroutines_tests
    PRIVATE
    GTest::gtest_main
    pneumo::coroutines
    pneumo::compiler_warnings
)

include(GoogleTest)
gtest_discover_tests(common_tests)
gtest_discover_tests(meta_tests)
gtest_discover_tests(formatting_tests)
gtest_discover_tests(units_tests)
gtest_discover_tests(logging_tests)
gtest_discover_tests(coroutines_tests PROPERTIES TIMEOUT 15)

add_test(
    NAME SourceEmbedProbes.SingleTU
    COMMAND
    "${CMAKE_COMMAND}"
    -DPROBE_TARGET=meta_source_single_tu_probe
    -DPROBE_BINARY_DIR=${CMAKE_BINARY_DIR}
    -DPROBE_BINARY=$<TARGET_FILE:meta_source_single_tu_probe>
    -DPROBE_NM=${CMAKE_NM}
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/source_embed_probes/run_probe.cmake"
)

add_test(
    NAME SourceEmbedProbes.MultiTU
    COMMAND
    "${CMAKE_COMMAND}"
    -DPROBE_TARGET=meta_source_multi_tu_probe
    -DPROBE_BINARY_DIR=${CMAKE_BINARY_DIR}
    -DPROBE_BINARY=$<TARGET_FILE:meta_source_multi_tu_probe>
    -DPROBE_NM=${CMAKE_NM}
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/source_embed_probes/run_probe.cmake"
)

add_test(
    NAME SourceEmbedProbes.Manual
    COMMAND
    "${CMAKE_COMMAND}"
    -DPROBE_TARGET=meta_source_manual_probe
    -DPROBE_BINARY_DIR=${CMAKE_BINARY_DIR}
    -DPROBE_BINARY=$<TARGET_FILE:meta_source_manual_probe>
    -DPROBE_NM=${CMAKE_NM}
    -P
    "${CMAKE_CURRENT_SOURCE_DIR}/source_embed_probes/run_probe.cmake"
)

set_tests_properties(
    SourceEmbedProbes.SingleTU
    SourceEmbedProbes.MultiTU
    SourceEmbedProbes.Manual
    PROPERTIES
    RUN_SERIAL TRUE
)
