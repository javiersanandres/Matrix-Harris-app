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
    test_horizontal_order.cpp
    test_graphicalhypergraph_persistence.cpp
    test_graphicalhypergraph.cpp
    test_jointgraphicalhypergraph.cpp
    test_operation_feasibility.cpp
    test_highs_outcome.cpp
    test_gurobi_runtime.cpp
    test_relocation_sifting.cpp
    test_line_style.cpp
    test_joint_diagrams.cpp
    test_graph_blocks.cpp
    test_copy_paste.cpp
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
        highs::highs  # test_highs_outcome.cpp checks the shared HiGHS outcome rule directly
)

target_compile_definitions(hypergraph_logic_tests PRIVATE BK_TEST GS_TEST JGH_TEST)

# Discover and register all tests
include(GoogleTest)
gtest_discover_tests(hypergraph_logic_tests
    DISCOVERY_MODE PRE_TEST
)