#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QDialogButtonBox>

namespace ui {

// ============================================================================
// FuseNodesDialog
//
// Simple modal dialog shown after the user selects the second node in a
// fuseNodes operation. Presents a QLineEdit pre-filled with the first node's
// current name so the user can confirm or change it.
// ============================================================================
class FuseNodesDialog : public QDialog {
    Q_OBJECT

public:
    explicit FuseNodesDialog(const QString& default_name,
                             QWidget* parent = nullptr);

    // Returns the name the user typed, or an empty string if cancelled.
    QString chosenName() const;

private:
    QLineEdit*        name_edit_;
    QDialogButtonBox* buttons_;
};

} // namespace ui
