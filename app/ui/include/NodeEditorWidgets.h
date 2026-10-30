#pragma once

#include "Node.h"
#include "ProjectSettings.h"

#include <QAbstractButton>
#include <QColor>
#include <QFrame>
#include <QList>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>

#include <array>
#include <optional>

class QComboBox;
class QDialog;
class QScrollArea;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QSpinBox;

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

        // Sizes a dialog laid out as banner / scrolling body / footer to fit its
        // content, but never beyond the main window (or the screen, without a
        // parent), and centres it there. What does not fit is reached by
        // scrolling the body. (Defined in NodeDialogs.cpp.)
        void fitToWindow(QDialog* dialog, QWidget* banner, QScrollArea* scroll,
            QWidget* footer, int preferred_width);
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
        enum class Glyph { Create, Edit, Fuse, Settings };

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
    //
    // Resizable (setResizable), it is also where the box's size is chosen: its
    // edges and corners are dragged as on the canvas, the default size of its
    // shape stays behind it as a shadow once the box has a size of its own, and
    // the fields in the bottom right corner show the size in pixels, to type it,
    // with a button to go back to the shape's size. The preview zooms to keep
    // both in sight once the drag ends.
    // ============================================================================
    class NodePreview : public QWidget {
        Q_OBJECT
    public:
        explicit NodePreview(QWidget* parent = nullptr);

        void setAttributes(const hypergraph_logic::NodeAttributes& attributes);
        void setResizable(bool resizable);
        // The default size of each shape (the project's); the built-in ones until given.
        void setBoxSizes(const hypergraph_logic::BoxSizes& sizes);
        QSize sizeHint() const override { return resizable_ ? QSize(320, 240) : QSize(320, 150); }

    signals:
        // The user gave the box this size; an empty side follows its shape's default.
        void sizeEdited(std::optional<double> width, std::optional<double> height);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mouseReleaseEvent(QMouseEvent* event) override;
        void leaveEvent(QEvent* event) override;

    private:
        QRectF stage() const;      // where the box is drawn (the card minus hint and fields)
        double fitScale() const;   // the zoom that shows the box and its shadow comfortably
        QRectF boxRect() const;    // the box, in widget coordinates, at the current zoom
        void zoomToFit();          // glides to fitScale()
        void syncFields();

        hypergraph_logic::NodeAttributes attributes_;
        hypergraph_logic::BoxSizes box_sizes_;
        bool resizable_ = false;
        double scale_ = 0.0;       // 0 until the first layout
        QPointer<QVariantAnimation> zoom_;

        int hover_edges_ = 0;
        int drag_edges_ = 0;       // the edges being dragged (0 when not resizing)
        QPointF press_pos_;
        QSizeF press_size_;

        QWidget* fields_ = nullptr;
        QSpinBox* width_ = nullptr;
        QSpinBox* height_ = nullptr;
        QToolButton* reset_ = nullptr;
        bool syncing_ = false;
    };

    // ============================================================================
    // NodeAttributesForm
    //
    // Every editable attribute of a node:
    //   - Nombre             : typed.
    //   - Forma              : picked from a list (rectangle, rounded rectangle,
    //                          circle, rhombus).
    //   - Color de fondo     : ColorPickerButton.
    //   - Color del texto    : ColorPickerButton with "Automático", which follows
    //                          the background (black or white, whichever reads
    //                          better) until the user picks a colour explicitly.
    //                          Both colours optionally have a tick box to make
    //                          them the project's defaults for new boxes.
    //   - Tamaño de fuente   : FontSizeSelector, optionally with tick boxes to
    //                          give that size to every box of the diagram and to
    //                          make it the project's default, stacked on its right.
    //   - Capa negra / Hogar : two mutually exclusive tick boxes. Capa negra forces
    //                          a black background and white text, Hogar a white
    //                          background and black text (both lock the pickers);
    //                          unticking restores the colours in use before.
    //
    // Merge mode (when both `left` and `right` are given) lays the same editors
    // out as a three-way merge: each row shows the left node's value, a button
    // to take it, the editable result, a button to take the right node's value,
    // and the right node's value. Rows where the two nodes differ are flagged.
    // It also has a "Tamaño" row, to take either node's size.
    //
    // The box's size is chosen in the dialog's preview (NodePreview), which
    // hands it over through setBoxSize.
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

        // Gives the box this size; an empty side follows its shape's default.
        void setBoxSize(std::optional<double> width, std::optional<double> height);

        // The project the box belongs to, whose default sizes the sizes shown
        // are measured with (nullptr: the built-in ones).
        void setProject(const app_logic::ProjectSettings* project);

        // Colours the user explicitly picked while the form was open, so the
        // project can offer them again next time.
        const QList<QColor>& pickedColours() const { return picked_colours_; }

        void focusName();

        // Shows, under the font size, a tick box to give that size to every box of
        // the diagram (hidden by default), and whether it is ticked.
        void setFontSizeForAllAvailable(bool available);
        bool fontSizeForAll() const;

        // Shows, next to each colour and the font size, a tick box to make it the
        // project's default for new boxes (hidden by default), and whether each is
        // ticked. The colours' are never ticked while a fire state imposes them.
        void setProjectDefaultsAvailable(bool available);
        bool colourAsDefault() const;
        bool fontColourAsDefault() const;
        bool fontSizeAsDefault() const;

    signals:
        void attributesChanged();

    private:
        enum Field { Name, Shape, Size, Colour, FontColour, FontSize, Fire, FieldCount };

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
        std::optional<app_logic::ProjectSettings> project_;

        // Editors.
        QLineEdit* name_ = nullptr;
        QListWidget* shape_ = nullptr;
        QLabel* size_ = nullptr; // merge mode only
        ColorPickerButton* colour_ = nullptr;
        ColorPickerButton* font_colour_ = nullptr;
        TickBox* colour_default_ = nullptr;
        TickBox* font_colour_default_ = nullptr;
        FontSizeSelector* font_size_ = nullptr;
        TickBox* font_size_all_ = nullptr;
        TickBox* font_size_default_ = nullptr;
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
