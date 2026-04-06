# ============================================================================
# Unit Tests
# ============================================================================

add_executable(hypergraph_logic_tests
    test_node.cpp
    test_hyperedge.cpp
    
    
    test_hypergraph_connection.cpp
)

target_include_directories(hypergraph_logic_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(hypergraph_logic_tests
    PRIVATE
        hypergraph_logic
        GTest::gtest
        GTest::gtest_main
)

# Discover and register all tests
include(GoogleTest)
gtest_discover_tests(hypergraph_logic_tests
    DISCOVERY_MODE PRE_TEST
)