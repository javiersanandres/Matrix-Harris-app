add_executable(minimizeCrossingsResults
    minimizeCrossingsResults.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossingsILP.cpp
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
        GS_TEST
)

# GlobalSiftingILP.cpp (compiled directly into this executable, same as
# minimizeCrossings.cpp above) calls into HiGHS. The highs::highs target
# already exists at this point in the build -- it's fetched once, near the
# top level, by the same FetchContent block the "hypergraph" library links
# against -- so this is just linking to it, not fetching it again.
target_link_libraries(minimizeCrossingsResults
    PRIVATE highs::highs
)