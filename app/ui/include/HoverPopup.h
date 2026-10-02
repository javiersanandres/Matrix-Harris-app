#pragma once

#include <QLabel>

namespace ui {

    // ============================================================================
    // HoverPopup
    //
    // A small frameless, always-on-top label used as a custom "hover card" for
    // HelpButton, drawn as the application's cards: a white rounded rectangle
    // with a hairline border, a soft shadow and a thin accent line on top. Unlike a native QToolTip -- which is not a real interactive
    // widget and can't itself be hovered onto -- this is a genuine QWidget, so
    // entering/leaving IT (not just the button that spawned it) can factor into
    // "is the mouse still over something related to this popup".
    // ============================================================================
    class HoverPopup : public QLabel {
        Q_OBJECT

    public:
        explicit HoverPopup(const QString& richText, QWidget* parent = nullptr);

        // Positions itself just below-left of `anchor` (in global coordinates)
        // and shows itself without stealing keyboard focus from the app.
        void showNear(QWidget* anchor);

    signals:
        // Emitted when the mouse enters/leaves this popup's own area.
        void hoverChanged(bool hovering);

    protected:
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;
        void paintEvent(QPaintEvent* event) override;

    private:
        // Room around the card for its shadow.
        static constexpr int SHADOW = 12;
    };

} // namespace ui
