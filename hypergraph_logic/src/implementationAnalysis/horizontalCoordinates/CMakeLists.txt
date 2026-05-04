# Set CMAKE_PREFIX_PATH to help find Qt6
# Append to existing path rather than replacing it
list(APPEND CMAKE_PREFIX_PATH "C:/Qt2/6.11.0/msvc2022_64")
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets)

# ============================================================================
# JSON (nlohmann) - header-only, fetched from official release tarball
# ============================================================================
include(FetchContent)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Horizontal Coordinates Visualization library
add_executable(horizontalCoordinatesVisualization
    horizontalCoordinatesVisualization.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalCoordinates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hyperedge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hypergraph.cpp
)

target_include_directories(horizontalCoordinatesVisualization
    PRIVATE
        ${CMAKE_SOURCE_DIR}/hypergraph_logic/include
)

target_compile_definitions(horizontalCoordinatesVisualization PRIVATE BK_TEST GS_TEST)

# Add MSVC-specific compiler flag for __cplusplus macro
if(MSVC)
    target_compile_options(horizontalCoordinatesVisualization PRIVATE /Zc:__cplusplus)
endif()

set_target_properties(horizontalCoordinatesVisualization PROPERTIES
    AUTOMOC ON
)

target_link_libraries(horizontalCoordinatesVisualization
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        Qt6::Core
        Qt6::Widgets    
)

# Crossing Minimization + Horizontal Coordinates Visualization library
add_executable(minCrossHorizCoord
    minCrossHorizCoord.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalCoordinates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hyperedge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hypergraph.cpp
)

target_include_directories(minCrossHorizCoord
    PRIVATE
        ${CMAKE_SOURCE_DIR}/hypergraph_logic/include
)

target_compile_definitions(minCrossHorizCoord
    PRIVATE
        BK_TEST
        GS_TEST
        INSTANCES_DIR="${CMAKE_SOURCE_DIR}/hypergraph_logic/src/implementationAnalysis/minimizeCrossings/AllInstances"
)

if(MSVC)
    target_compile_options(minCrossHorizCoord PRIVATE /Zc:__cplusplus)
endif()

set_target_properties(minCrossHorizCoord PROPERTIES
    AUTOMOC ON
)

target_link_libraries(minCrossHorizCoord
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        Qt6::Core
        Qt6::Widgets
)


