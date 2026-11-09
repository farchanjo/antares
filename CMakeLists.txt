cmake_minimum_required(VERSION 4.4)

project(antares
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Kubernetes desktop manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# FetchContent works outside the tree, so the corpus scanners never read
# third-party files as project ones.
set(FETCHCONTENT_BASE_DIR "$ENV{HOME}/.cache/antares/deps" CACHE PATH "")

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

set(ANTARES_ALLOCATOR "mimalloc" CACHE STRING "Allocator: mimalloc or system")
set_property(CACHE ANTARES_ALLOCATOR PROPERTY STRINGS mimalloc system)

if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
    include(FetchContent)
    FetchContent_Declare(mimalloc
        URL https://github.com/microsoft/mimalloc/archive/refs/tags/v3.6.1.tar.gz
        URL_HASH SHA256=2672e6a99d4579fb4bb32d9de0d63aa424ce600d31fbfaf06eb74d940d2e07a6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # mimalloc's own debug build aborts on macOS during process teardown
    # (exit 134 after a clean run). The release runtime is stable there and our
    # own -g line info is unaffected.
    set(MI_DEBUG OFF CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(mimalloc)
endif()

# TLS and HTTP/2 are pinned by exact version (D-17); the Homebrew prefix is the
# only place they exist on macOS.
# The pin is exact where the toolchain is home-grown (the macOS host), and a
# floor elsewhere, so a distro's older pair still configures.
if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/openssl@3)
    find_package(OpenSSL 3.6.4 EXACT REQUIRED COMPONENTS SSL Crypto)
else()
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS SSL Crypto)
endif()
# nghttp2 ships no CMake config, only a pkg-config file, so the pin goes there.
find_package(PkgConfig REQUIRED)
if(APPLE)
    pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2=1.69.0)
else()
    pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2>=1.55)
endif()

set(ANTARES_HTTP_SOURCES
    src/domain/http/response.cpp
    src/domain/http/url.cpp
    src/domain/http/auth.cpp
    src/domain/http/request.cpp
    src/domain/units/bytes.cpp
    src/adapters/http/tls_context.cpp
    src/adapters/http/connection.cpp
    src/adapters/http/h1_session.cpp
    src/adapters/http/client.cpp
    src/application/http/blocking_client.cpp
)

add_library(antares_http STATIC ${ANTARES_HTTP_SOURCES})
target_link_libraries(antares_http PUBLIC antares_io OpenSSL::SSL OpenSSL::Crypto PkgConfig::NGHTTP2)
set_target_properties(antares_http PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

set(ANTARES_IO_SOURCES
    src/ports/io/io_engine.cpp
    src/ports/io/io_stats.cpp
    src/ports/io/io_handle.cpp
    src/adapters/platform/io/engine_base.cpp
    src/adapters/platform/io/poll_engine.cpp
    src/application/io/engine_factory.cpp
)

if(APPLE)
    list(APPEND ANTARES_IO_SOURCES src/adapters/platform/io/kqueue_engine.cpp)
endif()

if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
    list(APPEND ANTARES_IO_SOURCES src/platform/common/allocator.cpp)
    set(ANTARES_MIMALLOC_INCLUDE "${mimalloc_SOURCE_DIR}/include")
endif()

add_library(antares_io STATIC ${ANTARES_IO_SOURCES})
target_link_libraries(antares_io PUBLIC antares_core)

if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
    target_include_directories(antares_io PRIVATE "${ANTARES_MIMALLOC_INCLUDE}")
endif()

# The port names no Qt type (P-06): MOC off proves it stays that way.
set_target_properties(antares_io PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# -ffile-prefix-map keeps sccache keys stable across checkouts (P-08).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")
endif()

enable_testing()

# Tests fan out across every core. Presets cannot read the environment for this
# (`jobs` takes an integer and rejects `$penv{}`), and a bare `--parallel` is
# serial, so the count is resolved here at configure time and `JOBS` overrides.
include(ProcessorCount)
ProcessorCount(ANTARES_JOBS)
if(DEFINED ENV{JOBS} AND NOT "$ENV{JOBS}" STREQUAL "")
    set(ANTARES_JOBS "$ENV{JOBS}")
endif()
if(ANTARES_JOBS EQUAL 0)
    set(ANTARES_JOBS 4)
endif()
message(STATUS "antares: test parallelism ${ANTARES_JOBS}")

add_custom_target(lint
    COMMAND ${CMAKE_SOURCE_DIR}/tools/check-include-paths.sh
    COMMAND ${CMAKE_SOURCE_DIR}/tools/check-domain-purity.sh
    COMMAND ${CMAKE_SOURCE_DIR}/tools/check-pins.sh
    COMMENT "antares: lint rules"
    VERBATIM
    USES_TERMINAL)

add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel ${ANTARES_JOBS}
    COMMENT "antares: test suite"
    VERBATIM
    USES_TERMINAL)
add_dependencies(check antares_io_conformance lint)

# Presets are read from the source directory, so every command starts there.
set(ANTARES_CMAKE_RUN ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR} ${CMAKE_COMMAND})

