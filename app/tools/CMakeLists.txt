# ============================================================================
# Developer tools, left out of the normal build (build them by name).
#
#   capture_pictures   retakes the pictures of the options' help and the tour
#                      that show a box's menu or the box dialog:
#                        cmake --build <build dir> --target capture_pictures
#                        capture_pictures app/ui/resources
# ============================================================================

add_executable(capture_pictures EXCLUDE_FROM_ALL capture_pictures.cpp)
target_link_libraries(capture_pictures PRIVATE ui_lib)
set_target_properties(capture_pictures PROPERTIES AUTOMOC ON)
