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

# Gurobi, so minimizeCrossingsILP.cpp's Gurobi backend (and this program's
# ILP-Gurobi benchmark column, via ILPBackendTestHook.h) can actually be
# compiled and exercised here, not just in the main "hypergraph" library.
# Gurobi_FOUND / Gurobi_INCLUDE_DIRS / Gurobi_LIBRARIES are inherited from the
# parent hypergraph_logic/src/CMakeLists.txt scope (this directory is reached
# via its add_subdirectory(), which runs after that file's Gurobi detection),
# so nothing needs to be re-detected here. Same runtime caveat as there:
# GUROBI_AVAILABLE only means the SDK was found at configure time -- whether a
# license is actually usable is still checked at runtime, in
# minimizeCrossingsILP.cpp.
if(Gurobi_FOUND)
    target_compile_definitions(minimizeCrossingsResults PRIVATE GUROBI_AVAILABLE)
    target_include_directories(minimizeCrossingsResults PRIVATE ${Gurobi_INCLUDE_DIRS})
    target_link_libraries(minimizeCrossingsResults PRIVATE ${Gurobi_LIBRARIES})
else()
    message(STATUS "minimizeCrossingsResults: Gurobi not found -- ILP-Gurobi column will report N/A")
endif()