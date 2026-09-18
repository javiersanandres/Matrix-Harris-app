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
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES ../include/Hypergraph.h)

target_include_directories(hypergraph
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(hypergraph
    PUBLIC node hyperedge
    PRIVATE highs::highs
)


# ============================================================================
# Gurobi configuration (optional -- speeds up minimizeCrossingsILP.cpp and
# horizontalOrder.cpp when a valid license is available; both fall back to
# HiGHS otherwise, so neither requires Gurobi to build or run).
# ============================================================================

# Try to find Gurobi
find_package(Gurobi QUIET)

if(NOT Gurobi_FOUND)
    # If find_package fails, set up Gurobi manually
    set(GUROBI_HOME "C:/gurobi1301/win64" CACHE PATH "Gurobi installation directory")

    if(EXISTS "${GUROBI_HOME}")
        set(Gurobi_INCLUDE_DIRS "${GUROBI_HOME}/include")

        # Select the correct C++ library based on build type
        # mdd2017 = debug, dynamic runtime
        # md2017 = release, dynamic runtime
        set(Gurobi_CPP_LIB_DEBUG "${GUROBI_HOME}/lib/gurobi_c++mdd2017.lib")
        set(Gurobi_CPP_LIB_RELEASE "${GUROBI_HOME}/lib/gurobi_c++md2017.lib")
        set(Gurobi_C_LIB "${GUROBI_HOME}/lib/gurobi130.lib")

        # Use generator expressions to select the right library
        set(Gurobi_LIBRARIES 
            $<IF:$<CONFIG:Debug>,${Gurobi_CPP_LIB_DEBUG},${Gurobi_CPP_LIB_RELEASE}>
            ${Gurobi_C_LIB}
        )

        set(Gurobi_FOUND TRUE)
        message(STATUS "Gurobi found at: ${GUROBI_HOME}")
        message(STATUS "  Debug C++ lib: ${Gurobi_CPP_LIB_DEBUG}")
        message(STATUS "  Release C++ lib: ${Gurobi_CPP_LIB_RELEASE}")
        message(STATUS "  C lib: ${Gurobi_C_LIB}")
    else()
        message(STATUS "Gurobi not found at ${GUROBI_HOME} -- minimizeCrossingsILP.cpp "
                        "and horizontalOrder.cpp will both use HiGHS only.")
    endif()
else()
    message(STATUS "Gurobi found via find_package")
endif()

# Let minimizeCrossingsILP.cpp prefer Gurobi over HiGHS when it's available.
# GUROBI_AVAILABLE only means the SDK was found at configure time -- it says
# nothing about whether a valid license is present on the machine that
# eventually runs the binary. That's a runtime question (a license server can
# be unreachable, a token/named-user license can expire, etc.), so the .cpp
# checks for a working license itself at runtime and falls back to HiGHS
# whenever that check fails, even though this flag is set.
if(Gurobi_FOUND)
    target_compile_definitions(hypergraph PRIVATE GUROBI_AVAILABLE)
    target_include_directories(hypergraph PRIVATE ${Gurobi_INCLUDE_DIRS})
    target_link_libraries(hypergraph PRIVATE ${Gurobi_LIBRARIES})
endif()


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

# horizontalOrder.cpp (compiled directly into this library, see target_sources
# above) prefers Gurobi over HiGHS when it's available -- same runtime-checked
# arrangement as minimizeCrossingsILP.cpp in the hypergraph library, and the
# same caveat: GUROBI_AVAILABLE only means the SDK was found at configure
# time, not that a license is actually usable on the machine that runs the
# binary (that's checked at runtime, in horizontalOrder.cpp itself).
if(Gurobi_FOUND)
    target_compile_definitions(graphical_hypergraph PRIVATE GUROBI_AVAILABLE)
    target_include_directories(graphical_hypergraph PRIVATE ${Gurobi_INCLUDE_DIRS})
    target_link_libraries(graphical_hypergraph PRIVATE ${Gurobi_LIBRARIES})
endif()

# Enable BK_TEST when building tests
if(BUILD_TESTING)
    target_compile_definitions(graphical_hypergraph PRIVATE BK_TEST JGH_TEST)
endif()

# Keep the analysis separate from the core libraries.
add_subdirectory(implementationAnalysis/minimizeCrossings)
add_subdirectory(implementationAnalysis/horizontalCoordinates)
add_subdirectory(implementationAnalysis/horizontalOrder)
add_subdirectory(implementationAnalysis/benchmarkAnalysis)