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
# The version is only written in the root CMakeLists.txt (project VERSION):
# the app reads it from here, so "Acerca de" and the installer always agree.
target_compile_definitions(TallerMatrixHarris PRIVATE APP_VERSION="${PROJECT_VERSION}")

# ── Updates (WinSparkle) ──────────────────────────────────────────────────────
# Prebuilt WinSparkle, downloaded once at configure time like the other
# dependencies. Only the executable uses it (see Updates.h): the UI library and
# the tests never load its DLL, which is copied next to the executable.
include(FetchContent)
FetchContent_Declare(
    winsparkle
    URL https://github.com/vslavik/winsparkle/releases/download/v0.9.4/WinSparkle-0.9.4.zip
    URL_HASH SHA256=6037df37fc263bd1650a1c4949681a9d40ffe991d01f35892a406cb5d103c976
)
FetchContent_MakeAvailable(winsparkle)
# The zip keeps its top folder (it also holds a __MACOSX one).
set(WINSPARKLE_DIR "${winsparkle_SOURCE_DIR}/WinSparkle-0.9.4")
if(NOT EXISTS "${WINSPARKLE_DIR}")
    set(WINSPARKLE_DIR "${winsparkle_SOURCE_DIR}")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WINSPARKLE_ARCH "x64")
else()
    set(WINSPARKLE_ARCH "Win32")
endif()
add_library(WinSparkle SHARED IMPORTED)
set_target_properties(WinSparkle PROPERTIES
    IMPORTED_LOCATION "${WINSPARKLE_DIR}/${WINSPARKLE_ARCH}/Release/WinSparkle.dll"
    IMPORTED_IMPLIB "${WINSPARKLE_DIR}/${WINSPARKLE_ARCH}/Release/WinSparkle.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${WINSPARKLE_DIR}/include"
)
target_sources(TallerMatrixHarris PRIVATE Updates.cpp Updates.h)
target_link_libraries(TallerMatrixHarris PRIVATE WinSparkle)
add_custom_command(TARGET TallerMatrixHarris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:WinSparkle>" "$<TARGET_FILE_DIR:TallerMatrixHarris>"
    COMMENT "Copying WinSparkle.dll next to the executable"
)

# The executable's icon (Explorer, taskbar): ui/resources/app/app.ico, the
# same file the windows take theirs from (":/app/app.ico").
if(WIN32)
    target_sources(TallerMatrixHarris PRIVATE app.rc)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()