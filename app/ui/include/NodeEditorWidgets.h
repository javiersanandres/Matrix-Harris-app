#pragma once

#include "Node.h"

#include <QAbstractButton>
#include <QColor>
#include <QFrame>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include <array>
#include <optional>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;

namespace ui {

    // ============================================================================
    // Building blocks shared by the node dialogs (NodeDialog, FuseNodesDialog).
    // ============================================================================

    namespace dialog_theme {
        // Style sheet applied to every node dialog (cards, inputs, buttons, ...).
        QString styleSheet();

        // Re-applies the style sheet after a dynamic property used in a selector
        // (e.g. [active="true"]) changed on w.
        void repolish(QWidget* w);
    }

    // Renders a node with the given attributes into a pixmap of the given
    // logical size, scaled down (never up) to fit, with no label scrolling.
    QPixmap renderNodeIcon(const hypergraph_logic::NodeAttributes& attributes,
        const QSize& size, qreal device_pixel_ratio);

    // ============================================================================
    // DialogBanner
    //
    // Gradient header at the top of a node dialog: a glyph, a title and a
    // subtitle, painted in white over an indigo-violet gradient.
    //
    // The node dialogs have no window title bar, so the banner stands in for it:
    // dragging it moves the dialog, and its "×" button (top right) emits
    // closeRequested().
    // ============================================================================
    class DialogBanner : public QWidget {
        Q_OBJECT
    public:
        enum class Glyph { Create, Edit, Fuse };

        DialogBanner(Glyph glyph, const QString& title, const QString& subtitle,
            QWidget* parent = nullptr);

    signals:
        void closeRequested();

    protected:
        void paintEvent(QPaintEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;

    private:
        Glyph glyph_;
        QString title_;
        QString subtitle_;
        QToolButton* close_;
        bool dragging_ = false;
        QPoint drag_offset_;
    };

    // ============================================================================
    // TickBox
    //
    // Checkable button drawn entirely by hand (box, tick, optional icon, text),
    // so its border is uniform on every side regardless of the platform style.
    // ============================================================================
    class TickBox : public QAbstractButton {
        Q_OBJECT
    public:
        explicit TickBox(const QString& text, const QIcon& icon = {}, QWidget* parent = nullptr);

        QSize sizeHint() const override;

    protected:
        void paintEvent(QPaintEvent* event) override;
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;
        bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }

    private:
        static constexpr int BOX = 18;
        static constexpr int GAP = 8;
        bool hovered_ = false;
    };

    // ============================================================================
    // ColorPickerButton
    //
    // Word-style colour button: shows the current colour and opens a popup with
    // the theme colours (with tints and shades), the standard colours, the
    // colours recently used in the project and "Más colores…", which opens a
    // full spectrum/HSV picker. Optionally offers an "Automático" entry whose
    // colour is decided by the caller.
    // ============================================================================
    class ColorPickerButton : public QToolButton {
        Q_OBJECT
    public:
        explicit ColorPickerButton(QWidget* parent = nullptr);

        void setRecentColours(const QList<QColor>& recent) { recent_ = recent; }

        // Enables the "Automático" entry at the top of the popup.
        void setAutomaticAvailable(bool available) { automatic_available_ = available; }

        // Updates the displayed colour. automatic=true shows it as "Automático".
        void setColour(const QColor& colour, bool automatic = false);
        QColor colour() const { return colour_; }

    signals:
        // The user explicitly chose a colour.
        void colourPicked(const QColor& colour);
        // The user chose the "Automático" entry.
        void automaticPicked();

    private:
        void rebuildMenu();
        void openMoreColours();
        void refreshFace();

        QMenu* menu_ = nullptr;
        QColor colour_ = Qt::white;
        bool automatic_ = false;
        bool automatic_available_ = false;
        QList<QColor> recent_;
    };

    // ============================================================================
    // FontSizeSelector
    //
    // Word-style font size control: an editable combo with the usual sizes plus
    // "grow" and "shrink" buttons that jump to the next/previous preset.
    // ============================================================================
    class FontSizeSelector : public QWidget {
        Q_OBJECT
    public:
        explicit FontSizeSelector(QWidget* parent = nullptr);

        int value() const { return value_; }
        void setValue(int size);

        static constexpr int MIN_SIZE = 1;
        static constexpr int MAX_SIZE = 400;

    signals:
        void valueChanged(int size);

    private:
        void commitText();
        void step(bool grow);

        QComboBox* combo_ = nullptr;
        QToolButton* grow_ = nullptr;
        QToolButton* shrink_ = nullptr;
        int value_ = hypergraph_logic::NodeAttributes::DEFAULT_FONT_SIZE;
    };

    // ============================================================================
    // NodePreview
    //
    // Draws a node exactly as it will look on the canvas (same painter), centred
    // on a dotted "canvas" card.
    // ============================================================================
    class NodePreview : public QWidget {
        Q_OBJECT
    public:
        explicit NodePreview(QWidget* parent = nullptr);

