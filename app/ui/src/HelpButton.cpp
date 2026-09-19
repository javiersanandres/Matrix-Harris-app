#include "HelpButton.h"
#include "HoverPopup.h"

#include <QEvent>
#include <QTimer>

namespace ui {

    namespace {
        // Grace period between "mouse left the button (or popup)" and actually
        // hiding the popup -- long enough that moving the cursor from the
        // button onto the popup itself (or back) doesn't cause a visible
        // flicker, short enough that it still feels instant when the mouse
        // genuinely moves away for good.
        constexpr int kHideDelayMs = 150;
    }

    HelpButton::HelpButton(QWidget* parent)
        : QToolButton(parent)
    {
        // Owned manually (see ~HelpButton), not parented: HoverPopup is a
        // top-level Qt::ToolTip window, so parenting it here would only affect
        // stacking/lifetime bookkeeping, not layout.
        popup_ = new HoverPopup(QString(), nullptr);
        connect(popup_, &HoverPopup::hoverChanged, this, [this](bool hovering) {
            if (hovering) cancelHide();
            else          scheduleHide();
            });

        hide_timer_ = new QTimer(this);
        hide_timer_->setSingleShot(true);
        hide_timer_->setInterval(kHideDelayMs);
        connect(hide_timer_, &QTimer::timeout, popup_, &QWidget::hide);

        // Click also shows it -- covers keyboard (Space/Enter) and touch
        // activation, which don't generate a genuine hover.
        connect(this, &QToolButton::clicked, this, &HelpButton::showPopup);
    }

    HelpButton::~HelpButton() {
        delete popup_; // no parent, so not torn down automatically
    }

    void HelpButton::showPopup() {
        cancelHide();
        popup_->setText(toolTip()); // stay in sync in case toolTip() changed
        popup_->showNear(this);
    }

    void HelpButton::scheduleHide() {
        hide_timer_->start();
    }

    void HelpButton::cancelHide() {
        hide_timer_->stop();
    }

    void HelpButton::enterEvent(QEnterEvent* event) {
        QToolButton::enterEvent(event);
        showPopup();
    }

    void HelpButton::leaveEvent(QEvent* event) {
        QToolButton::leaveEvent(event);
        scheduleHide();
    }

    bool HelpButton::event(QEvent* e) {
        if (e->type() == QEvent::ToolTip) {
            return true; // suppress the native tooltip; HoverPopup replaces it
        }
        return QToolButton::event(e);
    }

} // namespace ui