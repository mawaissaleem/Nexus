# Test suite CMake configuration

set(TEST_TARGETS
    test_fuzzy
    test_desktop_entry
    test_ranking
    test_ranking_eval
    test_calculator
    test_database
    test_alias
    test_file_search
    test_config
    test_json
    test_theme
    test_query
    test_executor
    test_action_dispatcher
    test_search_engine
    test_async_search_coordinator
    test_database_migrations
    test_shortcut
    test_app_indexer
    test_app_provider
    test_file_indexer
    test_file_watcher
    test_app_watcher
    test_cli
    test_plugin_protocol
    test_plugin_provider
    test_plugin_loader
    test_launcher_copy
)

# Each test links the smallest layer it needs (see CMakeLists.txt), so layering is enforced at link time.
set(TEST_LIBS_DEFAULT nexus_providers)
set(TEST_LIBS_test_fuzzy nexus_utils)
set(TEST_LIBS_test_json nexus_utils)
set(TEST_LIBS_test_theme nexus_utils)
set(TEST_LIBS_test_shortcut nexus_utils)
set(TEST_LIBS_test_query nexus_core)
set(TEST_LIBS_test_executor nexus_core)
set(TEST_LIBS_test_action_dispatcher nexus_core)
set(TEST_LIBS_test_config nexus_core)
set(TEST_LIBS_test_async_search_coordinator nexus_core)
set(TEST_LIBS_test_database nexus_index)
set(TEST_LIBS_test_database_migrations nexus_index)
set(TEST_LIBS_test_app_indexer nexus_index)
set(TEST_LIBS_test_app_watcher nexus_index)
set(TEST_LIBS_test_desktop_entry nexus_core nexus_index)
set(TEST_LIBS_test_file_watcher nexus_index)
set(TEST_LIBS_test_cli nexus_app)
set(TEST_LIBS_test_plugin_loader nexus_app)
set(TEST_LIBS_test_launcher_copy nexus_core)

foreach(test_name ${TEST_TARGETS})
    add_executable(${test_name} ${test_name}.cpp)
    target_compile_options(${test_name} PRIVATE -UNDEBUG)
    if(DEFINED TEST_LIBS_${test_name})
        target_link_libraries(${test_name} PRIVATE ${TEST_LIBS_${test_name}})
    else()
        target_link_libraries(${test_name} PRIVATE ${TEST_LIBS_DEFAULT})
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# Include-level layering check: catches header-only and unlinked wrong-direction includes.
add_test(NAME test_layering COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_layering.cmake)


target_compile_definitions(test_action_dispatcher PRIVATE NEXUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# test_theme scans src/ui for stray colour literals.
target_compile_definitions(test_theme PRIVATE NEXUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Qt-linked UI test: compiles the one ui source it needs and runs on the offscreen platform (no X server).
target_sources(test_launcher_copy PRIVATE ${CMAKE_SOURCE_DIR}/src/ui/copy_result.cpp)
target_link_libraries(test_launcher_copy PRIVATE Qt${QT_VERSION_MAJOR}::Gui)
set_tests_properties(test_launcher_copy PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
