# ============================================================================
# Unit Tests for app layer
# ============================================================================

add_executable(app_logic_tests
    test_editors.cpp
    test_project.cpp
)

target_include_directories(app_logic_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../editors/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../ui/include
)

target_link_libraries(app_logic_tests
    PRIVATE
        editors
        ui_lib
        GTest::gtest
        GTest::gtest_main
)

# Discover and register all tests
include(GoogleTest)
gtest_discover_tests(app_logic_tests
    DISCOVERY_MODE PRE_TEST
)