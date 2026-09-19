#pragma once

#include <QToolButton>

class QTimer;

namespace ui {

    class HoverPopup;

    // ============================================================================
    // HelpButton
    //
    // A QToolButton that shows its explanatory text (from setToolTip(), reused
    // as the popup's content) the instant the mouse enters it -- no waiting for
    // Qt's default tooltip delay -- and keeps it open for as long as the mouse
    // stays over EITHER the button or the popup itself, hiding only once it has
    // left both (with a short grace period so moving from the button onto the
    // popup doesn't cause a flicker in between). Clicking the button shows the
    // same popup too, for keyboard/touch activation where there's no real hover.
    //
    // This bypasses Qt's native tooltip mechanism entirely (see event() below)
    // in favor of HoverPopup, since a native QToolTip isn't a real interactive
    // widget -- it can't itself be hovered onto -- and always waits for a fixed
    // delay before appearing.
    // ============================================================================
    class HelpButton : public QToolButton {
        Q_OBJECT

    public:
        explicit HelpButton(QWidget* parent = nullptr);
        ~HelpButton() override;

    protected:
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;
        bool event(QEvent* e) override; // swallows the native ToolTip event

    private:
        void showPopup();
        void scheduleHide();
        void cancelHide();

        HoverPopup* popup_;
        QTimer* hide_timer_;
    };

} // namespace ui