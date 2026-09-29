#include "ViewOverlays.h"
#include "UiStyle.h"

#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QToolButton>

#include <algorithm>

namespace ui {

    // ============================================================================
    // FloatingPanel
    // ============================================================================

    FloatingPanel::FloatingPanel(QWidget* parent)
        : QFrame(parent)
    {
        setObjectName("floatingPanel");
        setMouseTracking(true);

        content_ = new QHBoxLayout(this);
        content_->setContentsMargins(GRIP_WIDTH + 4, 6, 7, 6);
        content_->setSpacing(6);
        content_->setSizeConstraint(QLayout::SetFixedSize); // always as big as its contents

        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(26);
        shadow->setOffset(0, 5);
        shadow->setColor(QColor(31, 35, 48, 55));
        setGraphicsEffect(shadow);

        parent->installEventFilter(this);
    }

    void FloatingPanel::placeInCorner(Qt::Corner corner, int margin) {
        anchor_right_ = corner == Qt::TopRightCorner || corner == Qt::BottomRightCorner;
        anchor_bottom_ = corner == Qt::BottomLeftCorner || corner == Qt::BottomRightCorner;
        corner_distance_ = QPoint(margin, margin);
        reanchor();
    }

    void FloatingPanel::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(style::palette::line, 1.0));
        p.setBrush(Qt::white);
        p.drawRoundedRect(card, 12, 12);

        // Grip: two columns of three dots.
        p.setPen(Qt::NoPen);
        p.setBrush(dragging_ ? style::palette::accent : style::palette::faint);
        const double cy = height() / 2.0;
        for (double x : { 8.0, 13.0 })
            for (double dy : { -5.0, 0.0, 5.0 })
                p.drawEllipse(QPointF(x, cy + dy), 1.4, 1.4);
    }

    void FloatingPanel::mousePressEvent(QMouseEvent* event) {
        if (event->button() != Qt::LeftButton) return QFrame::mousePressEvent(event);
        dragging_ = true;
        drag_offset_ = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        raise();
        update();
        event->accept();
    }

    void FloatingPanel::mouseMoveEvent(QMouseEvent* event) {
        if (dragging_) {
            moveClamped(mapToParent(event->position().toPoint()) - drag_offset_);
            event->accept();
            return;
        }
        // Only the grip advertises that the panel can be grabbed.
        setCursor(event->position().x() <= GRIP_WIDTH + 2 ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }

    void FloatingPanel::mouseReleaseEvent(QMouseEvent* event) {
        if (dragging_ && event->button() == Qt::LeftButton) {
            dragging_ = false;
            setCursor(Qt::OpenHandCursor);
            update();
            event->accept();
            return;
        }
        QFrame::mouseReleaseEvent(event);
    }

    bool FloatingPanel::eventFilter(QObject* watched, QEvent* event) {
        if (watched == parentWidget() && event->type() == QEvent::Resize) reanchor();
        return QFrame::eventFilter(watched, event);
    }

    void FloatingPanel::resizeEvent(QResizeEvent* event) {
        QFrame::resizeEvent(event);
        reanchor(); // e.g. a longer label keeps the panel glued to its corner
    }

    void FloatingPanel::moveClamped(const QPoint& pos) {
        const QWidget* host = parentWidget();
        if (!host) return;
        const int max_x = std::max(EDGE_GAP, host->width() - width() - EDGE_GAP);
        const int max_y = std::max(EDGE_GAP, host->height() - height() - EDGE_GAP);
        const int x = std::clamp(pos.x(), EDGE_GAP, max_x);
        const int y = std::clamp(pos.y(), EDGE_GAP, max_y);
        move(x, y);

        anchor_right_ = x + width() / 2 > host->width() / 2;
        anchor_bottom_ = y + height() / 2 > host->height() / 2;
        corner_distance_ = QPoint(anchor_right_ ? host->width() - (x + width()) : x,
            anchor_bottom_ ? host->height() - (y + height()) : y);
    }

    void FloatingPanel::reanchor() {
        const QWidget* host = parentWidget();
        if (!host) return;
        const int x = anchor_right_ ? host->width() - width() - corner_distance_.x() : corner_distance_.x();
        const int y = anchor_bottom_ ? host->height() - height() - corner_distance_.y() : corner_distance_.y();
        const int max_x = std::max(EDGE_GAP, host->width() - width() - EDGE_GAP);
        const int max_y = std::max(EDGE_GAP, host->height() - height() - EDGE_GAP);
        move(std::clamp(x, EDGE_GAP, max_x), std::clamp(y, EDGE_GAP, max_y));
    }

    // ============================================================================
    // HintBanner
    // ============================================================================

    HintBanner::HintBanner(QWidget* parent)
        : QFrame(parent)
    {
        setObjectName("hintBanner");
        setStyleSheet(QStringLiteral(R"(
QFrame#hintBanner { background: rgba(31, 35, 48, 235); border-radius: 19px; }
QLabel#hintText { color: white; font-weight: 600; }
QLabel#hintKey {
    color: #C7CAD6; border: 1px solid #5A6072; border-radius: 5px; padding: 0px 5px; font-size: 8pt;
}
QToolButton#hintCancel {
    color: #D9DBFF; background: rgba(255, 255, 255, 22); border: none; border-radius: 13px;
    padding: 4px 13px; font-weight: 600;
}
QToolButton#hintCancel:hover { background: rgba(255, 255, 255, 48); color: white; }
)"));

        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(18, 6, 6, 6);
        row->setSpacing(10);
        row->setSizeConstraint(QLayout::SetFixedSize);

        text_ = new QLabel(this);
        text_->setObjectName("hintText");
        row->addWidget(text_);

        auto* key = new QLabel(QStringLiteral("Esc"), this);
        key->setObjectName("hintKey");
        row->addWidget(key);

        cancel_ = new QToolButton(this);
        cancel_->setObjectName("hintCancel");
        cancel_->setText(QStringLiteral("Cancelar"));
        cancel_->setCursor(Qt::PointingHandCursor);
        row->addWidget(cancel_);
        connect(cancel_, &QToolButton::clicked, this, &HintBanner::cancelRequested);

        auto* opacity = new QGraphicsOpacityEffect(this);
        opacity->setOpacity(0.0);
        setGraphicsEffect(opacity);
        fade_ = new QPropertyAnimation(opacity, "opacity", this);
        fade_->setDuration(170);
        connect(fade_, &QPropertyAnimation::finished, this, [this] {
            if (fade_->endValue().toDouble() == 0.0) hide();
        });

        parent->installEventFilter(this);
        hide();
    }

    void HintBanner::setHint(const QString& text) {
        auto* opacity = static_cast<QGraphicsOpacityEffect*>(graphicsEffect());
        fade_->stop();
        fade_->setStartValue(opacity->opacity());
        if (text.isEmpty()) {
            if (!isVisible()) return;
            fade_->setEndValue(0.0);
        }
        else {
            text_->setText(text);
            adjustSize();
            recentre();
            show();
            raise();
            fade_->setEndValue(1.0);
        }
        fade_->start();
    }

    bool HintBanner::eventFilter(QObject* watched, QEvent* event) {
        if (watched == parentWidget() && event->type() == QEvent::Resize) recentre();
        return QFrame::eventFilter(watched, event);
    }

    void HintBanner::recentre() {
        if (!parentWidget()) return;
        move((parentWidget()->width() - width()) / 2, 14);
    }

} // namespace ui
