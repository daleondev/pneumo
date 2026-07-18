cmake_minimum_required(VERSION 4.2.0)
project(
    pneumo
    VERSION 0.1.0
    DESCRIPTION "Pneumo C++26 utilities"
    LANGUAGES CXX
)

#---------------------------------------------------------
# options
#---------------------------------------------------------

option(PNM_BUILD_SAMPLES "Build the Pneumo sample executables." ${PROJECT_IS_TOP_LEVEL})
option(PNM_BUILD_TESTS "Build the Pneumo tests." ${PROJECT_IS_TOP_LEVEL})

option(PFMT_ENABLE_JSON "Enable JSON formatting support." OFF)
option(PFMT_ENABLE_YAML "Enable YAML formatting support." OFF)
option(PFMT_ENABLE_TOML "Enable TOML formatting support." OFF)

#---------------------------------------------------------
# dependencies
#---------------------------------------------------------

include(${CMAKE_CURRENT_LIST_DIR}/cmake/deps.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/platform.cmake)

#---------------------------------------------------------
# common
#---------------------------------------------------------

add_library(pneumo_common INTERFACE)
add_library(pneumo::common ALIAS pneumo_common)

target_sources(
    pneumo_common
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
    FILES 
        ${CMAKE_CURRENT_LIST_DIR}/include/pneumo/common.hpp
)

target_compile_features(
    pneumo_common
    INTERFACE
    cxx_std_26
)

target_compile_definitions(
    pneumo_common
    INTERFACE
    $<$<CONFIG:Debug>:PNM_ENABLE_ASSERTS>
)

pcmn_apply_platform_definitions(pneumo_common)

#---------------------------------------------------------
# meta
#---------------------------------------------------------

add_library(pneumo_meta INTERFACE)
add_library(pneumo::meta ALIAS pneumo_meta)

target_sources(
    pneumo_meta
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
    FILES ${CMAKE_CURRENT_LIST_DIR}/include/pneumo/meta.hpp
)

target_link_libraries(
    pneumo_meta
    INTERFACE
    pneumo::common
)

target_compile_features(
    pneumo_meta
    INTERFACE
    cxx_std_26
)

target_compile_options(
    pneumo_meta
    INTERFACE
    -freflection
    $<$<CXX_COMPILER_ID:Clang>:-fexpansion-statements>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>
)

#---------------------------------------------------------
# formatting
#---------------------------------------------------------

add_library(pneumo_formatting INTERFACE)
add_library(pneumo::formatting ALIAS pneumo_formatting)

target_sources(
    pneumo_formatting
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
    FILES ${CMAKE_CURRENT_LIST_DIR}/include/pneumo/formatting.hpp
)

target_link_libraries(
    pneumo_formatting
    INTERFACE
    pneumo::meta
)

target_compile_features(
    pneumo_formatting
    INTERFACE
    cxx_std_26
)

if(PFMT_ENABLE_JSON OR PFMT_ENABLE_YAML OR PFMT_ENABLE_TOML)
    target_link_libraries(
        pneumo_formatting
        INTERFACE
        glaze::glaze
        glaze_defines
    )
endif()

#---------------------------------------------------------
# units
#---------------------------------------------------------

add_library(pneumo_units INTERFACE)
add_library(pneumo::units ALIAS pneumo_units)

target_sources(
    pneumo_units
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
    FILES ${CMAKE_CURRENT_LIST_DIR}/include/pneumo/units.hpp
)

target_link_libraries(
    pneumo_units
    INTERFACE
    pneumo::meta
)

target_compile_features(
    pneumo_units
    INTERFACE
    cxx_std_26
)

#---------------------------------------------------------
# logging
#---------------------------------------------------------

add_library(pneumo_logging INTERFACE)
add_library(pneumo::logging ALIAS pneumo_logging)

target_sources(
    pneumo_logging
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
    FILES ${CMAKE_CURRENT_LIST_DIR}/include/pneumo/logging.hpp
)

target_link_libraries(
    pneumo_logging
    INTERFACE
    pneumo::formatting
)

target_compile_features(
    pneumo_logging
    INTERFACE
    cxx_std_26
)

#---------------------------------------------------------
# combined
#---------------------------------------------------------

add_library(pneumo INTERFACE)
add_library(pneumo::pneumo ALIAS pneumo)

target_sources(
    pneumo
    INTERFACE
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/include
    FILES ${CMAKE_CURRENT_LIST_DIR}/include/pneumo/pneumo.hpp
)

target_link_libraries(
    pneumo
    INTERFACE
    pneumo::common
    pneumo::meta
    pneumo::formatting
    pneumo::units
    pneumo::logging
)

#---------------------------------------------------------
# compiler warnings
#---------------------------------------------------------

add_library(pneumo_compiler_warnings INTERFACE)
add_library(pneumo::compiler_warnings ALIAS pneumo_compiler_warnings)
target_compile_options(
    pneumo_compiler_warnings
    INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /constexpr:steps20000000>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Wpedantic -Werror>
)

#---------------------------------------------------------
# linter
#---------------------------------------------------------

option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis during build" OFF)

if(ENABLE_CLANG_TIDY)
    if(NOT CMAKE_CXX_CLANG_TIDY)
        find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
        if(CLANG_TIDY_EXE)
            set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        endif()
    endif()

    if(CMAKE_CXX_CLANG_TIDY)
        message(STATUS "Using clang-tidy: ${CMAKE_CXX_CLANG_TIDY}")
        list(APPEND CMAKE_CXX_CLANG_TIDY "-extra-arg=-Wno-unknown-warning-option")
    else()
        message(WARNING "clang-tidy not found, disabling ENABLE_CLANG_TIDY")
    endif()
endif()

#---------------------------------------------------------
# samples
#---------------------------------------------------------

if(PNM_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

#---------------------------------------------------------
# tests
#---------------------------------------------------------

if(PNM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
