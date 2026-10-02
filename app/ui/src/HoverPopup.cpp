#include "HoverPopup.h"

#include "UiStyle.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
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
        // The card is painted by hand (paintEvent): a see-through window, with
        // the text inside it and room around it for the shadow.
        setAttribute(Qt::WA_TranslucentBackground);
        setTextFormat(Qt::RichText);
        setWordWrap(true);
        setContentsMargins(SHADOW + 16, SHADOW + 16, SHADOW + 16, SHADOW + 14);
        setFixedWidth(340 + 2 * SHADOW);
        setStyleSheet(QStringLiteral("QLabel { background: transparent; color: #4B5068; font-size: 9.5pt; }"));
    }

    void HoverPopup::showNear(QWidget* anchor) {
        adjustSize();

        // The card (not its shadow margin) just below the button.
        QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 4)) - QPoint(SHADOW, SHADOW);

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

    void HoverPopup::paintEvent(QPaintEvent* event) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(SHADOW, SHADOW - 2, -SHADOW, -SHADOW - 2);
        constexpr double RADIUS = 12.0;

        // Soft shadow: rings fading outwards, a little lower than the card.
        p.setPen(Qt::NoPen);
        for (int i = SHADOW; i >= 1; --i) {
            const double t = 1.0 - i / double(SHADOW + 1);
            p.setBrush(QColor(31, 35, 48, static_cast<int>(14 * t * t)));
            p.drawRoundedRect(card.adjusted(-i, -i + 3, i, i + 3), RADIUS + i, RADIUS + i);
        }

        // The card, and a thin accent line along its top.
        QPainterPath shape;
        shape.addRoundedRect(card, RADIUS, RADIUS);
        p.fillPath(shape, Qt::white);
        p.save();
        p.setClipPath(shape);
        QLinearGradient accent(card.topLeft(), card.topRight());
        accent.setColorAt(0, style::palette::accent);
        accent.setColorAt(1, style::palette::violet);
        p.fillRect(QRectF(card.left(), card.top(), card.width(), 4), accent);
        p.restore();
        p.setPen(QPen(QColor(0xDC, 0xDF, 0xEA), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), RADIUS, RADIUS);
        p.end();

        // The text, inside the margins.
        QLabel::paintEvent(event);
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