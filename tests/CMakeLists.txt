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
)

foreach(test_name ${TEST_TARGETS})
    add_executable(${test_name} ${test_name}.cpp)
    target_compile_options(${test_name} PRIVATE -UNDEBUG)
    target_link_libraries(${test_name} PRIVATE nexus_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()


# test_theme scans src/ui for stray colour literals.
target_compile_definitions(test_theme PRIVATE NEXUS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
