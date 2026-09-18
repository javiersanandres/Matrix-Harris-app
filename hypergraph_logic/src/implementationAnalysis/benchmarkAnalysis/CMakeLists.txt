# ============================================================================
# benchmarkAnalysis — CMakeLists.txt
# ============================================================================

include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

# ── Qt Widgets ────────────────────────────────────────────────────────────
# fullPipelineResults now opens a viewer window after the console benchmark
# finishes, built on HypergraphRenderer (which is Qt Widgets code). Prefer
# Qt6, fall back to Qt5 — adjust/trim this if your project already pins one
# version elsewhere (in which case just reuse that find_package call and
# target name instead of duplicating it here).
find_package(Qt6 QUIET COMPONENTS Widgets)
if (Qt6_FOUND)
    set(BENCHMARK_QT_WIDGETS_TARGET Qt6::Widgets)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets)
    set(BENCHMARK_QT_WIDGETS_TARGET Qt5::Widgets)
endif()

# ── app/ui location ──────────────────────────────────────────────────────
# ASSUMPTION: app/ui sits at <repo_root>/app/ui, with its headers (including
# HypergraphRenderer.h, NodeItem.h, HyperedgeItem.h) under app/ui/include —
# mirroring how hypergraph_logic/include already works below. If your real
# layout differs, only these two lines need to change.
set(UI_DIR "${CMAKE_SOURCE_DIR}/app/ui")
set(UI_INCLUDE_DIR "${UI_DIR}/include")

add_executable(fullPipelineResults
    fullPipelineResults.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossingsILP.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalCoordinates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../portAssignment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hyperedge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hypergraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraphPersistence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../JointGraphicalHypergraph.cpp
    ${UI_DIR}/src/HypergraphRenderer.cpp
    $<IF:$<BOOL:${Gurobi_FOUND}>,${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalOrder.cpp,>
)

target_include_directories(fullPipelineResults
    PRIVATE
        ${CMAKE_SOURCE_DIR}/hypergraph_logic/include
        ${UI_INCLUDE_DIR}
        $<IF:$<BOOL:${Gurobi_FOUND}>,${Gurobi_INCLUDE_DIRS},>
)

target_link_libraries(fullPipelineResults
    PRIVATE
        nlohmann_json::nlohmann_json
        ${BENCHMARK_QT_WIDGETS_TARGET}
        $<IF:$<BOOL:${Gurobi_FOUND}>,${Gurobi_LIBRARIES},>
        PRIVATE highs::highs
)

# Note: no AUTOMOC/AUTORCC needed for this target. The new viewer classes
# (ZoomableGraphicsView, HypergraphViewerWindow) don't declare Q_OBJECT or
# use signals/slots macros — only lambda-based QObject::connect — so moc
# generation isn't required. If you later add a Q_OBJECT class here, set:
#   set_target_properties(fullPipelineResults PROPERTIES AUTOMOC ON)

# Default benchmark directory: <repo_root>/benchmark/
# Override with -DBENCHMARK_DIR=<path> if needed.
if(NOT DEFINED BENCHMARK_DIR)
    set(BENCHMARK_DIR "${CMAKE_SOURCE_DIR}/hypergraph_logic/src/implementationAnalysis/benchmarkAnalysis/benchmark")
endif()

target_compile_definitions(fullPipelineResults
    PRIVATE
        GUROBI_AVAILABLE
        BENCHMARK_DIR="${BENCHMARK_DIR}"
)

if(MSVC)
    target_compile_options(fullPipelineResults PRIVATE
        $<IF:$<CONFIG:Debug>,/MDd,/MD>
    )
else()
    target_compile_options(fullPipelineResults PRIVATE -O2)
endif()