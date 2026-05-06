#include "DiagramTabBar.h"
#include "DiagramTabWidget.h"

#include <QScrollArea>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>

namespace ui {

    DiagramTabBar::DiagramTabBar(QWidget* parent)
        : QWidget(parent)
    {
        auto* outer = new QHBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        scroll_area_ = new QScrollArea(this);
        scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_area_->setFrameShape(QFrame::NoFrame);
        scroll_area_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        scroll_content_ = new QWidget;
        tabs_layout_ = new QHBoxLayout(scroll_content_);
        tabs_layout_->setContentsMargins(4, 4, 4, 4);
        tabs_layout_->setSpacing(6);
        tabs_layout_->addStretch();

        add_button_ = new QPushButton("+", scroll_content_);
        add_button_->setFixedSize(32, 32);
        add_button_->setToolTip("Nuevo diagrama");
        tabs_layout_->addWidget(add_button_);

        scroll_area_->setWidget(scroll_content_);
        scroll_area_->setWidgetResizable(true);
        outer->addWidget(scroll_area_);

        connect(add_button_, &QPushButton::clicked,
            this, &DiagramTabBar::addTabRequested);
    }

    int DiagramTabBar::addTab(QGraphicsScene* scene, const QString& name) {
        auto* tab = new DiagramTabWidget(scene, name, false, scroll_content_);

        int insert_pos = tabs_layout_->count() - 2;
        tabs_layout_->insertWidget(insert_pos, tab);
        tabs_.push_back(tab);

        connect(tab, &DiagramTabWidget::clicked, this, [this, tab] {
            auto it = std::find(tabs_.begin(), tabs_.end(), tab);
            if (it != tabs_.end()) {
                int current_idx = std::distance(tabs_.begin(), it);
                setActiveTab(current_idx);
                emit tabClicked(current_idx);
            }
            });

        connect(tab, &DiagramTabWidget::renamed, this, [this, tab](const QString& n) {
            auto it = std::find(tabs_.begin(), tabs_.end(), tab);
            if (it != tabs_.end()) {
                int current_idx = std::distance(tabs_.begin(), it);
                emit tabRenamed(current_idx, n);
            }
            });

        connect(tab, &DiagramTabWidget::removeRequested, this, [this, tab] {
            auto it = std::find(tabs_.begin(), tabs_.end(), tab);
            if (it != tabs_.end()) {
                int current_idx = std::distance(tabs_.begin(), it);
                emit removeTabRequested(current_idx);
            }
            });

        return static_cast<int>(tabs_.size()) - 1;
    }

    void DiagramTabBar::removeTab(int index) {
        if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
        auto* tab = tabs_[index];
        tabs_layout_->removeWidget(tab);
        delete tab;
        tabs_.erase(tabs_.begin() + index);
    }

    void DiagramTabBar::setActiveTab(int index) {
        active_index_ = index;
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i)
            tabs_[i]->setActive(i == index);
        if (joint_tab_)
            joint_tab_->setActive(index == -1);
    }

    void DiagramTabBar::setTabName(int index, const QString& name) {
        if (index >= 0 && index < static_cast<int>(tabs_.size()))
            tabs_[index]->setDisplayName(name);
    }

    void DiagramTabBar::setJointScene(QGraphicsScene* scene) {
        if (!joint_tab_) {
            joint_tab_ = new DiagramTabWidget(scene, "Diagrama conjunto", true, this);
            static_cast<QHBoxLayout*>(layout())->addWidget(joint_tab_);

            connect(joint_tab_, &DiagramTabWidget::clicked,
                this, &DiagramTabBar::jointTabClicked);
            connect(joint_tab_, &DiagramTabWidget::renamed,
                this, &DiagramTabBar::jointTabRenamed);
        }
    }

} // namespace ui
