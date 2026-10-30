#pragma once

#include "Project.h"
#include "ProjectSettings.h"

#include <QColor>
#include <QDialog>
#include <QList>

#include <optional>

class QListWidget;
class QSettings;
class QSpinBox;

namespace ui {

class ColorPickerButton;
class FontSizeSelector;
class NodePreview;
class StyledDialog;
class TickBox;

// ============================================================================
// app_defaults
//
// The settings new projects start with, kept by the application (in QSettings,
// so in the registry on Windows) rather than in any project. Without any saved,
// the built-in ones (ProjectSettings()).
// ============================================================================
namespace app_defaults {
    app_logic::ProjectSettings load(QSettings& settings);
    void save(QSettings& settings, const app_logic::ProjectSettings& defaults);

    // The same, with the application's QSettings.
    app_logic::ProjectSettings load();
    void save(const app_logic::ProjectSettings& defaults);
}

// ============================================================================
// ProjectSettingsDialog
//
// "Ajustes del proyecto": everything a project decides for all its diagrams
// (see ProjectSettings), laid out like the node dialogs:
//
//   - Cajas nuevas         : shape, background and text colour, font size, with
//                            a preview of a new box. Only for boxes created
//                            from now on.
//   - Equivalencias nuevas : continuous or dashed line.
//   - Distancias           : between neighbouring boxes, between the boxes and
//                            their connections, between stacked horizontal lines.
//   - Tamaño por defecto   : width and height of each shape's boxes.
//
// Under them, a tick box to also make these the application's defaults for new
// projects, and buttons to fill the form with the application's defaults or the
// built-in ones. Nothing is applied until "Aceptar"; the caller applies it
// (Project::setSettings), which cannot be undone, as the footer says.
// ============================================================================
class ProjectSettingsDialog : public QDialog {
    Q_OBJECT

public:
    ProjectSettingsDialog(const app_logic::ProjectSettings& current,
                          const app_logic::ProjectSettings& app_defaults,
                          const QList<QColor>& recent_colours,
                          QWidget* parent = nullptr);

    // The settings as they are in the form.
    app_logic::ProjectSettings settings() const { return settings_; }

    // Whether they should also become the application's defaults for new projects.
    bool saveAsAppDefaults() const;

    // Fills the form with these settings.
    void setSettings(const app_logic::ProjectSettings& settings);

private:
    void sync();

    app_logic::ProjectSettings settings_;
    app_logic::ProjectSettings app_defaults_;
    bool font_auto_ = true; // the text colour follows the background (as "Automático" does)
    bool syncing_ = false;

    QListWidget* shape_ = nullptr;
    ColorPickerButton* colour_ = nullptr;
    ColorPickerButton* font_colour_ = nullptr;
    FontSizeSelector* font_size_ = nullptr;
    NodePreview* preview_ = nullptr;
    TickBox* continuous_ = nullptr;
    TickBox* dashed_ = nullptr;
    QSpinBox* block_sep_ = nullptr;
    QSpinBox* layer_gap_ = nullptr;
    QSpinBox* horizontal_sep_ = nullptr;
    struct SizeFields { QSpinBox* width = nullptr; QSpinBox* height = nullptr; };
    SizeFields rectangle_, circle_, rhombus_;
    TickBox* as_app_defaults_ = nullptr;
};

// "¿Y las cajas que ya existen?": asked when new default sizes would change
// boxes that use them. Returns the answer, or nothing if cancelled. The dialog
// itself (not yet shown) is what existingBoxesQuestion builds.
std::optional<app_logic::Project::ExistingBoxes> askAboutExistingBoxes(QWidget* parent);
StyledDialog* existingBoxesQuestion(QWidget* parent);

} // namespace ui