        void setAttributes(const hypergraph_logic::NodeAttributes& attributes);
        QSize sizeHint() const override { return QSize(320, 150); }

    protected:
        void paintEvent(QPaintEvent* event) override;

    private:
        hypergraph_logic::NodeAttributes attributes_;
    };

    // ============================================================================
    // NodeAttributesForm
    //
    // Every editable attribute of a node:
    //   - Nombre             : typed.
    //   - Forma              : picked from a list (rectangle, circle, rhombus).
    //   - Color de fondo     : ColorPickerButton.
    //   - Color del texto    : ColorPickerButton with "Automático", which follows
    //                          the background (black or white, whichever reads
    //                          better) until the user picks a colour explicitly.
    //   - Tamaño de fuente   : FontSizeSelector, optionally with a tick box to
    //                          give that size to every box of the diagram.
    //   - Fuego / Fuego con cenizas : two mutually exclusive tick boxes. Either
    //                          one forces a white background and black text (and
    //                          locks both pickers); unticking restores the colours
    //                          in use before.
    //
    // Merge mode (when both `left` and `right` are given) lays the same editors
    // out as a three-way merge: each row shows the left node's value, a button
    // to take it, the editable result, a button to take the right node's value,
    // and the right node's value. Rows where the two nodes differ are flagged.
    // ============================================================================
    class NodeAttributesForm : public QWidget {
        Q_OBJECT
    public:
        NodeAttributesForm(const hypergraph_logic::NodeAttributes& initial,
            const QList<QColor>& recent_colours,
            QWidget* parent = nullptr);

        // Merge-mode constructor.
        NodeAttributesForm(const hypergraph_logic::NodeAttributes& initial,
            const QList<QColor>& recent_colours,
            const hypergraph_logic::NodeAttributes& left, const QString& left_title,
            const hypergraph_logic::NodeAttributes& right, const QString& right_title,
            QWidget* parent = nullptr);

        hypergraph_logic::NodeAttributes attributes() const;
        void setAttributes(const hypergraph_logic::NodeAttributes& attributes);

        // Colours the user explicitly picked while the form was open, so the
        // project can offer them again next time.
        const QList<QColor>& pickedColours() const { return picked_colours_; }

        void focusName();

        // Shows, under the font size, a tick box to give that size to every box of
        // the diagram (hidden by default), and whether it is ticked.
        void setFontSizeForAllAvailable(bool available);
        bool fontSizeForAll() const;

    signals:
        void attributesChanged();

    private:
        enum Field { Name, Shape, Colour, FontColour, FontSize, Fire, FieldCount };

        struct MergeRow {
            QFrame* left_chip = nullptr;
            QFrame* right_chip = nullptr;
            QToolButton* take_left = nullptr;
            QToolButton* take_right = nullptr;
        };

        void build(const QList<QColor>& recent_colours);
        QWidget* createEditor(Field field, const QList<QColor>& recent_colours);
        QFrame* createChip(Field field, const hypergraph_logic::NodeAttributes& side);
        QWidget* createSideHeader(const hypergraph_logic::NodeAttributes& side,
            const QString& title, bool left);

        // Mutations (all end in sync()).
        void setBackground(const QColor& colour);
        void setFontColourManual(const QColor& colour);
        void setFontColourAutomatic();
        void setFireState(hypergraph_logic::FireState fire);
        void takeField(Field field, const hypergraph_logic::NodeAttributes& side);
        void takeAll(const hypergraph_logic::NodeAttributes& side);
        void rememberPicked(const QColor& colour);

        // Pushes current_ into every widget and refreshes the merge markers.
        void sync();
        static bool sameField(Field field, const hypergraph_logic::NodeAttributes& a,
            const hypergraph_logic::NodeAttributes& b);

        hypergraph_logic::NodeAttributes current_;
        bool font_auto_ = true;

        // Colours in use before a fire state locked them, restored when unticked.
        hypergraph_logic::Color pre_fire_colour_;
        hypergraph_logic::Color pre_fire_font_colour_;
        bool pre_fire_font_auto_ = true;

        bool syncing_ = false;
        QList<QColor> picked_colours_;

        // Editors.
        QLineEdit* name_ = nullptr;
        QListWidget* shape_ = nullptr;
        ColorPickerButton* colour_ = nullptr;
        ColorPickerButton* font_colour_ = nullptr;
        FontSizeSelector* font_size_ = nullptr;
        TickBox* font_size_all_ = nullptr;
        TickBox* fire_ = nullptr;
        TickBox* ashes_ = nullptr;

        // Merge mode.
        std::optional<hypergraph_logic::NodeAttributes> left_;
        std::optional<hypergraph_logic::NodeAttributes> right_;
        QString left_title_;
        QString right_title_;
        std::array<MergeRow, FieldCount> merge_rows_{};
    };

} // namespace ui
