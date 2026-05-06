# ============================================================================
# ui_lib — Qt UI layer including the hypergraph renderer
#
# HypergraphRenderer lives here (not in a separate graphical_engine library)
# because it depends on NodeItem and HyperedgeItem, which are Qt UI classes.
# Both overloads of render() are compiled into ui_lib:
#   - Static overload  : produces raw QGraphicsRectItem / QGraphicsPathItem.
#   - Factory overload : produces NodeItem / HyperedgeItem via caller lambdas.
# ============================================================================

find_package(Qt6 REQUIRED COMPONENTS Widgets)
list(APPEND CMAKE_PREFIX_PATH "C:/Qt2/6.11.0/msvc2022_64")

add_library(ui_lib)

target_sources(ui_lib
    PRIVATE
        HypergraphRenderer.cpp
        NodeItem.cpp
        HyperedgeItem.cpp
        DiagramScene.cpp
        DiagramView.cpp
        DiagramTabWidget.cpp
        DiagramTabBar.cpp
        Dialogs.cpp
        MainWindow.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES
            ../include/HypergraphRenderer.h
            ../include/NodeItem.h
            ../include/HyperedgeItem.h
            ../include/DiagramScene.h
            ../include/DiagramView.h
            ../include/DiagramTabWidget.h
            ../include/DiagramTabBar.h
            ../include/FuseNodesDialog.h
            ../include/AddHypergraphDialog.h
            ../include/MainWindow.h
)

target_include_directories(ui_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ui_lib
    PUBLIC
        editors
        hypergraph_logic
        Qt6::Widgets
)

set_target_properties(ui_lib PROPERTIES AUTOMOC ON)

# ── Main executable ───────────────────────────────────────────────────────────
add_executable(HypergraphApp main.cpp)
target_link_libraries(HypergraphApp PRIVATE ui_lib)
set_target_properties(HypergraphApp PROPERTIES AUTOMOC ON)