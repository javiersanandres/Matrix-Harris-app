# ============================================================================
# Node library
# ============================================================================
add_library(node)

target_sources(node
    PRIVATE
        Node.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES ../include/Node.h)

target_include_directories(node
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

# ============================================================================
# Hyperedge library
# ============================================================================
add_library(hyperedge)

target_sources(hyperedge
    PRIVATE
        Hyperedge.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES ../include/Hyperedge.h)

target_include_directories(hyperedge
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hyperedge
    PUBLIC node
)

# ============================================================================
# HiGHS (MIT licensed MIP/LP solver) - header+source fetched, used privately
# by GlobalSiftingILP.cpp for the exact crossing-minimization ILP. Kept
# separate from the Gurobi setup below: that one is an optional, license-
# gated dependency for horizontalOrder.cpp; this one is always available and
# free, so minimizeCrossingsILP() never needs a license to build or run.
# ============================================================================
include(FetchContent)

FetchContent_Declare(
    highs
    GIT_REPOSITORY https://github.com/ERGO-Code/HiGHS.git
    GIT_TAG        v1.7.2   # bump if you want a newer release
)

# FAST_BUILD skips HiGHS' own docs/tests/extra binaries so this doesn't slow
# down your configure step. Belt-and-suspenders: also shield HiGHS from
# picking up this project's BUILD_TESTING=ON (used below for
# graphical_hypergraph) so it doesn't try to build its own test suite too.
set(FAST_BUILD ON CACHE BOOL "" FORCE)
set(_saved_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(highs)
set(BUILD_TESTING ${_saved_build_testing})

# ============================================================================
# Hypergraph library
# ============================================================================
add_library(hypergraph)

target_sources(hypergraph
    PRIVATE
        Hypergraph.cpp
        minimizeCrossings.cpp
        minimizeCrossingsILP.cpp
        GurobiRuntime.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES ../include/Hypergraph.h ../include/GurobiRuntime.h)

target_include_directories(hypergraph
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hypergraph
    PUBLIC node hyperedge
    PRIVATE highs::highs
)


# Gurobi is not linked: GurobiRuntime.cpp finds whatever version is installed
# at run time and loads it (see GurobiRuntime.h), so building needs no Gurobi
# SDK and the program runs, and falls back to HiGHS, without Gurobi. Loading
# it needs the platform's dynamic-loading library (none on Windows).
target_link_libraries(hypergraph PRIVATE ${CMAKE_DL_LIBS})


# ============================================================================
# JSON (nlohmann) - header-only, fetched from official release tarball
# ============================================================================
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

# ============================================================================
# graphical hypergraph library
# ============================================================================
add_library(graphical_hypergraph)

target_sources(graphical_hypergraph
    PRIVATE
        horizontalCoordinates.cpp
        portAssignment.cpp
        GraphicalHypergraph.cpp
        GraphicalHypergraphBlocks.cpp
        GraphicalHypergraphPersistence.cpp
        JointGraphicalHypergraph.cpp
        horizontalOrder.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES ../include/GraphicalHypergraph.h)

target_include_directories(graphical_hypergraph
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(graphical_hypergraph
    PUBLIC
        hypergraph
        nlohmann_json::nlohmann_json
    PRIVATE
        highs::highs
)

# Enable BK_TEST when building tests
if(BUILD_TESTING)
    target_compile_definitions(graphical_hypergraph PRIVATE BK_TEST JGH_TEST)
endif()

# Keep the analysis separate from the core libraries.
add_subdirectory(implementationAnalysis/minimizeCrossings)
add_subdirectory(implementationAnalysis/horizontalCoordinates)
add_subdirectory(implementationAnalysis/horizontalOrder)
add_subdirectory(implementationAnalysis/benchmarkAnalysis)