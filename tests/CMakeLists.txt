# Test suite CMake configuration

set(TEST_TARGETS
    test_fuzzy
    test_desktop_entry
    test_ranking
    test_calculator
    test_database
    test_alias
    test_file_search
    test_config
    test_query
    test_executor
    test_search_engine
)

foreach(test_name ${TEST_TARGETS})
    add_executable(${test_name} ${test_name}.cpp)
    target_compile_options(${test_name} PRIVATE -UNDEBUG)
    target_link_libraries(${test_name} PRIVATE nexus_core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

