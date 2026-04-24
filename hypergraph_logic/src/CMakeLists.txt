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


# Graphical hypergraph library
add_library(graphical_hypergraph)
target_sources(graphical_hypergraph
    PRIVATE
        minimizeCrossings.cpp
        horizontalCoordinates.cpp
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
    PUBLIC hypergraph
)

# Enable BK_TEST when building tests
if(BUILD_TESTING)
    target_compile_definitions(graphical_hypergraph PRIVATE BK_TEST)
endif()

# For efficiency measures, keep them separate from the core libraries.
add_subdirectory(efficiencyMeasures/minimizeCrossings)