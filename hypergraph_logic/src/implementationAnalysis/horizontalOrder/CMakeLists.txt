# ============================================================================
# JSON (nlohmann) - header-only, fetched from official release tarball
# ============================================================================
include(FetchContent)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

add_executable(horizontalOrderResults
    horizontalOrderResults.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalOrder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GurobiRuntime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalCoordinates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hyperedge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hypergraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraphBlocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraphPersistence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../portAssignment.cpp
)

target_include_directories(horizontalOrderResults
    PRIVATE
        ${CMAKE_SOURCE_DIR}/hypergraph_logic/include
)

# HiGHS -- the highs::highs target already exists at this point in the build
# (fetched once, near the top level, by the same FetchContent block the
# "hypergraph" and "graphical_hypergraph" libraries link against), so this is
# just linking to it, not fetching it again. Always linked: it's the always-
# available fallback backend for horizontalOrder.cpp's MIP, and the
# ILP-HiGHS benchmark column below always exercises it.
target_link_libraries(horizontalOrderResults
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        highs::highs
)

# Gurobi needs nothing here: GurobiRuntime.cpp loads whatever version is
# installed at run time, so the ILP-Gurobi column works wherever Gurobi is
# installed and licensed.
target_link_libraries(horizontalOrderResults PRIVATE ${CMAKE_DL_LIBS})


if(MSVC)
    target_compile_options(horizontalOrderResults PRIVATE
        $<IF:$<CONFIG:Debug>,/MDd,/MD>
    )
else()
    target_compile_options(horizontalOrderResults PRIVATE -O2)
endif()