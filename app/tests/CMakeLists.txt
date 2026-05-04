# ============================================================================
# Unit Tests
# ============================================================================

add_executable(app_logic_tests)

target_include_directories(app_logic_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(app_logic_tests
    PRIVATE
        app_logic
        GTest::gtest
        GTest::gtest_main
)

# Discover and register all tests
include(GoogleTest)
gtest_discover_tests(app_logic_tests
    DISCOVERY_MODE PRE_TEST
)