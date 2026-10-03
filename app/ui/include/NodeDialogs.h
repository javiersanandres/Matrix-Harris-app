#pragma once

#include "Node.h"

#include <QColor>
#include <QDialog>
#include <QList>

class QPushButton;

namespace ui {

class NodeAttributesForm;
class NodePreview;

// ============================================================================
// NodeDialog
//
// Modal form to introduce every attribute of a node, with a live preview of
// the node at the bottom. Used in two modes:
//   - Create : after choosing any node-creation action ("Crear" / "Cancelar").
//   - Edit   : "Propiedades" of an existing node, pre-filled with its current
//              attributes ("Actualizar" / "Cancelar").
// ============================================================================
class NodeDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode { Create, Edit };

    NodeDialog(Mode mode,
               const hypergraph_logic::NodeAttributes& initial,
               const QList<QColor>& recent_colours,
               QWidget* parent = nullptr);

    // The attributes as they are in the form (name trimmed).
    hypergraph_logic::NodeAttributes attributes() const;

    // Colours the user explicitly picked while the dialog was open.
    QList<QColor> pickedColours() const;

    // Edit mode only: whether every box of the diagram should take the font size
    // too ("Aplicar a todas las cajas del esquema").
    bool fontSizeForAll() const;

private:
    NodeAttributesForm* form_ = nullptr;
    NodePreview*        preview_ = nullptr;
    QPushButton*        accept_ = nullptr;
};

// ============================================================================
// FuseNodesDialog
//
// Shown after the second node of a fusion is chosen. Presents both nodes side
// by side as a three-way merge: for every attribute the user can take the
// left node's value, the right node's value, or edit the result by hand (or
// take everything from one side at once). A preview of the fused node is
// shown at the bottom ("Fusionar" / "Cancelar").
// ============================================================================
class FuseNodesDialog : public QDialog {
    Q_OBJECT

public:
    FuseNodesDialog(const hypergraph_logic::NodeAttributes& first,
                    const hypergraph_logic::NodeAttributes& second,
                    const QList<QColor>& recent_colours,
                    QWidget* parent = nullptr);

    hypergraph_logic::NodeAttributes attributes() const;
    QList<QColor> pickedColours() const;

private:
    NodeAttributesForm* form_ = nullptr;
    NodePreview*        preview_ = nullptr;
    QPushButton*        accept_ = nullptr;
};

} // namespace ui
