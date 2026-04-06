# Graphical engine that consumes hypergraph_logic (views / display)
cmake_minimum_required(VERSION 3.10)

# Collect sources for the engine
file(GLOB_RECURSE GRAPHICAL_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
)

if(NOT GRAPHICAL_ENGINE_SOURCES)
    message(WARNING "No sources found in graphical_engine/src. Add sources or remove this subdirectory.")
else()
    add_executable(graphical_engine ${GRAPHICAL_ENGINE_SOURCES})

    # Engine needs the hypergraph_logic public headers and library
    target_include_directories(graphical_engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # Link to hypergraph_logic if available
    if(TARGET hypergraph_logic)
        target_link_libraries(graphical_engine PRIVATE hypergraph_logic)
    else()
        message(WARNING "graph_logic target not found. graphical_engine will build without linking to it.")
    endif()

    target_compile_features(graphical_engine PUBLIC cxx_std_20)
endif()