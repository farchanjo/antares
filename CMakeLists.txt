cmake_minimum_required(VERSION 3.25)

project(antares
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Kubernetes desktop manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt is optional while the tree is empty: the presets must configure without it.
find_package(Qt6 QUIET COMPONENTS Core Widgets)

if(Qt6_FOUND)
    qt_standard_project_setup()
    message(STATUS "antares: Qt ${Qt6_VERSION} found")
else()
    message(STATUS "antares: Qt6 not found — headers-only configure")
endif()

add_library(antares_core INTERFACE)
target_include_directories(antares_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(Qt6_FOUND)
    target_link_libraries(antares_core INTERFACE Qt6::Core Qt6::Widgets)
endif()

# Stable-header precompiled header. Empty until the first shared header exists.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp")
    target_precompile_headers(antares_core INTERFACE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/pch.hpp>"
    )
endif()

# Fast linker (P-08). lld links several times faster than ld64/bfd.
option(ANTARES_FAST_LINKER "Use lld as the linker when the compiler is Clang" ON)

if(ANTARES_FAST_LINKER AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_link_options("-fuse-ld=lld")
    message(STATUS "antares: lld linker requested")
endif()

# -ffile-prefix-map keeps sccache keys stable across checkouts (P-08).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
endif()
