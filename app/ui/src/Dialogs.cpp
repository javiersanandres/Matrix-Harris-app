#include "AddHypergraphDialog.h"
#include "UiStyle.h"

#include <QListWidgetItem>
#include <QPushButton>

#include <algorithm>

namespace ui {

// ============================================================================
// AddHypergraphDialog
// ============================================================================

namespace {
    constexpr int ADD = 1;
    constexpr int CANCEL = 0;
}

AddHypergraphDialog::AddHypergraphDialog(const std::vector<DiagramEntry>& entries,
                                         QWidget* parent)
    : StyledDialog(Badge::Joint, QStringLiteral("Añadir al esquema conjunto"), parent)
{
    setMessage(QStringLiteral("Elige el esquema que quieres incorporar."));

    list_ = new QListWidget(this);
    list_->setObjectName("diagramList");
    list_->setStyleSheet(QStringLiteral(R"(
QListWidget#diagramList { border: none; background: transparent; outline: none; }
QListWidget#diagramList::item {
    border: 1px solid #E3E6EF; border-radius: 10px; margin: 3px 0; padding: 8px 10px;
    color: #1F2330; background: white;
}
QListWidget#diagramList::item:hover { border-color: #A5A8F5; background: #FAFAFF; }
QListWidget#diagramList::item:selected { border: 2px solid #6366F1; background: #EEF0FF; color: #1F2330; }
QListWidget#diagramList::item:disabled { color: #A0A5B5; background: #F7F8FA; }
)"));
    list_->setIconSize(QSize(20, 20));
    const QIcon diagram_icon = style::icon(style::Icon::Joint);
    for (const auto& entry : entries) {
        QString text = QString::fromStdString(entry.name);
        if (entry.already_incorporated) text += QStringLiteral("  ·  ya incorporado");
        auto* item = new QListWidgetItem(diagram_icon, text);
        if (entry.already_incorporated)
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        list_->addItem(item);
        ids_.push_back(entry.id);
    }
    // Tall enough for every row (up to six; more scroll), measured with the
    // styled rows' real height.
    list_->ensurePolished();
    int height = 6;
    for (int row = 0; row < std::min(list_->count(), 6); ++row)
        height += list_->sizeHintForRow(row);
    list_->setFixedHeight(height);
    list_->setVerticalScrollBarPolicy(list_->count() > 6 ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    list_->setMinimumWidth(320);
    setBody(list_);

    addButton(QStringLiteral("Cancelar"), CANCEL, ButtonStyle::Secondary, false, true);
    add_button_ = addButton(QStringLiteral("Añadir"), ADD, ButtonStyle::Primary, true);

    // "Añadir" only makes sense once something that can be added is selected.
    auto refresh = [this] {
        const auto* item = list_->currentItem();
        add_button_->setEnabled(item && (item->flags() & Qt::ItemIsEnabled) && item->isSelected());
    };
    connect(list_, &QListWidget::itemSelectionChanged, this, refresh);
    refresh();

    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (item->flags() & Qt::ItemIsEnabled) add_button_->click();
    });
}

std::string AddHypergraphDialog::selectedId() const {
    if (choice() != ADD) return {};
    int row = list_->currentRow();
    if (row < 0 || row >= static_cast<int>(ids_.size())) return {};
    // Ensure the item is enabled (not already incorporated).
    if (!(list_->item(row)->flags() & Qt::ItemIsEnabled)) return {};
    return ids_[row];
}

} // namespace ui
