cmake_minimum_required(VERSION 4.4)

project(antares
    VERSION 0.1.0
    DESCRIPTION "Cross-platform Kubernetes desktop manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The fastest available linker, in descending order. Link time dominates an
# incremental build here, and on Linux mold is several times quicker than lld.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(ANTARES_FAST_LINKER NAMES mold)
    if(ANTARES_FAST_LINKER)
        add_link_options("-fuse-ld=mold")
    endif()
endif()

# libc++ on both targets, and it is not only a Darwin matter: libstdc++ 13 gates
# `<expected>` behind `__cpp_concepts >= 202002`, which clang 18 reports as
# 201907, so libstdc++ hides the header under the compiler this project uses.
#
# The flag is scoped to C++ on purpose. C sources exist (mimalloc is one), and
# clang counts `-stdlib` as an unused argument for them; under the release
# preset's -Werror that alone fails the build.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>")
add_link_options(-stdlib=libc++)

# FetchContent works outside the tree, so the corpus scanners never read
# third-party files as project ones.
set(FETCHCONTENT_BASE_DIR "$ENV{HOME}/.cache/antares/deps" CACHE PATH "")

# Qt is optional while the tree is empty: the presets must configure without it.
# `LinguistTools` is optional: a Qt without it only loses the catalogue build, and
# naming it as a plain component would flip `Qt6_FOUND` off and take the toolkit
# down with it.
find_package(Qt6 QUIET COMPONENTS Core Widgets OPTIONAL_COMPONENTS LinguistTools)

if(Qt6_FOUND)
    # `I18N_TRANSLATED_LANGUAGES` is what makes the build extract `tr()` strings
    # and compile the catalogues into the binary (I18N-003). The `.qm` output is a
    # build artifact and stays out of the tree.
    qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES pt_BR)
    message(STATUS "antares: Qt ${Qt6_VERSION} found")
else()
    message(STATUS "antares: Qt6 not found — headers-only configure")
endif()

add_library(antares_core INTERFACE)
target_include_directories(antares_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# The toolkit is not the core. Linking Qt into `antares_core` would let a domain
# header include `<QObject>` and still build, so D-03 would hold on paper only.
# Everything below `adapters/ui` links the core; only that adapter links this.
# The window is absent without Qt6 and the tree still configures, builds and
# tests the engine, HTTP and Kubernetes layers. That is what lets the Linux loop
# run while the acceptance target (P-05) is the only one with a toolkit.
#
# Two model files exist while 009 and 010 settle: `resource_model` is what the
# window binds to, `resource_list_model` is the change-set applier the suite
# drives without a widget. They converge later.
set(ANTARES_UI_SOURCES
    src/adapters/ui/change_queue.cpp
    src/adapters/ui/page_error.cpp
    src/adapters/ui/resource_list_model.cpp
    src/adapters/ui/row_role.cpp
    src/adapters/ui/row_text.cpp
    src/adapters/ui/view_config.cpp
    src/adapters/ui/avatar.cpp
    src/adapters/ui/detail_pane.cpp
    src/adapters/ui/family.cpp
    src/adapters/ui/kube_page_source.cpp
    src/adapters/ui/preferences_page.cpp
    src/adapters/ui/qt_list_model.cpp
)

if(Qt6_FOUND)
    list(APPEND ANTARES_UI_SOURCES
        src/adapters/ui/main_window.cpp
        src/adapters/ui/application.cpp)
endif()

add_library(antares_ui STATIC ${ANTARES_UI_SOURCES})
target_link_libraries(antares_ui PUBLIC antares_core antares_k8s)
set_target_properties(antares_ui PROPERTIES AUTOUIC OFF AUTORCC OFF)

if(Qt6_FOUND)
    target_link_libraries(antares_ui PUBLIC Qt6::Core Qt6::Widgets)
    target_compile_definitions(antares_ui PUBLIC ANTARES_HAS_QT=1)
    set_target_properties(antares_ui PROPERTIES AUTOMOC ON)
else()
    set_target_properties(antares_ui PROPERTIES AUTOMOC OFF)
endif()

# Stable-header precompiled header, propagated to every consumer through the
# interface target. It holds the standard library and the two third parties, so
# a project header change never invalidates it.
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
# The PCH names both third parties, and it is propagated through `antares_core`.
# The include directories have to travel with it, or a Homebrew prefix that is
# not `/usr` leaves every translation unit unable to find the header.
target_link_libraries(antares_core INTERFACE OpenSSL::SSL OpenSSL::Crypto)

# nghttp2 ships no CMake config, only a pkg-config file, so the pin goes there.
find_package(PkgConfig REQUIRED)
if(APPLE)
    pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2=1.69.0)
