# ============================================================================
# ui_lib — Qt UI layer including the hypergraph renderer
#
# HypergraphRenderer lives here (not in a separate graphical_engine library)
# because it depends on NodeItem and HyperedgeItem, which are Qt UI classes.
# Both overloads of render() are compiled into ui_lib:
#   - Static overload  : produces raw QGraphicsRectItem / QGraphicsPathItem.
#   - Factory overload : produces NodeItem / HyperedgeItem via caller lambdas.
# ============================================================================

find_package(Qt6 REQUIRED COMPONENTS Widgets LinguistTools)
list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.11.2/msvc2022_64")

add_library(ui_lib)

target_sources(ui_lib
    PRIVATE
        HypergraphRenderer.cpp
        NodeItem.cpp
        HyperedgeItem.cpp
        EquivalenceItem.cpp
        DiagramScene.cpp
        DiagramView.cpp
        DiagramTabWidget.cpp
        DiagramTabBar.cpp
        Dialogs.cpp
        NodeVisuals.cpp
        NodeEditorWidgets.cpp
        NodeDialogs.cpp
        ProjectSettingsDialog.cpp
        LayoutPreview.cpp
        SettingsWidgets.cpp
        BusySpinner.cpp
        MinimizingProgressDialog.cpp
        HelpButton.cpp
        HoverPopup.cpp
        UiStyle.cpp
        AppDialogs.cpp
        GurobiGuide.cpp
        GurobiNotice.cpp
        HelpNotifier.cpp
        Tutorial.cpp
        OptionsHelp.cpp
        PictureViewer.cpp
        PieceClipboard.cpp
        DiagramExport.cpp
        ViewOverlays.cpp
        MainWindow.cpp
        Translation.cpp
        LanguageButton.cpp
        NativeWindow.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../include
        FILES
            ../include/HypergraphRenderer.h
            ../include/NodeItem.h
            ../include/HyperedgeItem.h
            ../include/EquivalenceItem.h
            ../include/DiagramScene.h
            ../include/DiagramView.h
            ../include/DiagramTabWidget.h
            ../include/DiagramTabBar.h
            ../include/NodeDialogs.h
            ../include/ProjectSettingsDialog.h
            ../include/LayoutPreview.h
            ../include/SettingsWidgets.h
            ../include/NodeVisuals.h
            ../include/NodeEditorWidgets.h
            ../include/AddHypergraphDialog.h
            ../include/MinimizingProgressDialog.h
            ../include/BusySpinner.h
            ../include/HelpButton.h
            ../include/HoverPopup.h
            ../include/UiStyle.h
            ../include/AppDialogs.h
            ../include/GurobiGuide.h
            ../include/GurobiNotice.h
            ../include/HelpNotifier.h
            ../include/Tutorial.h
            ../include/OptionsHelp.h
            ../include/PictureViewer.h
            ../include/PieceClipboard.h
            ../include/DiagramExport.h
            ../include/ViewOverlays.h
            ../include/MainWindow.h
            ../include/Translation.h
            ../include/LanguageButton.h
            ../include/NativeWindow.h
)

target_include_directories(ui_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(ui_lib
    PUBLIC
        editors
        hypergraph_logic
        Qt6::Widgets
)
# NativeWindow.cpp turns off the system's window animations (DWM).
if(WIN32)
    target_link_libraries(ui_lib PRIVATE dwmapi)
endif()

set_target_properties(ui_lib PROPERTIES AUTOMOC ON)

# Pictures embedded in the executable:
#   :/gurobi/<instalar|licencia>/NN.png  screenshots of the Gurobi guides
#                                        (GurobiGuide.cpp; a missing one shows
#                                        a placeholder);
#   :/<es|en>/tutorial/<name>.png        pictures of the introductory tour
#                                        (Tutorial.cpp; a missing one is left out);
#   :/<es|en>/information/<topic>/NN.png pictures of the options' help
#                                        (OptionsHelp.cpp).
# The tour's and the help's pictures show the app, so there is a set per
# language; i18n::picture() falls back to the Spanish one.
set(UI_RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../resources")
file(GLOB UI_IMAGES CONFIGURE_DEPENDS
    "${UI_RESOURCES_DIR}/gurobi/*/*.png"
    "${UI_RESOURCES_DIR}/*/tutorial/*.png"
    "${UI_RESOURCES_DIR}/*/information/*/*.png"
    "${UI_RESOURCES_DIR}/app/*.ico")
if(UI_IMAGES)
    qt_add_resources(ui_lib "ui_images"
        PREFIX "/"
        BASE "${UI_RESOURCES_DIR}"
        FILES ${UI_IMAGES})
endif()

# ── Translations ──────────────────────────────────────────────────────────────
# The texts are written in Spanish (Tr::tr in the interface, LogicText::tr in
# the logic and the editors); app/translations/matrix_harris_en.ts holds their
# English. After adding or changing texts, update it with
#     cmake --build <build dir> --target update_translations
# and fill in the new English (tests check nothing is left untranslated). The
# build compiles it into :/i18n/matrix_harris_en.qm.
set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(LOGIC_DIR "${APP_DIR}/../hypergraph_logic")
file(GLOB TRANSLATED_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/*.h"
    "${APP_DIR}/editors/src/*.cpp"
    "${APP_DIR}/editors/include/*.h"
    "${LOGIC_DIR}/src/*.cpp"
    "${LOGIC_DIR}/include/*.h")
qt_add_translations(ui_lib
    TS_FILES "${APP_DIR}/translations/matrix_harris_en.ts"
    SOURCES ${TRANSLATED_SOURCES}
    RESOURCE_PREFIX "/i18n"
    NO_GENERATE_PLURALS_TS_FILE
    LUPDATE_OPTIONS -source-language es -target-language en -locations none -no-obsolete -no-ui-lines)
