#include "FuseNodesDialog.h"
#include "AddHypergraphDialog.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QListWidgetItem>

namespace ui {

// ============================================================================
// FuseNodesDialog
// ============================================================================

FuseNodesDialog::FuseNodesDialog(const QString& default_name, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Fusionar nodos");
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Nombre del nodo fusionado:"));

    name_edit_ = new QLineEdit(default_name, this);
    name_edit_->selectAll();
    layout->addWidget(name_edit_);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(name_edit_, &QLineEdit::returnPressed, this, &QDialog::accept);
}

QString FuseNodesDialog::chosenName() const {
    return (result() == QDialog::Accepted) ? name_edit_->text().trimmed()
                                           : QString();
}

// ============================================================================
// AddHypergraphDialog
// ============================================================================

AddHypergraphDialog::AddHypergraphDialog(const std::vector<DiagramEntry>& entries,
                                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Añadir diagrama al diagrama conjunto");
    setModal(true);
    setMinimumWidth(300);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Selecciona un diagrama:"));

    list_ = new QListWidget(this);
    for (const auto& entry : entries) {
        auto* item = new QListWidgetItem(QString::fromStdString(entry.name));
        if (entry.already_incorporated) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
            item->setForeground(Qt::gray);
        }
        list_->addItem(item);
        ids_.push_back(entry.id);
    }
    layout->addWidget(list_);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list_, &QListWidget::itemDoubleClicked,
            this, &AddHypergraphDialog::onItemDoubleClicked);
}

std::string AddHypergraphDialog::selectedId() const {
    if (result() != QDialog::Accepted) return {};
    int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(ids_.size())) return {};
    // Ensure the item is enabled (not already incorporated).
    if (!(list_->item(row)->flags() & Qt::ItemIsEnabled)) return {};
    return ids_[row];
}

void AddHypergraphDialog::onItemDoubleClicked(QListWidgetItem* item) {
    if (item->flags() & Qt::ItemIsEnabled)
        accept();
}

} // namespace ui