else()
    pkg_check_modules(NGHTTP2 REQUIRED IMPORTED_TARGET libnghttp2>=1.55)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBURING REQUIRED IMPORTED_TARGET liburing>=2.5)
endif()

set(ANTARES_HTTP_SOURCES
    src/domain/http/response.cpp
    src/domain/http/cache.cpp
    src/domain/http/cookie_jar.cpp
    src/domain/http/url.cpp
    src/domain/http/auth.cpp
    src/domain/http/retry_policy.cpp
    src/domain/http/sse.cpp
    src/domain/json/sequence.cpp
    src/domain/http/websocket.cpp
    src/domain/http/request.cpp
    src/domain/units/bytes.cpp
    src/adapters/http/tls_context.cpp
    src/adapters/log/console_logger.cpp
    src/adapters/http/connection.cpp
    src/adapters/http/content_decoder.cpp
    src/adapters/http/h1_session.cpp
    src/adapters/http/h2_session.cpp
    src/adapters/http/websocket.cpp
    src/adapters/http/client.cpp
    src/application/http/blocking_client.cpp
)

add_library(antares_http STATIC ${ANTARES_HTTP_SOURCES})
pkg_check_modules(ZLIB REQUIRED IMPORTED_TARGET zlib)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
pkg_check_modules(BROTLI REQUIRED IMPORTED_TARGET libbrotlidec libbrotlienc)

target_link_libraries(antares_core INTERFACE PkgConfig::NGHTTP2)
target_link_libraries(antares_http PUBLIC antares_io OpenSSL::SSL OpenSSL::Crypto PkgConfig::NGHTTP2
                                      PkgConfig::ZLIB PkgConfig::ZSTD PkgConfig::BROTLI)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(antares_http PUBLIC PkgConfig::LIBURING)
endif()
set_target_properties(antares_http PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# The Kubernetes API client: a pure domain, a port and one adapter over
# antares_http. It adds no transport and no third-party dependency.
set(ANTARES_K8S_SOURCES
    src/domain/json/scan_error.cpp
    src/domain/json/value.cpp
    src/domain/yaml/node.cpp
    src/domain/k8s/api_error.cpp
    src/domain/k8s/api_path.cpp
    src/domain/k8s/options.cpp
    src/domain/k8s/discovery.cpp
    src/domain/k8s/exec_channel.cpp
    src/domain/k8s/status.cpp
    src/domain/k8s/watch_event.cpp
    src/domain/k8s/kubeconfig.cpp
    src/domain/k8s/cluster_name.cpp
    src/domain/k8s/namespace_name.cpp
    src/domain/k8s/object_name.cpp
    src/domain/k8s/uid.cpp
    src/domain/k8s/scope.cpp
    src/domain/k8s/labels.cpp
    src/domain/k8s/change_set.cpp
    src/domain/k8s/store_error.cpp
    src/domain/k8s/store_metrics.cpp
    src/domain/k8s/entry_decoder.cpp
    src/application/k8s/store_scope.cpp
    src/application/k8s/object_store.cpp
    src/adapters/k8s/kubeconfig_loader.cpp
    src/adapters/k8s/cluster_binding.cpp
    src/adapters/k8s/http_kube_client.cpp
)

add_library(antares_k8s STATIC ${ANTARES_K8S_SOURCES})
target_link_libraries(antares_k8s PUBLIC antares_http)
set_target_properties(antares_k8s PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

set(ANTARES_IO_SOURCES
    src/ports/io/io_engine.cpp
    src/ports/io/io_stats.cpp
    src/ports/io/io_handle.cpp
    src/adapters/platform/io/engine_base.cpp
    src/adapters/platform/io/poll_engine.cpp
    src/application/io/engine_factory.cpp
    src/application/io/engine_pool.cpp
)

# The kernel engine is the platform's own: kqueue on Darwin, io_uring with an
# epoll fallback on Linux. `poll` stays compiled everywhere as the last resort
# (ADR 0009, ADR 0018).
if(APPLE)
    list(APPEND ANTARES_IO_SOURCES src/adapters/platform/io/kqueue_engine.cpp
                                   src/platform/mac/thread_tuning.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND ANTARES_IO_SOURCES src/adapters/platform/io/io_uring_engine.cpp
                                   src/adapters/platform/io/epoll_engine.cpp
                                   src/platform/linux/thread_tuning.cpp)
else()
    list(APPEND ANTARES_IO_SOURCES src/platform/linux/thread_tuning.cpp)
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
    COMMAND ${CMAKE_SOURCE_DIR}/tools/check-tidy.sh ${CMAKE_BINARY_DIR}
    COMMENT "antares: lint rules"
    VERBATIM
    USES_TERMINAL)

add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel ${ANTARES_JOBS}
    COMMENT "antares: test suite"
    VERBATIM
    USES_TERMINAL)
