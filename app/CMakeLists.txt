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
add_executable(TallerMatrixHarris main.cpp)
target_link_libraries(TallerMatrixHarris PRIVATE ui_lib)
set_target_properties(TallerMatrixHarris PROPERTIES
    WIN32_EXECUTABLE TRUE
    AUTOMOC ON
)
# The executable's icon (Explorer, taskbar): ui/resources/app/app.ico, the
# same file the windows take theirs from (":/app/app.ico").
if(WIN32)
    target_sources(TallerMatrixHarris PRIVATE app.rc)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()