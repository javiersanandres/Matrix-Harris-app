#pragma once

#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

class QGraphicsScene;

namespace ui {

    class DiagramView;

    // ============================================================================
    // DiagramTabWidget
    //
    // One card in the horizontal tab bar: a live miniature of the diagram on
    // top and its name underneath.
    //
    //   - Clicking the card outside the miniature selects it (clicked()).
    //   - The miniature is a small live view of its own: Ctrl+wheel zooms,
    //     the wheel scrolls and dragging pans, without selecting the tab.
    //   - Double-clicking the name edits it in place: Enter or leaving the field
    //     commits (renamed()), Esc restores the previous name.
    //   - The name is centred under the miniature.
    //   - Hovering a regular tab reveals a bin button (removeRequested()). The
    //     joint tab has no bin, cannot be renamed and carries a "joint" glyph.
    //   - The active tab is outlined and tinted in the accent colour.
    //
    // The miniature view shares the same QGraphicsScene as the central editing
    // view, so it always reflects the current graph state for free; it re-fits
    // itself whenever the scene's extent changes.
    // ============================================================================
    class DiagramTabWidget : public QWidget {
        Q_OBJECT

    public:
        explicit DiagramTabWidget(QGraphicsScene* shared_scene,
            const QString& name,
            bool            is_joint,
            QWidget* parent = nullptr);

        void    setActive(bool active);
        bool    isActive() const { return active_; }

        void    setDisplayName(const QString& name);
        QString displayName() const { return name_; }

        bool isJoint() const { return is_joint_; }

        // Replaces the name with a focused, fully selected inline editor (used
        // right after creating a diagram). Does nothing on the joint tab.
        void startRename();

        // Shows another scene in the miniature (the joint tab after a project
        // is replaced).
        void setScene(QGraphicsScene* scene);

        static constexpr int TAB_WIDTH = 212;
        static constexpr int TAB_HEIGHT = 150;

    signals:
        void renamed(const QString& new_name);   // user committed a rename
        void clicked();                          // tab selected
        void removeRequested();                  // bin button pressed

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;

    private:
        void finishRename(bool commit);
        void refreshName();
        void refitMiniature();

        bool is_joint_;
        bool active_ = false;
        bool hovered_ = false;
        QString name_;

        DiagramView* miniature_view_;
        QWidget* name_box_;         // centres the glyph and the name
        QLabel* glyph_ = nullptr;   // joint tab only
        QLabel* name_label_;
        QLineEdit* name_edit_;
        QToolButton* trash_btn_;    // regular tabs only, shown on hover

        static constexpr int NAME_HEIGHT = 24;
    };

} // namespace ui
