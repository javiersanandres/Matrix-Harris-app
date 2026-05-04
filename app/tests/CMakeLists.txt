# ============================================================================
# Unit Tests for app layer
# ============================================================================

add_executable(app_logic_tests
    # Add test source files here as you create them
    # test_example.cpp
)

target_include_directories(app_logic_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../
)

target_link_libraries(app_logic_tests
    PRIVATE
        editors
        graphical_engine
        GTest::gtest
        GTest::gtest_main
)

# Discover and register all tests
include(GoogleTest)
gtest_discover_tests(app_logic_tests
    DISCOVERY_MODE PRE_TEST
)