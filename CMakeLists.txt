cmake_minimum_required(VERSION 3.10)

project(Matrix-Harris-app 
    VERSION 0.1.0 
    LANGUAGES CXX
    DESCRIPTION "A hierarchical hypergraph visualization and manipulation tool"
)

# ============================================================================
# Global Configuration
# ============================================================================

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# Project Options
# ============================================================================

option(BUILD_TESTING "Enable tests" ON)
option(BUILD_GRAPHICAL_ENGINE "Build graphical engine" OFF)

# ============================================================================
# Testing
# ============================================================================

include(CTest)
enable_testing()

if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# ============================================================================
# Subdirectories
# ============================================================================

add_subdirectory(hypergraph_logic)
add_subdirectory(app)

if(BUILD_GRAPHICAL_ENGINE)
    add_subdirectory(graphical_engine)
endif()
