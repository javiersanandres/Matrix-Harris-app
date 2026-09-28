# ============================================================================
# benchmarkAnalysis — CMakeLists.txt
#
# Two benchmark executables share the same hypergraph sources, renderer and
# dependencies:
#
#   fullPipelineResults        — the CSV benchmark in ./benchmark
#   northDagsPipelineResults   — the public North DAGs suite (GraphML)
#
# Both run the console benchmark first and then open the Qt viewer showing
# the MH and MH+GS layouts of every instance (HypergraphRenderer).
# ============================================================================

include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

# ── Qt Widgets ────────────────────────────────────────────────────────────
# Needed by the post-run viewer (HypergraphRenderer is Qt Widgets code).
# Prefer Qt6, fall back to Qt5 — if your project already pins one version
# elsewhere, reuse that find_package call and target name instead.
find_package(Qt6 QUIET COMPONENTS Widgets)
if (Qt6_FOUND)
    set(BENCHMARK_QT_WIDGETS_TARGET Qt6::Widgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(BENCHMARK_QT_WIDGETS_TARGET Qt5::Widgets)
endif()

# ── app/ui location ──────────────────────────────────────────────────────
# ASSUMPTION: app/ui sits at <repo_root>/app/ui, with its headers (including
# HypergraphRenderer.h, NodeItem.h, HyperedgeItem.h) under app/ui/include.
# If your real layout differs, only these two lines need to change.
set(UI_DIR "${CMAKE_SOURCE_DIR}/app/ui")
set(UI_INCLUDE_DIR "${UI_DIR}/include")

# ── Sources shared by every benchmark executable ─────────────────────────
set(HYPERGRAPH_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

set(BENCHMARK_COMMON_SOURCES
    ${HYPERGRAPH_SRC_DIR}/minimizeCrossings.cpp
    ${HYPERGRAPH_SRC_DIR}/minimizeCrossingsILP.cpp
    ${HYPERGRAPH_SRC_DIR}/horizontalCoordinates.cpp
    ${HYPERGRAPH_SRC_DIR}/portAssignment.cpp
    ${HYPERGRAPH_SRC_DIR}/Node.cpp
    ${HYPERGRAPH_SRC_DIR}/Hyperedge.cpp
    ${HYPERGRAPH_SRC_DIR}/Hypergraph.cpp
    ${HYPERGRAPH_SRC_DIR}/GraphicalHypergraph.cpp
    ${HYPERGRAPH_SRC_DIR}/GraphicalHypergraphPersistence.cpp
    ${HYPERGRAPH_SRC_DIR}/JointGraphicalHypergraph.cpp
    ${UI_DIR}/src/HypergraphRenderer.cpp
    $<IF:$<BOOL:${Gurobi_FOUND}>,${HYPERGRAPH_SRC_DIR}/horizontalOrder.cpp,>
)

# ── add_benchmark_executable(<name> <main .cpp>) ─────────────────────────
# Creates one benchmark executable with the shared sources, include paths,
# libraries and compile options. Dataset locations are added per target
# below with target_compile_definitions.
#
# No AUTOMOC/AUTORCC: the viewer classes don't declare Q_OBJECT and only use
# lambda-based QObject::connect. If you add a Q_OBJECT class, set
#   set_target_properties(<name> PROPERTIES AUTOMOC ON)
function(add_benchmark_executable name main_source)
    add_executable(${name}
        ${main_source}
        ${BENCHMARK_COMMON_SOURCES}
    )

    target_include_directories(${name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/hypergraph_logic/include
            ${UI_INCLUDE_DIR}
            $<IF:$<BOOL:${Gurobi_FOUND}>,${Gurobi_INCLUDE_DIRS},>
    )

    target_link_libraries(${name}
        PRIVATE
            nlohmann_json::nlohmann_json
            ${BENCHMARK_QT_WIDGETS_TARGET}
            highs::highs
            $<IF:$<BOOL:${Gurobi_FOUND}>,${Gurobi_LIBRARIES},>
    )

    if(MSVC)
        target_compile_options(${name} PRIVATE
            $<IF:$<CONFIG:Debug>,/MDd,/MD>
        )
    else()
        target_compile_options(${name} PRIVATE -O2)
    endif()
endfunction()

# ============================================================================
# fullPipelineResults — CSV benchmark
# ============================================================================

add_benchmark_executable(fullPipelineResults fullPipelineResults.cpp)

# Default: <this folder>/benchmark. Override with -DBENCHMARK_DIR=<path>.
if(NOT DEFINED BENCHMARK_DIR)
    set(BENCHMARK_DIR "${CMAKE_SOURCE_DIR}/hypergraph_logic/src/implementationAnalysis/benchmarkAnalysis/benchmark")
endif()

target_compile_definitions(fullPipelineResults
    PRIVATE
        GUROBI_AVAILABLE
        BENCHMARK_DIR="${BENCHMARK_DIR}"
)

# ============================================================================
# northDagsPipelineResults — North DAGs (GraphML, graphdrawing.org)
# ============================================================================

add_benchmark_executable(northDagsPipelineResults northDagsPipelineResults.cpp)

# Default: <this folder>/north — extract the North DAGs GraphML archive there.
# Override with -DNORTH_BENCHMARK_DIR=<path>, or pass the folder as argv[1].
if(NOT DEFINED NORTH_BENCHMARK_DIR)
    set(NORTH_BENCHMARK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/north")
endif()

target_compile_definitions(northDagsPipelineResults
    PRIVATE
        GUROBI_AVAILABLE
        NORTH_BENCHMARK_DIR="${NORTH_BENCHMARK_DIR}"
)

if(NOT EXISTS "${NORTH_BENCHMARK_DIR}")
    message(STATUS
        "northDagsPipelineResults: dataset folder not found at ${NORTH_BENCHMARK_DIR}. "
        "Download the North DAGs (GraphML) from http://www.graphdrawing.org/data.html "
        "and extract them there, or pass the folder as the first program argument.")
endif()