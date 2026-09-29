#include "DiagramTabBar.h"
#include "DiagramTabWidget.h"
#include "UiStyle.h"

#include <QFrame>
#include <QVBoxLayout>
#include <QPainter>
#include <QScrollBar>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>

namespace ui {

    // ============================================================================
    // AddTabButton
    // ============================================================================

    AddTabButton::AddTabButton(QWidget* parent)
        : QAbstractButton(parent)
    {
        setFixedSize(SIZE, SIZE);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("Nuevo esquema"));

        hover_anim_ = new QVariantAnimation(this);
        hover_anim_->setDuration(180);
        hover_anim_->setEasingCurve(QEasingCurve::OutCubic);
        connect(hover_anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            hover_ = v.toDouble();
            update();
        });
    }

    void AddTabButton::animateHover(double to) {
        hover_anim_->stop();
        hover_anim_->setStartValue(hover_);
        hover_anim_->setEndValue(to);
        hover_anim_->start();
    }

    void AddTabButton::enterEvent(QEnterEvent* event) {
        QAbstractButton::enterEvent(event);
        animateHover(1.0);
    }

    void AddTabButton::leaveEvent(QEvent* event) {
        QAbstractButton::leaveEvent(event);
        animateHover(0.0);
    }

    void AddTabButton::paintEvent(QPaintEvent*) {
        using namespace style;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
        const double h = hover_;

        QColor fill = mix(QColor(255, 255, 255, 0), palette::accent_soft, h);
        if (isDown()) fill = palette::accent_soft.darker(106);
        QPen outline(mix(QColor(0xC9, 0xCD, 0xD9), palette::accent, h), 1.5, Qt::DashLine);
        outline.setDashPattern({ 4.0, 3.0 });
        p.setPen(outline);
        p.setBrush(fill);
        p.drawRoundedRect(card, 12, 12);

        // The plus grows a little under the mouse and dips while pressed.
        const double arm = (isDown() ? 8.5 : 9.0 + 1.5 * h);
        const QPointF c = card.center();
        p.setPen(QPen(mix(palette::faint, palette::accent, h), 3.0, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(c.x() - arm, c.y()), QPointF(c.x() + arm, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - arm), QPointF(c.x(), c.y() + arm));
    }

    // ============================================================================
    // DiagramTabBar
    // ============================================================================

    DiagramTabBar::DiagramTabBar(QWidget* parent)
        : QWidget(parent)
    {
        outer_ = new QHBoxLayout(this);
        outer_->setContentsMargins(8, 6, 10, 4);
        outer_->setSpacing(10);

        scroll_area_ = new QScrollArea(this);
        scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll_area_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_area_->setFrameShape(QFrame::NoFrame);
        scroll_area_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // Tabs plus the room the scrollbar needs: showing the scrollbar never
        // squeezes the tabs, so there is nothing to scroll vertically.
        scroll_area_->setFixedHeight(DiagramTabWidget::TAB_HEIGHT + 2 * STRIP_PADDING + SCROLLBAR_ROOM);
        scroll_area_->setStyleSheet(QStringLiteral(R"(
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 0 6px 1px 6px; }
QScrollBar::handle:horizontal { background: #C9CDD9; border-radius: 4px; min-width: 40px; }
QScrollBar::handle:horizontal:hover { background: #A5A8F5; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
)"));
        scroll_area_->viewport()->installEventFilter(this);

        scroll_content_ = new QWidget;
        tabs_layout_ = new QHBoxLayout(scroll_content_);
        tabs_layout_->setContentsMargins(2, STRIP_PADDING, 2, STRIP_PADDING);
        tabs_layout_->setSpacing(8);
        tabs_layout_->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        add_button_ = new AddTabButton(scroll_content_);
        tabs_layout_->addWidget(add_button_, 0, Qt::AlignVCenter);
        tabs_layout_->addStretch();

        scroll_area_->setWidget(scroll_content_);
        scroll_area_->setWidgetResizable(true);
        outer_->addWidget(scroll_area_, 1);

        connect(add_button_, &QAbstractButton::clicked,
            this, &DiagramTabBar::addTabRequested);
    }

    void DiagramTabBar::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.fillRect(rect(), style::palette::surface);
        p.setPen(style::palette::line);
        p.drawLine(QPoint(0, height() - 1), QPoint(width(), height() - 1));

        // Divider between the scrollable tabs and the joint tab.
        if (joint_tab_) {
            const int x = joint_tab_->geometry().left() - outer_->spacing() / 2 - 1;
            p.setPen(QColor(0xDD, 0xE0, 0xEA));
            p.drawLine(QPoint(x, joint_tab_->geometry().top() + 10), QPoint(x, joint_tab_->geometry().bottom() - 10));
        }
    }

    bool DiagramTabBar::eventFilter(QObject* watched, QEvent* event) {
        // The strip only scrolls sideways: turn the vertical wheel into that.
        if (watched == scroll_area_->viewport() && event->type() == QEvent::Wheel) {
            auto* wheel = static_cast<QWheelEvent*>(event);
            const QPoint delta = wheel->angleDelta();
            const int step = delta.x() != 0 ? delta.x() : delta.y();
            QScrollBar* bar = scroll_area_->horizontalScrollBar();
            bar->setValue(bar->value() - step);
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

    int DiagramTabBar::addTab(QGraphicsScene* scene, const QString& name) {
        auto* tab = new DiagramTabWidget(scene, name, false, scroll_content_);

        // Tabs go before the "+" card, which always follows the last tab.
        tabs_layout_->insertWidget(static_cast<int>(tabs_.size()), tab, 0, Qt::AlignTop);
        tabs_.push_back(tab);

        connect(tab, &DiagramTabWidget::clicked, this, [this, tab] {
            auto it = std::find(tabs_.begin(), tabs_.end(), tab);
            if (it != tabs_.end()) {
                int current_idx = static_cast<int>(std::distance(tabs_.begin(), it));
                setActiveTab(current_idx);
                emit tabClicked(current_idx);
            }
            });

        connect(tab, &DiagramTabWidget::renamed, this, [this, tab](const QString& n) {
            auto it = std::find(tabs_.begin(), tabs_.end(), tab);
            if (it != tabs_.end()) {
                int current_idx = static_cast<int>(std::distance(tabs_.begin(), it));
                emit tabRenamed(current_idx, n);
            }
            });

        connect(tab, &DiagramTabWidget::removeRequested, this, [this, tab] {
            auto it = std::find(tabs_.begin(), tabs_.end(), tab);
            if (it != tabs_.end()) {
                int current_idx = static_cast<int>(std::distance(tabs_.begin(), it));
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
        if (index >= 0 && index < static_cast<int>(tabs_.size()))
            revealLater(tabs_[index]);
    }

    void DiagramTabBar::revealLater(QWidget* tab) {
        // A tab that was just added has no geometry yet: lay the strip out
        // first, then scroll so the tab (and the "+" card next to it) shows.
        QPointer<QWidget> target(tab);
        QTimer::singleShot(0, this, [this, target] {
            if (!target) return;
            tabs_layout_->activate();
            scroll_content_->resize(
                std::max(scroll_content_->minimumSizeHint().width(), scroll_area_->viewport()->width()),
                scroll_area_->viewport()->height());
            scroll_area_->ensureWidgetVisible(target, AddTabButton::SIZE + 16, 0);
        });
    }

    void DiagramTabBar::setTabName(int index, const QString& name) {
        if (index >= 0 && index < static_cast<int>(tabs_.size()))
            tabs_[index]->setDisplayName(name);
    }

    void DiagramTabBar::startRename(int index) {
        if (index >= 0 && index < static_cast<int>(tabs_.size()))
            tabs_[index]->startRename();
    }

    void DiagramTabBar::setJointScene(QGraphicsScene* scene) {
        if (joint_tab_) {
            joint_tab_->setScene(scene); // a new project brings a new joint scene
            return;
        }
        {
            joint_tab_ = new DiagramTabWidget(scene, QStringLiteral("Esquema conjunto"), true, this);
            // Lined up with the tabs inside the strip (which sit STRIP_PADDING down).
            auto* column = new QVBoxLayout;
            column->setContentsMargins(0, STRIP_PADDING, 0, 0);
            column->addWidget(joint_tab_);
            column->addStretch();
            outer_->addLayout(column);

            connect(joint_tab_, &DiagramTabWidget::clicked,
                this, &DiagramTabBar::jointTabClicked);
            connect(joint_tab_, &DiagramTabWidget::renamed,
                this, &DiagramTabBar::jointTabRenamed);
        }
    }

} // namespace ui
