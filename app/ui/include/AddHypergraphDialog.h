#pragma once

#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <vector>
#include <string>

namespace ui {

// ============================================================================
// AddHypergraphDialog
//
// Modal dialog shown when the user clicks the background of the joint diagram.
// Displays all regular diagrams in the project as a list. Diagrams already
// incorporated into the joint are shown greyed out and non-selectable.
// The user selects one and clicks Aceptar to incorporate it.
// ============================================================================
class AddHypergraphDialog : public QDialog {
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

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    QListWidget*      list_;
    QDialogButtonBox* buttons_;

    // Parallel to list_ rows — maps row index to diagram ID.
    std::vector<std::string> ids_;
};

} // namespace ui
