# ============================================================================
# Unit Tests
# ============================================================================

add_executable(hypergraph_logic_tests
    test_node.cpp
    test_hyperedge.cpp
    test_connection_management.cpp
    test_hypergraph_internals.cpp 
    test_minimize_crossings.cpp 
    test_minimize_crossings_hypergraph.cpp 
    test_horizontal_coordinates.cpp
    test_port_assignment.cpp 
    test_horizontal_order.cpp "test_graphicalhypergraph_persistence.cpp")

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

target_compile_definitions(hypergraph_logic_tests PRIVATE BK_TEST GS_TEST)

# Discover and register all tests
include(GoogleTest)
gtest_discover_tests(hypergraph_logic_tests
    DISCOVERY_MODE PRE_TEST
)