add_custom_target(asan
    COMMAND ${ANTARES_CMAKE_RUN} --preset dev-asan
    COMMAND ${ANTARES_CMAKE_RUN} --build --preset dev-asan --parallel ${ANTARES_JOBS}
    COMMAND ${ANTARES_CMAKE_RUN} -E chdir ${CMAKE_SOURCE_DIR} ctest --preset dev-asan
            --output-on-failure --parallel ${ANTARES_JOBS}
    COMMENT "antares: AddressSanitizer pass"
    VERBATIM
    USES_TERMINAL)

if(APPLE)
    add_custom_target(leak
        COMMAND ${ANTARES_CMAKE_RUN} --preset dev
        COMMAND ${ANTARES_CMAKE_RUN} --build --preset dev --parallel ${ANTARES_JOBS}
        COMMAND ${ANTARES_CMAKE_RUN} -E chdir ${CMAKE_SOURCE_DIR} leaks --atExit --
                ${CMAKE_SOURCE_DIR}/build/dev/antares_io_conformance
        COMMENT "antares: leak pass (leaks)"
        VERBATIM
        USES_TERMINAL)
else()
    add_custom_target(leak
        COMMAND ${ANTARES_CMAKE_RUN} --preset dev-asan
        COMMAND ${ANTARES_CMAKE_RUN} --build --preset dev-asan --parallel ${ANTARES_JOBS}
        COMMAND ${ANTARES_CMAKE_RUN} -E env ASAN_OPTIONS=detect_leaks=1
                ${CMAKE_SOURCE_DIR}/build/dev-asan/antares_io_conformance
        COMMENT "antares: leak pass (LeakSanitizer)"
        VERBATIM
        USES_TERMINAL)
endif()

add_executable(antares_io_conformance tests/io/conformance.cpp)
target_link_libraries(antares_io_conformance PRIVATE antares_io)

if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
    if(APPLE)
        # ld64 needs force_load to beat libSystem, and it already pulls every
        # member in, so the archive goes on the line once only.
        target_link_options(antares_io_conformance PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:mimalloc-static>")
    else()
        target_link_libraries(antares_io_conformance PRIVATE mimalloc-static)
    endif()
endif()
set_target_properties(antares_io_conformance PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# P-13: symbolize the binary on Apple, so a leak report names frames.
if(APPLE)
    add_custom_command(TARGET antares_io_conformance POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:antares_io_conformance>
        COMMENT "dsymutil antares_io_conformance"
        VERBATIM)
endif()

# One CTest entry per case, so `ctest -j` fans the suite across cores.
set(ANTARES_IO_CASES
    post-runs-on-engine-thread
    socket-read-write
    file-read-immediate
    stop-ends-callbacks
    counters-move)

add_executable(antares_http_conformance tests/http/conformance.cpp)
target_link_libraries(antares_http_conformance PRIVATE antares_http)
target_include_directories(antares_http_conformance PRIVATE ${CMAKE_SOURCE_DIR}/tests/http)
set_target_properties(antares_http_conformance PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
    target_link_libraries(antares_http_conformance PRIVATE mimalloc-static)
    if(APPLE)
        target_link_options(antares_http_conformance PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:mimalloc-static>")
    endif()
endif()
if(APPLE)
    add_custom_command(TARGET antares_http_conformance POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:antares_http_conformance>
        COMMENT "dsymutil antares_http_conformance"
        VERBATIM)
endif()

set(ANTARES_HTTP_CASES
    async-get
    sync-get
    redirect-followed
    reentrant-from-callback
    pool-reuses-connection)

foreach(case IN LISTS ANTARES_HTTP_CASES)
    add_test(NAME "http/${case}" COMMAND antares_http_conformance "${case}")
endforeach()

foreach(case IN LISTS ANTARES_IO_CASES)
    add_test(NAME "poll/${case}" COMMAND antares_io_conformance "${case}")
    set_tests_properties("poll/${case}" PROPERTIES ENVIRONMENT "ANTARES_IO_BACKEND=poll")
    if(APPLE)
        add_test(NAME "kqueue/${case}" COMMAND antares_io_conformance "${case}")
        set_tests_properties("kqueue/${case}" PROPERTIES ENVIRONMENT "ANTARES_IO_BACKEND=kqueue")
    endif()
endforeach()
