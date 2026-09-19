#include "HoverPopup.h"

#include <QEnterEvent>
#include <QScreen>
#include <algorithm>

namespace ui {

    HoverPopup::HoverPopup(const QString& richText, QWidget* parent)
        : QLabel(richText, parent)
    {
        // Qt::ToolTip: no window-manager frame, always on top, doesn't show in
        // the taskbar -- visually similar to a native tooltip, but this is a
        // real widget underneath, so it receives real enter/leave events.
        setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_ShowWithoutActivating); // never steals keyboard focus
        setTextFormat(Qt::RichText);
        setWordWrap(true);
        setMargin(8);
        setMaximumWidth(320);
        setStyleSheet(
            "QLabel {"
            "  background-color: #FFFFDC;"
            "  border: 1px solid #B0AA78;"
            "  border-radius: 4px;"
            "}");
    }

    void HoverPopup::showNear(QWidget* anchor) {
        adjustSize();

        QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 2));

        QScreen* screen = anchor->screen();
        if (screen != nullptr) {
            QRect avail = screen->availableGeometry();

            // Pull left if the popup would run off the right edge; clamp back
            // against the left edge too, in case the button sits close to it.
            if (pos.x() + width() > avail.right())
                pos.setX(avail.right() - width());
            pos.setX(qMax(pos.x(), avail.left()));

            // If it would run off the bottom, flip to show above the button
            // instead of just sliding up into it.
            if (pos.y() + height() > avail.bottom())
                pos.setY(anchor->mapToGlobal(QPoint(0, 0)).y() - height() - 2);
            pos.setY(qMax(pos.y(), avail.top()));
        }

        move(pos);
        show();
    }

    void HoverPopup::enterEvent(QEnterEvent* event) {
        QLabel::enterEvent(event);
        emit hoverChanged(true);
    }

    void HoverPopup::leaveEvent(QEvent* event) {
        QLabel::leaveEvent(event);
        emit hoverChanged(false);
    }

} // namespace ui