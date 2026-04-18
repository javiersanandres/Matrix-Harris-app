# ============================================================================
# Hypergraph Logic Module
# ============================================================================

# Build the core libraries
add_subdirectory(src)

# Create the main interface library that aggregates all components
add_library(hypergraph_logic INTERFACE)

target_link_libraries(hypergraph_logic
    INTERFACE
        node
        hyperedge
        hypergraph
        graphical_hypergraph
)

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
