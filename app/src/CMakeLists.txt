add_library(app_logic)

target_sources(app_logic
    PRIVATE
        HypergraphEditor.cpp
        JointHypergraphEditor.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES
            ../include/HypergraphEditorBase.h
            ../include/HypergraphEditor.h
            ../include/JointHypergraphEditor.h
)

target_include_directories(app_logic
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(app_logic
    PUBLIC hypergraph_logic
)