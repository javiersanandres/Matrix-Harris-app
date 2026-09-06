# ============================================================================
# benchmarkAnalysis — CMakeLists.txt
# ============================================================================

include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

add_executable(fullPipelineResults
    fullPipelineResults.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalCoordinates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../portAssignment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hyperedge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hypergraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraphPersistence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../JointGraphicalHypergraph.cpp
    $<IF:$<BOOL:${Gurobi_FOUND}>,${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalOrder.cpp,>
)

target_include_directories(fullPipelineResults
    PRIVATE
        ${CMAKE_SOURCE_DIR}/hypergraph_logic/include
        $<IF:$<BOOL:${Gurobi_FOUND}>,${Gurobi_INCLUDE_DIRS},>
)

target_link_libraries(fullPipelineResults
    PRIVATE
        nlohmann_json::nlohmann_json
        $<IF:$<BOOL:${Gurobi_FOUND}>,${Gurobi_LIBRARIES},>
)

# Default benchmark directory: <repo_root>/benchmark/
# Override with -DBENCHMARK_DIR=<path> if needed.
if(NOT DEFINED BENCHMARK_DIR)
    set(BENCHMARK_DIR "${CMAKE_SOURCE_DIR}/hypergraph_logic/src/implementationAnalysis/benchmarkAnalysis/benchmark")
endif()

target_compile_definitions(fullPipelineResults
    PRIVATE
        BENCHMARK_DIR="${BENCHMARK_DIR}"
)

if(MSVC)
    target_compile_options(fullPipelineResults PRIVATE
        $<IF:$<CONFIG:Debug>,/MDd,/MD>
    )
else()
    target_compile_options(fullPipelineResults PRIVATE -O2)
endif()