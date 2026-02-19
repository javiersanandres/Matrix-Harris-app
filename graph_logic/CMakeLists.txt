add_subdirectory(src)

add_library(graph_logic INTERFACE)

target_link_libraries(graph_logic
    INTERFACE
        node
        diagram
)

# Tests
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
