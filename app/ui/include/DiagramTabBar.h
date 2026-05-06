#pragma once

#include <QWidget>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QPushButton>
#include <QGraphicsScene>
#include <vector>

namespace ui {

    class DiagramTabWidget;

    // ============================================================================
    // DiagramTabBar
    //
    // Horizontal bar:
    //   [ scrollable: tab0 | tab1 | ... | [+] ]  [ joint tab (fixed right) ]
    //
    // Each regular tab has a trash button that emits removeRequested(index).
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

        int tabCount() const { return static_cast<int>(tabs_.size()); }

    signals:
        void tabClicked(int index);
        void jointTabClicked();
        void addTabRequested();
        void tabRenamed(int index, const QString& new_name);
        void jointTabRenamed(const QString& new_name);
        void removeTabRequested(int index);

    private:
        QScrollArea* scroll_area_;
        QWidget* scroll_content_;
        QHBoxLayout* tabs_layout_;
        QPushButton* add_button_;
        DiagramTabWidget* joint_tab_ = nullptr;

        std::vector<DiagramTabWidget*> tabs_;
        int active_index_ = 0;
    };

} // namespace ui