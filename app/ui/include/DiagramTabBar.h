#pragma once

#include <QAbstractButton>
#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QGraphicsScene>
#include <vector>

class QVariantAnimation;

namespace ui {

    class DiagramTabWidget;

    // ============================================================================
    // AddTabButton
    //
    // Small rounded square with a "+" that sits right after the last tab,
    // centred on the tabs' height. On hover it fades into the accent colour
    // (dashed outline, tint and plus), and it dips slightly while pressed.
    // ============================================================================
    class AddTabButton : public QAbstractButton {
        Q_OBJECT
    public:
        explicit AddTabButton(QWidget* parent = nullptr);

        static constexpr int SIZE = 56;

    protected:
        void paintEvent(QPaintEvent* event) override;
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;

    private:
        void animateHover(double to);

        QVariantAnimation* hover_anim_;
        double hover_ = 0.0; // 0 = idle, 1 = fully hovered
    };

    // ============================================================================
    // DiagramTabBar
    //
    // Horizontal bar:
    //   [ scrollable: tab0 | tab1 | ... | [+] ]  |  [ joint tab (fixed right) ]
    //
    // The strip scrolls only horizontally (the mouse wheel scrolls it sideways)
    // and always keeps room for its slim scrollbar, so the tabs never have to
    // scroll vertically when the scrollbar appears. Each regular tab has a bin
    // button that emits removeTabRequested(index).
    // ============================================================================
    class DiagramTabBar : public QWidget {
        Q_OBJECT

    public:
        explicit DiagramTabBar(QWidget* parent = nullptr);

        int  addTab(QGraphicsScene* scene, const QString& name);
        void removeTab(int index);
        void setActiveTab(int index);   // -1 = joint
        void setTabName(int index, const QString& name);
        void setJointScene(QGraphicsScene* scene);

        // Puts the tab's name into inline editing (focused, text selected).
        void startRename(int index);

        int tabCount() const { return static_cast<int>(tabs_.size()); }

    signals:
        void tabClicked(int index);
        void jointTabClicked();
        void addTabRequested();
        void tabRenamed(int index, const QString& new_name);
        void jointTabRenamed(const QString& new_name);
        void removeTabRequested(int index);

    protected:
        void paintEvent(QPaintEvent* event) override;
        bool eventFilter(QObject* watched, QEvent* event) override;

    private:
        // Scrolls the strip so tab is visible, once pending layout has settled.
        void revealLater(QWidget* tab);

        QHBoxLayout* outer_;
        QScrollArea* scroll_area_;
        QWidget* scroll_content_;
        QHBoxLayout* tabs_layout_;
        AddTabButton* add_button_;
        DiagramTabWidget* joint_tab_ = nullptr;

        std::vector<DiagramTabWidget*> tabs_;
        int active_index_ = 0;

        static constexpr int STRIP_PADDING = 4;   // around the tabs inside the strip
        static constexpr int SCROLLBAR_ROOM = 10; // reserved below the tabs for the scrollbar
    };

} // namespace ui
