cmake_minimum_required(VERSION 4.2.0)

add_executable(common_sample)

target_sources(
    common_sample
    PRIVATE
    common_sample.cpp
)

target_link_libraries(
    common_sample
    PRIVATE
    pneumo::common
    pneumo::compiler_warnings
)

add_executable(meta_sample)

target_sources(
    meta_sample
    PRIVATE
    meta_sample.cpp
)

target_link_libraries(
    meta_sample
    PRIVATE
    pneumo::meta
    pneumo::compiler_warnings
)

add_executable(formatting_sample)

target_sources(
    formatting_sample 
    PRIVATE 
    formatting_sample.cpp
)

target_link_libraries(
    formatting_sample
    PRIVATE
    pneumo::formatting
    pneumo::compiler_warnings
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:stdc++exp>
)

add_executable(units_sample)

target_sources(
    units_sample
    PRIVATE
    units_sample.cpp
)

target_link_libraries(
    units_sample
    PRIVATE
    pneumo::units
    pneumo::compiler_warnings
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:stdc++exp>
)

add_executable(logging_sample)

target_sources(
    logging_sample
    PRIVATE
    logging_sample.cpp
)

target_link_libraries(
    logging_sample
    PRIVATE
    pneumo::logging
    pneumo::compiler_warnings
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:stdc++exp>
)
