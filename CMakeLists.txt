cmake_minimum_required(VERSION 3.22)
project(nexus VERSION 0.4.0 LANGUAGES CXX C)

# Enforce C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt Meta-Object Compiler & Resources
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Dependencies
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

# Qt: developed and tested against Qt 5.15.3. Qt6 is supported and built in CI
# but has had less real-world testing — watch for API differences (see README).
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)
message(STATUS "Using Qt version: ${QT_VERSION_MAJOR} (${Qt${QT_VERSION_MAJOR}_VERSION})")

# Platform specific dependencies
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
endif()

# Layered static libraries. Direction (checked against the real include graph, PA.6):
#   nexus_utils <- nexus_core      (query, ranking, search engine, executor, config, aliases)
#   nexus_utils <- nexus_index     (database, indexers, watchers; does not use core)
#   nexus_core + nexus_index <- nexus_providers <- nexus_app <- nexus (ui, Qt)
# core and index are siblings. A lower layer calling an upper one links only in a binary that
# also pulls the upper library, so tests link the smallest layer they need: a wrong-direction
# call then fails with an undefined reference. Only nexus (ui) links Qt.
add_library(nexus_utils STATIC
    src/utils/logger.cpp
    src/utils/fuzzy.cpp
    src/utils/shortcut.cpp
    src/utils/json.cpp
    src/utils/theme.cpp
    src/utils/fs_watcher.cpp
)
target_include_directories(nexus_utils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(nexus_utils PUBLIC Threads::Threads stdc++fs)

add_library(nexus_core STATIC
    src/core/query.cpp
    src/core/executor.cpp
    src/core/action_dispatcher.cpp
    src/core/ranking_engine.cpp
    src/core/search_engine.cpp
    src/core/async_search_coordinator.cpp
    src/core/config_manager.cpp
    src/core/alias_manager.cpp
)
target_link_libraries(nexus_core PUBLIC nexus_utils)

add_library(nexus_index STATIC
    src/index/desktop_entry.cpp
    src/index/database.cpp
    src/index/app_indexer.cpp
    src/index/file_indexer.cpp
    src/index/file_watcher.cpp
)
target_link_libraries(nexus_index PUBLIC nexus_utils SQLite::SQLite3)

add_library(nexus_providers STATIC
    src/providers/app_provider.cpp
    src/providers/calc_provider.cpp
    src/providers/shell_provider.cpp
    src/providers/file_provider.cpp
    src/providers/alias_provider.cpp
)
target_link_libraries(nexus_providers PUBLIC nexus_core nexus_index)

add_library(nexus_app STATIC
    src/app/services.cpp
    src/app/cli.cpp
)
target_link_libraries(nexus_app PUBLIC nexus_providers)

# UI Sources and Headers for AUTOMOC
set(NEXUS_UI_HEADERS
    include/nexus/ui/global_shortcut.hpp
    include/nexus/ui/result_widget.hpp
    include/nexus/ui/launcher_window.hpp
    include/nexus/ui/settings_dialog.hpp
    include/nexus/ui/query_manager.hpp
    include/nexus/ui/theme.hpp
    include/nexus/ui/gui_app.hpp
)

set(NEXUS_UI_SOURCES
    src/ui/global_shortcut.cpp
    src/ui/result_widget.cpp
    src/ui/launcher_window.cpp
    src/ui/settings_dialog.cpp
    src/ui/query_manager.cpp
    src/ui/theme.cpp
    src/ui/gui_app.cpp
)

# Main Application Executable (GUI + CLI fallback)
add_executable(nexus
    src/main.cpp
    ${NEXUS_UI_SOURCES}
    ${NEXUS_UI_HEADERS}
)

target_link_libraries(nexus PRIVATE
    nexus_app
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
)

if(UNIX AND NOT APPLE)
    target_link_libraries(nexus PRIVATE X11::X11)
elseif(WIN32)
    target_link_libraries(nexus PRIVATE user32 shell32)
endif()

# Benchmarks: built by default so they keep compiling, but never registered with ctest.
option(NEXUS_BUILD_BENCH "Build bench/bench_nexus" ON)
if(NEXUS_BUILD_BENCH)
    add_executable(bench_nexus bench/bench_nexus.cpp)
    target_compile_options(bench_nexus PRIVATE -UNDEBUG)
    target_link_libraries(bench_nexus PRIVATE nexus_providers)
endif()

# Testing
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
