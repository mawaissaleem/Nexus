cmake_minimum_required(VERSION 3.22)
project(nexus VERSION 0.1.0 LANGUAGES CXX C)

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

# Core library sources
set(NEXUS_CORE_SOURCES
    src/utils/logger.cpp
    src/utils/fuzzy.cpp
    src/core/query.cpp
    src/core/executor.cpp
    src/core/ranking_engine.cpp
    src/core/search_engine.cpp
    src/core/async_search_coordinator.cpp
    src/index/desktop_entry.cpp
    src/index/database.cpp
    src/index/app_indexer.cpp
    src/providers/app_provider.cpp
    src/providers/calc_provider.cpp
    src/providers/shell_provider.cpp
    src/core/config_manager.cpp
    src/core/alias_manager.cpp
    src/providers/file_provider.cpp
    src/providers/alias_provider.cpp
)

# Core static library
add_library(nexus_core STATIC ${NEXUS_CORE_SOURCES})

target_include_directories(nexus_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(nexus_core PUBLIC
    Threads::Threads
    SQLite::SQLite3
    stdc++fs
)

# UI Sources and Headers for AUTOMOC
set(NEXUS_UI_HEADERS
    include/nexus/ui/global_shortcut.hpp
    include/nexus/ui/result_widget.hpp
    include/nexus/ui/launcher_window.hpp
    include/nexus/ui/settings_dialog.hpp
    include/nexus/ui/query_manager.hpp
)

set(NEXUS_UI_SOURCES
    src/ui/global_shortcut.cpp
    src/ui/result_widget.cpp
    src/ui/launcher_window.cpp
    src/ui/settings_dialog.cpp
    src/ui/query_manager.cpp
)

# Main Application Executable (GUI + CLI fallback)
add_executable(nexus
    src/main.cpp
    ${NEXUS_UI_SOURCES}
    ${NEXUS_UI_HEADERS}
)

target_include_directories(nexus PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nexus PRIVATE
    nexus_core
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

# Testing
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
