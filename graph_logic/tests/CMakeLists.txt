add_executable(graph_logic_tests test_node.cpp)

target_link_libraries(graph_logic_tests
    PRIVATE
        graph_logic
        GTest::gtest
        GTest::gtest_main
)

target_compile_features(graph_logic_tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(graph_logic_tests)

