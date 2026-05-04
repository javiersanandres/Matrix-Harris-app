# ============================================================================
# app — application layer
#
# Contains three sub-libraries:
#   editors/        — HypergraphEditor, JointHypergraphEditor, Project
#   graphical_engine/ — HypergraphRenderer (layout → QGraphicsScene)
#   ui/             — Qt MainWindow and all Qt widgets (added later)
# ============================================================================

add_subdirectory(editors/src)
add_subdirectory(graphical_engine/src)

#if(BUILD_TESTING)
#    add_subdirectory(tests)
#endif()