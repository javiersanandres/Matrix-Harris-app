find_package(Gurobi QUIET)

if(NOT Gurobi_FOUND)
    set(GUROBI_HOME "C:/gurobi1301/win64" CACHE PATH "Gurobi installation directory")

    if(EXISTS "${GUROBI_HOME}")
        set(Gurobi_INCLUDE_DIRS "${GUROBI_HOME}/include")

        set(Gurobi_CPP_LIB_DEBUG   "${GUROBI_HOME}/lib/gurobi_c++mdd2017.lib")
        set(Gurobi_CPP_LIB_RELEASE "${GUROBI_HOME}/lib/gurobi_c++md2017.lib")
        set(Gurobi_C_LIB           "${GUROBI_HOME}/lib/gurobi130.lib")

        set(Gurobi_LIBRARIES
            $<IF:$<CONFIG:Debug>,${Gurobi_CPP_LIB_DEBUG},${Gurobi_CPP_LIB_RELEASE}>
            ${Gurobi_C_LIB}
        )

        set(Gurobi_FOUND TRUE)
        message(STATUS "Gurobi found at: ${GUROBI_HOME}")
    else()
        message(FATAL_ERROR
            "Gurobi not found at ${GUROBI_HOME}. "
            "Set -DGUROBI_HOME=<path> to point to your Gurobi installation.")
    endif()
else()
    message(STATUS "Gurobi found via find_package")
endif()

# ============================================================================
# JSON (nlohmann) - header-only, fetched from official release tarball
# ============================================================================
include(FetchContent)

FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

add_executable(horizontalOrderResults
    horizontalOrderResults.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalOrder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../horizontalCoordinates.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../minimizeCrossings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hyperedge.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Hypergraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraph.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../GraphicalHypergraphPersistence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../portAssignment.cpp
)

target_include_directories(horizontalOrderResults
    PRIVATE
        ${CMAKE_SOURCE_DIR}/hypergraph_logic/include
        ${Gurobi_INCLUDE_DIRS}

)

target_link_libraries(horizontalOrderResults
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        ${Gurobi_LIBRARIES}
)


if(MSVC)
    target_compile_options(horizontalOrderResults PRIVATE
        $<IF:$<CONFIG:Debug>,/MDd,/MD>
    )
else()
    target_compile_options(horizontalOrderResults PRIVATE -O2)
endif()