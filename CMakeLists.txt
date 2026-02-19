cmake_minimum_required(VERSION 3.10)

project(Matrix-Harris-app VERSION 0.1.0 LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTING "Enable tests" ON)
option(BUILD_GRAPHICAL_ENGINE "Build graphical engine" OFF)

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/graph_logic/CMakeLists.txt")
    add_subdirectory(graph_logic)
endif()

if(BUILD_GRAPHICAL_ENGINE AND
   EXISTS "${CMAKE_SOURCE_DIR}/graphical_engine/CMakeLists.txt")
    add_subdirectory(graphical_engine)
endif()
