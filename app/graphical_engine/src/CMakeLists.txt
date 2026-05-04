# ============================================================================
# graphical_engine library
#
# Converts GraphicalHypergraph layout data into QGraphicsScene items.
# Depends on hypergraph_logic for layout types and Qt6::Widgets for the
# QGraphics* classes.
# ============================================================================

find_package(Qt6 REQUIRED COMPONENTS Widgets)
list(APPEND CMAKE_PREFIX_PATH "C:/Qt2/6.11.0/msvc2022_64")

add_library(graphical_engine)

target_sources(graphical_engine
    PRIVATE
        HypergraphRenderer.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES
            ../include/HypergraphRenderer.h
)

target_include_directories(graphical_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphical_engine
    PUBLIC
        hypergraph_logic
        Qt6::Widgets
)

# Required for any target that uses Qt's meta-object system.
set_target_properties(graphical_engine PROPERTIES AUTOMOC ON)