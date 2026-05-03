# ============================================================================
# Core Libraries
# ============================================================================

# Node library
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

# Hyperedge library
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

# Hypergraph library
add_library(hypergraph)

target_sources(hypergraph
    PRIVATE
        Hypergraph.cpp
        minimizeCrossings.cpp
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
)


# ============================================================================
# Gurobi configuration (optional, required for horizontalOrder.cpp)
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
        message(WARNING "Gurobi not found at ${GUROBI_HOME}. horizontalOrder.cpp will not be compiled.")
        set(COMPILE_HORIZONTAL_ORDER FALSE)
    endif()
else()
    message(STATUS "Gurobi found via find_package")
    set(COMPILE_HORIZONTAL_ORDER TRUE)
endif()


# Graphical hypergraph library
add_library(graphical_hypergraph)
target_sources(graphical_hypergraph
    PRIVATE
        horizontalCoordinates.cpp
        portAssignment.cpp
        $<IF:$<BOOL:${Gurobi_FOUND}>,horizontalOrder.cpp,>
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
        $<IF:$<BOOL:${Gurobi_FOUND}>,${Gurobi_INCLUDE_DIRS},>
)
target_link_libraries(graphical_hypergraph
    PUBLIC hypergraph
    $<IF:$<BOOL:${Gurobi_FOUND}>,${Gurobi_LIBRARIES},>
)

# Enable BK_TEST when building tests
if(BUILD_TESTING)
    target_compile_definitions(graphical_hypergraph PRIVATE BK_TEST)
endif()

# Keep the analysis separate from the core libraries.
add_subdirectory(implementationAnalysis/minimizeCrossings)
add_subdirectory(implementationAnalysis/horizontalCoordinates)
add_subdirectory(implementationAnalysis/horizontalOrder)