#include "NodeVisuals.h"
#include "LayoutTypes.h"
#include "UiStyle.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygonF>
#include <QTextLayout>
#include <QTextOption>

#include <algorithm>
#include <cmath>

using namespace hypergraph_logic;

namespace ui::node_visuals {

    namespace {

        // Horizontal / vertical breathing room between the label and the shape.
        constexpr double LABEL_PAD_X = 6.0;
        constexpr double LABEL_PAD_Y = 4.0;

        // Lays out `text` wrapped to `width` (breaking long words if needed) and
        // horizontally centred. Returns the total height of all lines.
        double layoutLabel(QTextLayout& layout, double width) {
            QTextOption option;
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            option.setAlignment(Qt::AlignHCenter);
            layout.setTextOption(option);

            double y = 0.0;
            layout.beginLayout();
            while (true) {
                QTextLine line = layout.createLine();
                if (!line.isValid()) break;
                line.setLineWidth(width);
                line.setPosition(QPointF(0.0, y));
                y += line.height();
            }
            layout.endLayout();
            return y;
        }

    } // namespace

    // ============================================================================
    // Colour conversion
    // ============================================================================

    QColor toQColor(const Color& c) {
        return QColor(c.r, c.g, c.b, c.a);
    }

    Color fromQColor(const QColor& c) {
        return Color{ static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()),
                      static_cast<uint8_t>(c.blue()), static_cast<uint8_t>(c.alpha()) };
    }

    QColor contrastingTextColour(const QColor& background) {
        // WCAG relative luminance; pick whichever of black/white has more contrast.
        auto channel = [](double v) {
            v /= 255.0;
            return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        };
        const double lum = 0.2126 * channel(background.red())
            + 0.7152 * channel(background.green())
            + 0.0722 * channel(background.blue());
        const double contrast_black = (lum + 0.05) / 0.05;
        const double contrast_white = 1.05 / (lum + 0.05);
        return contrast_black >= contrast_white ? QColor(Qt::black) : QColor(Qt::white);
    }

    // ============================================================================
    // Geometry
    // ============================================================================

    QSizeF boxSize(const NodeAttributes& attributes, const BoxSizes* sizes) {
        return QSizeF(attributes.boxWidth(sizes), attributes.boxHeight(sizes));
    }

    QPainterPath shapePath(NodeShape shape, const QRectF& box) {
        QPainterPath path;
        switch (shape) {
        case NodeShape::Circle:
            path.addEllipse(box);
            break;
        case NodeShape::Rhombus: {
            QPolygonF rhombus;
            rhombus << QPointF(box.center().x(), box.top())
                << QPointF(box.right(), box.center().y())
                << QPointF(box.center().x(), box.bottom())
                << QPointF(box.left(), box.center().y());
            path.addPolygon(rhombus);
            path.closeSubpath();
            break;
        }
        case NodeShape::RoundedRectangle: {
            const double r = std::min({ ROUNDED_NODE_RADIUS, box.width() / 2.0, box.height() / 2.0 });
            path.addRoundedRect(box, r, r);
            break;
        }
        default:
            path.addRect(box);
            break;
        }
        return path;
    }

    QRectF labelRect(const NodeAttributes& attributes, const QRectF& box) {
        QRectF inner;
        switch (attributes.shape) {
        case NodeShape::Circle: {
            // Largest square inscribed in the circle.
            const double side = std::min(box.width(), box.height()) / std::sqrt(2.0);
            inner = QRectF(0, 0, side, side);
            inner.moveCenter(box.center());
            inner.adjust(2.0, 2.0, -2.0, -2.0);
            break;
        }
        case NodeShape::Rhombus:
            // Largest rectangle inscribed in the rhombus: half its width and height.
            inner = QRectF(0, 0, box.width() / 2.0, box.height() / 2.0);
            inner.moveCenter(box.center());
            break;
        case NodeShape::RoundedRectangle: {
            // A little more room at the sides, so the text corners stay clear of the arcs.
            const double extra = ROUNDED_NODE_RADIUS * 0.3;
            inner = box.adjusted(LABEL_PAD_X + extra, LABEL_PAD_Y, -LABEL_PAD_X - extra, -LABEL_PAD_Y);
            break;
        }
        default:
            inner = box.adjusted(LABEL_PAD_X, LABEL_PAD_Y, -LABEL_PAD_X, -LABEL_PAD_Y);
            break;
        }

        if (attributes.hasAshes()) {
            // Keep the text entirely inside the white top part.
            const double white_bottom = box.top() + box.height() * ASHES_WHITE_FRACTION - LABEL_PAD_Y;
            inner.setBottom(std::min(inner.bottom(), white_bottom));
        }
        return inner;
    }

    QFont labelFont(const NodeAttributes& attributes) {
        QFont font = QApplication::font();
        font.setPointSizeF(std::max(1, attributes.font_size));
        return font;
    }

    double maxLabelScroll(const NodeAttributes& attributes, const QRectF& box) {
        const QRectF area = labelRect(attributes, box);
        if (area.width() <= 0.0 || area.height() <= 0.0) return 0.0;
        QTextLayout layout(QString::fromStdString(attributes.name), labelFont(attributes));
        const double content = layoutLabel(layout, area.width());
        return std::max(0.0, content - area.height());
    }

    // ============================================================================
    // Painting
    // ============================================================================

    void paintNode(QPainter* painter, const NodeAttributes& attributes,
        const QRectF& box, const PaintOptions& options)
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::TextAntialiasing, true);

        const QPainterPath outline = shapePath(attributes.shape, box);
        NodeAttributes shown = attributes;
        shown.applyFireColours(); // a fire state decides the colours, whatever is stored
        QColor text_colour = toQColor(shown.font_colour);

        // ── Fill ─────────────────────────────────────────────────────────────────
        painter->setPen(Qt::NoPen);
        if (options.fill_override) {
            painter->setBrush(*options.fill_override);
            painter->drawPath(outline);
            text_colour = contrastingTextColour(*options.fill_override);
        }
        else if (attributes.hasAshes()) {
            painter->setBrush(Qt::white);
            painter->drawPath(outline);
            QRectF ashes = box;
            ashes.setTop(box.top() + box.height() * ASHES_WHITE_FRACTION);
            QPainterPath ashes_path;
            ashes_path.addRect(ashes);
            painter->setBrush(Qt::black);
            painter->drawPath(outline.intersected(ashes_path));
            text_colour = Qt::black;
        }
        else {
            painter->setBrush(toQColor(shown.colour));
            painter->drawPath(outline);
        }

        // ── Outline ──────────────────────────────────────────────────────────────
        painter->setBrush(Qt::NoBrush);
        painter->setPen(options.outline);
        painter->drawPath(outline);

        // ── Label (wrapped, clipped to its area, scrollable) ─────────────────────
        const QRectF area = labelRect(attributes, box);
        const QString text = QString::fromStdString(attributes.name);
        if (!text.isEmpty() && area.width() > 0.0 && area.height() > 0.0) {
            QTextLayout layout(text, labelFont(attributes));
            const double content = layoutLabel(layout, area.width());
            const double max_scroll = std::max(0.0, content - area.height());
            const double scroll = std::clamp(options.label_scroll, 0.0, max_scroll);

            const double top = (max_scroll > 0.0)
                ? area.top() - scroll
                : area.top() + (area.height() - content) / 2.0;

            painter->save();
            painter->setClipRect(area);
            painter->setPen(text_colour);
            layout.draw(painter, QPointF(area.left(), top));
            painter->restore();

            if (max_scroll > 0.0 && options.show_scroll_indicator) {
                // Slim scroll indicator along the label area's right edge.
                const double track_h = area.height();
                const double thumb_h = std::max(6.0, track_h * area.height() / content);
                const double thumb_y = area.top() + (track_h - thumb_h) * (scroll / max_scroll);
                QColor thumb = text_colour;
                thumb.setAlpha(140);
                painter->setPen(Qt::NoPen);
                painter->setBrush(thumb);
                painter->drawRoundedRect(QRectF(area.right() + 1.5, thumb_y, 3.0, thumb_h), 1.5, 1.5);
            }
        }

        painter->restore();
    }

    // ============================================================================
    // Resizing
    // ============================================================================

    int resizeEdgesAt(const QRectF& box, const QPointF& pos, double reach) {
        if (!box.adjusted(-reach, -reach, reach, reach).contains(pos)) return 0;
        int edges = 0;
        if (std::abs(pos.x() - box.left()) <= reach) edges |= ResizeLeft;
        else if (std::abs(pos.x() - box.right()) <= reach) edges |= ResizeRight;
        if (std::abs(pos.y() - box.top()) <= reach) edges |= ResizeTop;
        else if (std::abs(pos.y() - box.bottom()) <= reach) edges |= ResizeBottom;
        return edges;
    }

    Qt::CursorShape resizeCursor(int edges) {
        const bool horizontal = edges & (ResizeLeft | ResizeRight);
        const bool vertical = edges & (ResizeTop | ResizeBottom);
        if (horizontal && vertical) {
            // Top left and bottom right share a diagonal; the other two corners, the other one.
            const bool main_diagonal = bool(edges & ResizeLeft) == bool(edges & ResizeTop);
            return main_diagonal ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
        }
        if (horizontal) return Qt::SizeHorCursor;
        if (vertical)   return Qt::SizeVerCursor;
        return Qt::ArrowCursor;
    }

    QSizeF resizedBox(const QSizeF& start, int edges, const QPointF& delta) {
        double w = start.width();
        double h = start.height();
        if (edges & ResizeRight)  w += 2.0 * delta.x();
        if (edges & ResizeLeft)   w -= 2.0 * delta.x();
        if (edges & ResizeBottom) h += 2.0 * delta.y();
        if (edges & ResizeTop)    h -= 2.0 * delta.y();
        return QSizeF(std::round(std::clamp(w, MIN_NODE_WIDTH, MAX_NODE_WIDTH)),
                      std::round(std::clamp(h, MIN_NODE_HEIGHT, MAX_NODE_HEIGHT)));
    }

    void paintResizeFrame(QPainter* painter, const QRectF& box, int active, double unit) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // A soft indigo halo with a fine line on it.
        QColor halo = style::palette::accent;
        halo.setAlphaF(0.14f);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(halo, 5.0 * unit));
        painter->drawRect(box);
        QColor line = style::palette::accent;
        line.setAlphaF(0.75f);
        painter->setPen(QPen(line, 1.0 * unit));
        painter->drawRect(box);

        // Round knobs on the corners, pills along the edges.
        const QColor shadow(31, 35, 48, 45);
        auto knob = [&](const QPointF& c, int edges) {
            const bool on = edges == active;
            const double grow = on ? 1.25 : 1.0;
            QPainterPath path;
            if ((edges & (ResizeLeft | ResizeRight)) && (edges & (ResizeTop | ResizeBottom))) {
                const double r = 4.5 * grow * unit;
                path.addEllipse(c, r, r);
            }
            else {
                const bool along_x = edges & (ResizeTop | ResizeBottom);
                const double l = 14.0 * grow * unit, t = 6.0 * grow * unit;
                const QSizeF s = along_x ? QSizeF(l, t) : QSizeF(t, l);
                path.addRoundedRect(QRectF(c - QPointF(s.width(), s.height()) / 2.0, s), t / 2.0, t / 2.0);
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(shadow);
            painter->drawPath(path.translated(0.0, 1.0 * unit));
            if (on) {
                QLinearGradient fill(path.boundingRect().topLeft(), path.boundingRect().bottomRight());
                fill.setColorAt(0.0, style::palette::accent);
                fill.setColorAt(1.0, style::palette::violet);
                painter->setBrush(fill);
                painter->setPen(QPen(Qt::white, 1.4 * unit));
            }
            else {
                painter->setBrush(Qt::white);
                painter->setPen(QPen(style::palette::accent, 1.4 * unit));
            }
            painter->drawPath(path);
        };
        const QPointF c = box.center();
        knob(QPointF(c.x(), box.top()), ResizeTop);
        knob(QPointF(c.x(), box.bottom()), ResizeBottom);
        knob(QPointF(box.left(), c.y()), ResizeLeft);
        knob(QPointF(box.right(), c.y()), ResizeRight);
        knob(box.topLeft(), ResizeLeft | ResizeTop);
        knob(box.topRight(), ResizeRight | ResizeTop);
        knob(box.bottomLeft(), ResizeLeft | ResizeBottom);
        knob(box.bottomRight(), ResizeRight | ResizeBottom);
        painter->restore();
    }

    void paintSizeChip(QPainter* painter, const QRectF& box, double unit) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QString text = QStringLiteral("%1 × %2").arg(qRound(box.width())).arg(qRound(box.height()));
        QFont font = QApplication::font();
        font.setPointSizeF(8.0);
        font.setBold(true);
        // Measured and drawn in screen pixels, then scaled back into the painter's units.
        const QSizeF text_size = QFontMetricsF(font).size(Qt::TextSingleLine, text);
        const QSizeF chip_size(text_size.width() + 18.0, text_size.height() + 6.0);
        const QPointF anchor(box.center().x(), box.top() - 12.0 * unit);
        painter->translate(anchor);
        painter->scale(unit, unit);
        const QRectF chip(-chip_size.width() / 2.0, -chip_size.height(), chip_size.width(), chip_size.height());
        const double radius = chip.height() / 2.0;
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(31, 35, 48, 40));
        painter->drawRoundedRect(chip.translated(0.0, 1.5), radius, radius);
        QLinearGradient fill(chip.topLeft(), chip.topRight());
        fill.setColorAt(0.0, style::palette::accent);
        fill.setColorAt(1.0, style::palette::violet);
        painter->setBrush(fill);
        painter->drawRoundedRect(chip, radius, radius);
        painter->setFont(font);
        painter->setPen(Qt::white);
        painter->drawText(chip, Qt::AlignCenter, text);
        painter->restore();
    }

} // namespace ui::node_visuals
