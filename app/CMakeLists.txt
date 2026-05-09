# ============================================================================
# app — application layer
#
# Contains three sub-libraries:
#   editors/        — HypergraphEditor, JointHypergraphEditor, Project
#   graphical_engine/ — HypergraphRenderer (layout -> QGraphicsScene)
#   ui/             — Qt MainWindow and all Qt widgets (added later)
# ============================================================================

add_subdirectory(editors/src)
add_subdirectory(ui/src)


# ── Main executable ───────────────────────────────────────────────────────────
add_executable(MatrixHarrisApp main.cpp)
target_link_libraries(MatrixHarrisApp PRIVATE ui_lib)
set_target_properties(MatrixHarrisApp PROPERTIES
    WIN32_EXECUTABLE TRUE
    AUTOMOC ON
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()