# The lint target is not a dependency: the CI runs it as its own job, and
# running it twice per pipeline buys nothing.
add_dependencies(check antares_io_conformance antares_io_hosted antares_ui_conformance
                 antares_http_conformance antares_k8s_conformance)

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

# Throwaway micro-benchmark: completion cost against in-flight operations.
add_executable(antares_io_bench tools/bench_io.cpp)
target_link_libraries(antares_io_bench PRIVATE antares_io)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(antares_io_bench PRIVATE PkgConfig::LIBURING)
endif()
if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
    target_include_directories(antares_io_bench PRIVATE "${ANTARES_MIMALLOC_INCLUDE}")
    target_link_libraries(antares_io_bench PRIVATE mimalloc-static)
endif()
target_include_directories(antares_io_conformance PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(antares_io_conformance PRIVATE antares_io)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(antares_io_conformance PRIVATE PkgConfig::LIBURING)
endif()

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

# The hosted-mode suite: the same port driven by a `poll()` host instead of
# `run()`. It is the Linux gate for the mode ADR-0020 decides, so a Darwin-only
# assumption surfaces here (ADR-0020, feature 007).
add_executable(antares_io_hosted tests/io/hosted.cpp)
target_link_libraries(antares_io_hosted PRIVATE antares_io)
target_include_directories(antares_io_hosted PRIVATE ${CMAKE_SOURCE_DIR}/tests)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(antares_io_hosted PRIVATE PkgConfig::LIBURING)
endif()
if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
    if(APPLE)
        target_link_options(antares_io_hosted PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:mimalloc-static>")
    else()
        target_link_libraries(antares_io_hosted PRIVATE mimalloc-static)
    endif()
endif()
set_target_properties(antares_io_hosted PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(APPLE)
    add_custom_command(TARGET antares_io_hosted POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:antares_io_hosted>
        COMMENT "dsymutil antares_io_hosted"
        VERBATIM)
endif()

# One CTest entry per case, so `ctest -j` fans the suite across cores.
set(ANTARES_IO_CASES
    post-runs-on-engine-thread
    socket-read-write
    file-read-immediate
    stop-ends-callbacks
    no-callback-after-stop
    cancel-pending-keeps-socket
    counters-move
    backend-name-matches-engine
    queue-four-producers
    queue-deadline-keeps-order
    cancel-walks-under-lock
    stop-settles-pending
    stop-is-reversible
    resolve-two-jobs-one-window
    stats-cancel-counts
    forget-drops-registration)

add_executable(antares_http_conformance tests/http/conformance.cpp)
target_link_libraries(antares_http_conformance PRIVATE antares_http)
target_include_directories(antares_http_conformance PRIVATE ${CMAKE_SOURCE_DIR}/tests/http ${CMAKE_SOURCE_DIR}/tests)
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

add_executable(antares_k8s_conformance tests/k8s/conformance.cpp
                                tests/support/counting_allocator.cpp)
target_link_libraries(antares_k8s_conformance PRIVATE antares_k8s)
target_include_directories(antares_k8s_conformance PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_definitions(antares_k8s_conformance
    PRIVATE ANTARES_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/k8s/fixtures")
set_target_properties(antares_k8s_conformance PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
# No mimalloc here: the suite replaces the global `operator new`/`delete` with a
# counting pair (tests/support/counting_allocator.cpp) and the two would collide.
# The allocation pattern under test is the store's, not the allocator's.
if(APPLE)
    add_custom_command(TARGET antares_k8s_conformance POST_BUILD
        COMMAND dsymutil $<TARGET_FILE:antares_k8s_conformance>
        COMMENT "dsymutil antares_k8s_conformance"
        VERBATIM)
endif()


if(Qt6_FOUND)
    # The live window binary. Without the toolkit the tree still builds.
    add_executable(antares_app src/adapters/ui/main.cpp)
    if(TARGET Qt6::lupdate)
        qt_add_translations(antares_app TS_FILES translations/antares_pt_BR.ts)
    endif()
    target_link_libraries(antares_app PRIVATE antares_ui)
    set_target_properties(antares_app PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
        target_link_libraries(antares_app PRIVATE mimalloc-static)
        if(APPLE)
            target_link_options(antares_app PRIVATE
                "LINKER:-force_load,$<TARGET_FILE:mimalloc-static>")
        endif()
    endif()

    add_executable(antares_ui_conformance tests/ui/conformance.cpp)
    target_link_libraries(antares_ui_conformance PRIVATE antares_ui)
    target_include_directories(antares_ui_conformance PRIVATE ${CMAKE_SOURCE_DIR}/tests
                                                              ${CMAKE_SOURCE_DIR}/tests/ui)
    set_target_properties(antares_ui_conformance PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
        target_link_libraries(antares_ui_conformance PRIVATE mimalloc-static)
        if(APPLE)
            target_link_options(antares_ui_conformance PRIVATE
                "LINKER:-force_load,$<TARGET_FILE:mimalloc-static>")
        endif()
    endif()

    set(ANTARES_UI_CASES
        build-and-shutdown
        two-instances
        sigterm-returns
        model-footprint-under-4k
        multi-data-fills-span
        status-text-falls-back
        fetch-inserts-one-range
        fetch-asks-the-limit
        can-fetch-more-needs-a-token
        change-set-applies-in-order
        empty-mask-repaints-nothing
        status-change-repaints-status
        reorder-is-a-move
        move-of-last-row-is-silent
        view-config-batched-and-fixed
        view-config-takes-saved-layout
        uniform-flag-flips-on-varied-row
        cross-thread-change-set-queues
        fetch-failure-shows-retry-row
        retry-does-not-loop
        truncation-is-reported
        role-index-matches-cue
        roles-for-mask)

    foreach(case IN LISTS ANTARES_UI_CASES)
        add_test(NAME "ui/${case}" COMMAND antares_ui_conformance "${case}")
        # No display here: the offscreen plugin is what makes a QApplication start
        # without one.
        set_tests_properties("ui/${case}" PROPERTIES TIMEOUT 30
                                                      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()

# The live read-only smoke. Not a ctest entry: it needs a real cluster.
add_executable(antares_k8s_live tools/k8s_live/main.cpp)
target_link_libraries(antares_k8s_live PRIVATE antares_k8s)
set_target_properties(antares_k8s_live PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(ANTARES_ALLOCATOR STREQUAL "mimalloc")
    target_link_libraries(antares_k8s_live PRIVATE mimalloc-static)
    if(APPLE)
        target_link_options(antares_k8s_live PRIVATE
            "LINKER:-force_load,$<TARGET_FILE:mimalloc-static>")
    endif()
endif()

set(ANTARES_K8S_CASES
    core-group-path
    named-group-path
    namespace-segment
    subresource-segment
    hostile-name-refused
    name-classes
    name-refusal-reason
    store-path-agreement
    list-options-rendered
    list-options-empty
    patch-content-types
    delete-options-body
    not-found-mapped
    unexpected-body-mapped
    message-bounded
    bookmark-advances
    expired-stream
    frames-split-across-chunks
    keep-alive-skipped
    kubeconfig-current-context
    kubeconfig-failures
    bearer-credential
    json-round-trip
    json-refuses-truncation
    discovery-parses-resources
    discovery-cluster-scoped
    discovery-core-versions
    discovery-groups
    exec-frame-round-trip
    exec-empty-payload
    exec-unknown-channel
    exec-resize-payload
    exec-error-frame
    frame-cursor-compacts-once
    entry-projection
    entry-refuses-hostile
    entry-labels-truncated
    entry-worst-case-bound
    store-bounds-5000-pods
    store-shares-entry
    store-upsert-in-place
    store-change-set-replays
    store-admits-until-ceiling
    store-relist-reconciles
    store-relist-abandoned
    store-routes-events
    store-metrics-bounded)

foreach(case IN LISTS ANTARES_K8S_CASES)
    add_test(NAME "k8s/${case}" COMMAND antares_k8s_conformance "${case}")
    set_tests_properties("k8s/${case}" PROPERTIES TIMEOUT 30)
endforeach()

set(ANTARES_HTTP_CASES
    async-get
    sync-get
    redirect-followed
    reentrant-from-callback
    pool-reuses-connection
    retries-then-succeeds
    retry-stops-at-ceiling
    post-not-retried
    idle-timer-fires
    pool-spreads-across-loops
    pool-release-races
    https-negotiates-h2
    https-falls-back-to-h11
    h2-multiplexes-one-connection
    h1-streams-chunks
    h2-streams-data
    bearer-is-sent
    basic-encodes-credential
    verifies-against-trusted-ca
    rejects-untrusted-ca
    cancel-completes-once
    resolves-name-off-loop
    unresolvable-name-fails
    sse-parses-events
    sse-ceiling-holds
    ws-round-trips-frames
    ws-splits-across-reads
    ws-reassembles-fragments
    ws-rejects-rsv
    json-sequence-splits
    json-sequence-drops-truncated-number
    sse-strips-bom
    ws-close-closes
    websocket-upgrades
    ws-refuses-bad-accept
    ws-accept-matches-rfc
    streaming-survives-redirect
    streaming-survives-retry
    logger-receives-failures
    ws-close-handshake
    hundred-streams-stay-unblocked
    content-encoding-gzip
    content-encoding-gzip-multi-member
    content-encoding-stacked
    content-encoding-zstd
    content-encoding-brotli
    redirect-drops-lowercase-auth
    priority-clamps-to-seven
    cookie-round-trips
    cookie-rejects-foreign-domain
    cookie-expires
    cookie-path-boundary
    cache-revalidates
    cache-needs-validator
    ws-permessage-deflate
    content-encoding-unknown-passes-through
    accept-encoding-is-offered)

foreach(case IN LISTS ANTARES_HTTP_CASES)
    add_test(NAME "http/${case}" COMMAND antares_http_conformance "${case}")
    # A hang must fail the suite, not block it.
    set_tests_properties("http/${case}" PROPERTIES TIMEOUT 30)
endforeach()

foreach(case IN LISTS ANTARES_IO_CASES)
    add_test(NAME "poll/${case}" COMMAND antares_io_conformance "${case}")
    set_tests_properties("poll/${case}" PROPERTIES ENVIRONMENT "ANTARES_IO_BACKEND=poll" TIMEOUT 30)
    if(APPLE)
        add_test(NAME "kqueue/${case}" COMMAND antares_io_conformance "${case}")
        set_tests_properties("kqueue/${case}" PROPERTIES ENVIRONMENT "ANTARES_IO_BACKEND=kqueue" TIMEOUT 30)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME "uring/${case}" COMMAND antares_io_conformance "${case}")
        set_tests_properties("uring/${case}" PROPERTIES ENVIRONMENT "ANTARES_IO_BACKEND=io_uring" TIMEOUT 30)
        add_test(NAME "epoll/${case}" COMMAND antares_io_conformance "${case}")
        set_tests_properties("epoll/${case}" PROPERTIES ENVIRONMENT "ANTARES_IO_BACKEND=epoll" TIMEOUT 30)
    endif()
endforeach()

set(ANTARES_IO_HOSTED_CASES
    turn-runs-on-host-thread
    post-waits-for-the-host
    post-wakes-the-handle
    next-deadline-tracks-timers
    turn-honours-its-budget
    stop-from-callback-does-not-deadlock
    no-completion-starts-after-stop
    shutdown-settles-pending
    wait-handle-is-pollable)

foreach(case IN LISTS ANTARES_IO_HOSTED_CASES)
    add_test(NAME "hosted-poll/${case}" COMMAND antares_io_hosted "${case}")
    set_tests_properties("hosted-poll/${case}" PROPERTIES
        ENVIRONMENT "ANTARES_IO_BACKEND=poll" TIMEOUT 30)
    if(APPLE)
        add_test(NAME "hosted-kqueue/${case}" COMMAND antares_io_hosted "${case}")
        set_tests_properties("hosted-kqueue/${case}" PROPERTIES
            ENVIRONMENT "ANTARES_IO_BACKEND=kqueue" TIMEOUT 30)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME "hosted-uring/${case}" COMMAND antares_io_hosted "${case}")
        set_tests_properties("hosted-uring/${case}" PROPERTIES
            ENVIRONMENT "ANTARES_IO_BACKEND=io_uring" TIMEOUT 30)
        add_test(NAME "hosted-epoll/${case}" COMMAND antares_io_hosted "${case}")
        set_tests_properties("hosted-epoll/${case}" PROPERTIES
            ENVIRONMENT "ANTARES_IO_BACKEND=epoll" TIMEOUT 30)
    endif()
endforeach()

# Linux only: preloaded over the conformance binary so `io_uring_queue_init`
# fails and the factory has to fall back to epoll (feature 004, FR-010).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT APPLE)
    add_library(antares_io_uring_shim SHARED tests/io/io_uring_shim.cpp)
    target_link_libraries(antares_io_uring_shim PRIVATE PkgConfig::LIBURING)
    # Not in the loop above: without the shim this case fails, so it gets its
    # own registration instead of running on every backend.
    add_test(NAME "shim/ring-refused-falls-back"
             COMMAND antares_io_conformance ring-refused-falls-back)
    set_tests_properties("shim/ring-refused-falls-back" PROPERTIES
        ENVIRONMENT "LD_PRELOAD=$<TARGET_FILE:antares_io_uring_shim>;ANTARES_IO_BACKEND="
        TIMEOUT 30)
endif()
