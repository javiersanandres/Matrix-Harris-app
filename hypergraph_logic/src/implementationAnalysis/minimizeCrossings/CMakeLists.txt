add_executable(minimizeCrossingsResults
    minimizeCrossingsResults.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hyperedge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hypergraph.cpp
)

target_include_directories(minimizeCrossingsResults
    PRIVATE
        ${CMAKE_SOURCE_DIR}/hypergraph_logic/include
)

target_compile_definitions(minimizeCrossingsResults
    PRIVATE
        INSTANCES_DIR="${CMAKE_SOURCE_DIR}/hypergraph_logic/src/implementationAnalysis/minimizeCrossings/AllInstances"
)