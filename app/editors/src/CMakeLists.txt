# ============================================================================
# editors library
#
# Contains HypergraphEditor, JointHypergraphEditor, and Project.
# Depends on hypergraph_logic (which already includes JointGraphicalHypergraph
# and GraphicalHypergraph).
# ============================================================================

add_library(editors)

target_sources(editors
    PRIVATE
        HypergraphEditor.cpp
        JointHypergraphEditor.cpp
        Project.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES
            ../include/HypergraphEditorBase.h
            ../include/HypergraphEditor.h
            ../include/JointHypergraphEditor.h
            ../include/Project.h
)

target_include_directories(editors
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(editors
    PUBLIC hypergraph_logic
)