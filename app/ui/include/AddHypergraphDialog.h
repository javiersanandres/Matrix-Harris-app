#pragma once

#include "AppDialogs.h"

#include <QListWidget>
#include <vector>
#include <string>

class QPushButton;

namespace ui {

// ============================================================================
// AddHypergraphDialog
//
// Modal dialog shown when the user clicks the background of the joint diagram.
// Lists every regular diagram in the project; those already incorporated into
// the joint are shown as such and cannot be picked. The user selects one and
// clicks "Añadir" (or double-clicks it) to incorporate it.
// ============================================================================
class AddHypergraphDialog : public StyledDialog {
    Q_OBJECT

public:
    struct DiagramEntry {
        std::string name;
        std::string id;
        bool        already_incorporated;
    };

    explicit AddHypergraphDialog(const std::vector<DiagramEntry>& entries,
                                 QWidget* parent = nullptr);

    // Returns the ID of the selected diagram, or empty string if cancelled
    // or no selectable item was chosen.
    std::string selectedId() const;

private:
    QListWidget* list_;
    QPushButton* add_button_ = nullptr;

    // Parallel to list_ rows — maps row index to diagram ID.
    std::vector<std::string> ids_;
};

} // namespace ui
