#include "LayoutPreview.h"
#include "HypergraphEditor.h"
#include "HypergraphRenderer.h"
#include "NodeVisuals.h"
#include "UiStyle.h"

#include <QEasingCurve>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace hypergraph_logic;

namespace ui {

    namespace nv = node_visuals;

    namespace {

        // 1, 2 and 3 above 4, 5 and 6: 1 reaches 4 and 6 through one connection,
        // and 2 and 3 reach 5 through another. Their bars run over each other,
        // so they are stacked.
        GraphicalHypergraph sample() {
            app_logic::HypergraphEditor ed{ GraphicalHypergraph("Muestra") };
            auto n1 = ed.createNode(NodeAttributes("1"), 0, -1, nullptr);
            auto n2 = ed.createNode(NodeAttributes("2"), 0, -1, nullptr);
            auto n3 = ed.createNode(NodeAttributes("3"), 0, -1, nullptr);
            auto n4 = ed.createNode(NodeAttributes("4"), 1, -1, n1);
            auto n5 = ed.createNode(NodeAttributes("5"), 1, -1, nullptr);
            auto n6 = ed.createNode(NodeAttributes("6"), 1, -1, nullptr);
            auto edgeOf = [&](const NodePtr& source, const NodePtr& target) {
                for (const auto& e : ed.getGraph().getAllHyperedges())
                    if (!e->isSegment() && e->containsSource(source) && e->containsTarget(target)) return e;
                return HyperedgePtr();
            };
            ed.addTargetToEdge(edgeOf(n1, n4), n6);
            ed.addConnection(n2, n5);
            ed.addSourceToEdge(edgeOf(n2, n5), n3);
            return ed.getGraph().clone();
        }

        // A horizontal piece of a connection's drawing.
        struct Run {
            const Hyperedge* edge;
            double y, x1, x2;
        };

        // Only the bars' own height counts: each bar starts with a move to it, and
        // the flat top of an arch over some verticals is not the bar.
        std::vector<Run> runsOf(const Hyperedge* edge, const QPainterPath& path) {
            std::vector<Run> runs;
            double bar_y = 0.0;
            for (int i = 0; i < path.elementCount(); ++i) {
                const QPainterPath::Element e = path.elementAt(i);
                if (e.type == QPainterPath::MoveToElement) bar_y = e.y;
                if (i == 0 || e.type != QPainterPath::LineToElement || std::abs(e.y - bar_y) > 0.01) continue;
                const QPainterPath::Element prev = path.elementAt(i - 1);
                if (std::abs(e.y - prev.y) > 0.01 || std::abs(e.x - prev.x) < 0.5) continue;
                runs.push_back({ edge, e.y, std::min(e.x, prev.x), std::max(e.x, prev.x) });
            }
            return runs;
        }

        // Where the vertical pieces of a drawing are.
        std::vector<double> verticalsOf(const QPainterPath& path) {
            std::vector<double> xs;
            for (int i = 1; i < path.elementCount(); ++i) {
                const QPainterPath::Element e = path.elementAt(i);
                const QPainterPath::Element prev = path.elementAt(i - 1);
                if (e.type == QPainterPath::LineToElement && std::abs(e.x - prev.x) < 0.01 && std::abs(e.y - prev.y) > 0.5)
                    xs.push_back(e.x);
            }
            return xs;
        }

        // The middle of the widest stretch of [x1, x2] that no vertical crosses,
        // so a vertical mark there is not mistaken for a line.
        double clearestX(double x1, double x2, std::vector<double> verticals) {
            verticals.erase(std::remove_if(verticals.begin(), verticals.end(),
                [&](double x) { return x <= x1 || x >= x2; }), verticals.end());
            verticals.push_back(x1);
            verticals.push_back(x2);
            std::sort(verticals.begin(), verticals.end());
            double best_x = (x1 + x2) / 2.0, widest = -1.0;
            for (size_t i = 1; i < verticals.size(); ++i)
                if (verticals[i] - verticals[i - 1] > widest) {
                    widest = verticals[i] - verticals[i - 1];
                    best_x = (verticals[i] + verticals[i - 1]) / 2.0;
                }
            return best_x;
        }

        // Whether a measure of `length` from `at` is a better place for a mark
        // than the best so far: shorter, or as short and further up and left
        // (so the same one is chosen every time).
        bool better(double length, const QPointF& at, double best_length, const std::optional<QPointF>& best_at) {
            if (length < best_length - 0.01) return true;
            if (length > best_length + 0.01 || !best_at) return false;
            return at.y() < best_at->y() - 0.01 || (at.y() < best_at->y() + 0.01 && at.x() < best_at->x());
        }

        QPointF lerp(const QPointF& a, const QPointF& b, double t) { return a + (b - a) * t; }

        QRectF lerp(const QRectF& a, const QRectF& b, double t) {
            return QRectF(lerp(a.topLeft(), b.topLeft(), t), lerp(a.bottomRight(), b.bottomRight(), t));
        }

        // Point by point when both drawings are made of the same pieces (the
        // usual case: the distances change, not the shape of the lines);
        // otherwise the new one, half way through.
        QPainterPath lerp(const QPainterPath& a, const QPainterPath& b, double t) {
            if (a.elementCount() != b.elementCount()) return t < 0.5 ? a : b;
            for (int i = 0; i < a.elementCount(); ++i)
                if (a.elementAt(i).type != b.elementAt(i).type) return t < 0.5 ? a : b;
            QPainterPath path = b;
            for (int i = 0; i < b.elementCount(); ++i)
                path.setElementPositionAt(i, a.elementAt(i).x + (b.elementAt(i).x - a.elementAt(i).x) * t,
                    a.elementAt(i).y + (b.elementAt(i).y - a.elementAt(i).y) * t);
            return path;
        }

        double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
            const QPointF ab = b - a;
            const double len2 = QPointF::dotProduct(ab, ab);
            const double t = len2 > 0.0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
            const QPointF d = p - (a + ab * t);
            return std::hypot(d.x(), d.y());
        }

        QString nameOf(LayoutPreview::Distance d) {
            switch (d) {
            case LayoutPreview::Distance::BlockSep:      return QStringLiteral("Entre cajas vecinas");
            case LayoutPreview::Distance::LayerGap:      return QStringLiteral("Entre cajas y conexiones");
            case LayoutPreview::Distance::HorizontalSep: return QStringLiteral("Entre líneas horizontales");
            }
            return {};
        }

        constexpr double PADDING = 26.0; // around the drawing, room for the marks' values

    } // namespace

    LayoutPreview::LayoutPreview(QWidget* parent)
        : QWidget(parent)
        , graph_(sample())
        , glide_(new QVariantAnimation(this))
        , unhold_(new QVariantAnimation(this))
    {
        setMouseTracking(true);
        setMinimumHeight(200);
        glide_->setDuration(420);
        glide_->setEasingCurve(QEasingCurve::OutCubic);
        glide_->setStartValue(0.0);
        glide_->setEndValue(1.0);
        connect(glide_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            progress_ = v.toDouble();
            update();
        });
        unhold_->setDuration(320);
        unhold_->setEasingCurve(QEasingCurve::OutCubic);
        unhold_->setStartValue(0.0);
        unhold_->setEndValue(1.0);
        connect(unhold_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            unhold_t_ = v.toDouble();
            update();
        });
        to_ = from_ = layOut();
    }

    void LayoutPreview::setSettings(const LayoutSettings& settings, bool glide) {
        // Only the distances: its boxes keep the built-in size whatever the defaults.
        LayoutSettings distances = settings.clamped();
        distances.box_sizes = BoxSizes();
        if (distances == graph_.getLayoutSettings()) return;
        graph_.setLayoutSettings(distances);
        // From wherever the drawing is now, even part way through a glide.
        from_ = shown();
        to_ = layOut();
        progress_ = 0.0;
        glide_->stop();
        if (glide && isVisible()) glide_->start();
        else progress_ = 1.0;
        update();
    }

    void LayoutPreview::holdScale(bool hold) {
        if (hold == (held_scale_ > 0.0)) return;
        const Frame frame = shown();
        if (hold) {
            unhold_->stop();
            held_scale_ = toWidget(frame).m11();
            return;
        }
        unhold_from_ = held_scale_;
        held_scale_ = 0.0;
        unhold_t_ = 0.0;
        unhold_->stop();
        if (isVisible()) unhold_->start();
        else unhold_t_ = 1.0;
        update();
    }

    void LayoutPreview::setActive(std::optional<Distance> distance) {
        if (active_ == distance) return;
        active_ = distance;
        update();
    }

    std::optional<double> LayoutPreview::measured(Distance distance) const {
        const auto& mark = to_.marks[static_cast<int>(distance)];
        if (!mark) return std::nullopt;
        const QPointF d = mark->to - mark->from;
        return std::hypot(d.x(), d.y());
    }

    LayoutPreview::Frame LayoutPreview::layOut() const {
        Frame frame;
        QGraphicsScene scene;
        std::unordered_map<Node*, QGraphicsPathItem*> node_items;
        std::unordered_map<Hyperedge*, QGraphicsPathItem*> edge_items;
        HypergraphRenderer::render(graph_, &scene, node_items, edge_items);
        for (const auto& [node, item] : node_items) frame.boxes[node] = item->path().boundingRect();
        std::vector<Run> runs;
        std::vector<double> verticals;
        for (const auto& [edge, item] : edge_items) {
            frame.lines[edge] = item->path();
            for (const Run& r : runsOf(edge, item->path())) runs.push_back(r);
            for (double x : verticalsOf(item->path())) verticals.push_back(x);
        }
        for (const auto& [node, box] : frame.boxes) frame.bounds |= box;
        for (const auto& [edge, path] : frame.lines) frame.bounds |= path.boundingRect();

        // Between neighbours: the closest two boxes side by side.
        std::optional<Mark> block;
        double best = std::numeric_limits<double>::max();
        for (const auto& [a, left] : frame.boxes)
            for (const auto& [b, right] : frame.boxes) {
                if (std::abs(left.center().y() - right.center().y()) > 0.5 || right.left() <= left.right()) continue;
                const double gap = right.left() - left.right();
                const QPointF at(left.right(), std::min(left.center().y(), right.center().y()));
                if (!better(gap, at, best, block ? std::optional(block->from) : std::nullopt)) continue;
                best = gap;
                block = Mark{ at, QPointF(right.left(), at.y()) };
            }
        frame.marks[static_cast<int>(Distance::BlockSep)] = block;

        // Between a box and the closest bar over or under it, where the bar
        // runs under the box (away from the box's corners).
        std::optional<Mark> layer;
        best = std::numeric_limits<double>::max();
        for (const auto& [node, box] : frame.boxes)
            for (const Run& r : runs) {
                const double x1 = std::max(r.x1, box.left() + 6.0), x2 = std::min(r.x2, box.right() - 6.0);
                if (x2 - x1 < 4.0) continue;
                const bool under = r.y > box.bottom();
                const double gap = under ? r.y - box.bottom() : box.top() - r.y;
                if (gap <= 0.0) continue;
                const double x = clearestX(x1, x2, verticals);
                const QPointF at(x, under ? box.bottom() : box.top());
                if (!better(gap, at, best, layer ? std::optional(layer->from) : std::nullopt)) continue;
                best = gap;
                layer = Mark{ at, QPointF(x, r.y) };
            }
        frame.marks[static_cast<int>(Distance::LayerGap)] = layer;

        // Between two stacked bars: the closest two running over each other.
        std::optional<Mark> stacked;
        best = std::numeric_limits<double>::max();
        for (const Run& a : runs)
            for (const Run& b : runs) {
                if (a.edge == b.edge || b.y <= a.y + 0.5) continue;
                const double x1 = std::max(a.x1, b.x1), x2 = std::min(a.x2, b.x2);
                if (x2 - x1 < 4.0) continue;
                const double x = clearestX(x1, x2, verticals);
                if (!better(b.y - a.y, QPointF(x, a.y), best, stacked ? std::optional(stacked->from) : std::nullopt)) continue;
                best = b.y - a.y;
                stacked = Mark{ QPointF(x, a.y), QPointF(x, b.y) };
            }
        frame.marks[static_cast<int>(Distance::HorizontalSep)] = stacked;
        return frame;
    }

    LayoutPreview::Frame LayoutPreview::shown() const {
        if (progress_ >= 1.0) return to_;
        const double t = progress_;
        Frame frame = to_;
        for (auto& [node, box] : frame.boxes)
            if (auto it = from_.boxes.find(node); it != from_.boxes.end()) box = lerp(it->second, box, t);
        for (auto& [edge, path] : frame.lines)
            if (auto it = from_.lines.find(edge); it != from_.lines.end()) path = lerp(it->second, path, t);
        for (size_t i = 0; i < frame.marks.size(); ++i)
            if (frame.marks[i] && from_.marks[i])
                frame.marks[i] = Mark{ lerp(from_.marks[i]->from, frame.marks[i]->from, t),
                                       lerp(from_.marks[i]->to, frame.marks[i]->to, t) };
        frame.bounds = lerp(from_.bounds, to_.bounds, t);
        return frame;
    }

    // As large as it fits, but never above its own size.
    double LayoutPreview::fitScale(const Frame& frame) const {
        const QRectF room = QRectF(rect()).adjusted(PADDING, PADDING + 8.0, -PADDING, -PADDING);
        if (frame.bounds.isEmpty() || room.isEmpty()) return 1.0;
        return std::min({ 1.0, room.width() / frame.bounds.width(), room.height() / frame.bounds.height() });
    }

    // The drawing centred, at the scale that fits (or the one held).
    QTransform LayoutPreview::toWidget(const Frame& frame) const {
        const QRectF room = QRectF(rect()).adjusted(PADDING, PADDING + 8.0, -PADDING, -PADDING);
        if (frame.bounds.isEmpty() || room.isEmpty()) return QTransform();
        double scale = fitScale(frame);
        if (held_scale_ > 0.0) scale = held_scale_;
        else if (unhold_t_ < 1.0) scale = unhold_from_ + (scale - unhold_from_) * unhold_t_;
        QTransform t;
        t.translate(room.center().x(), room.center().y());
        t.scale(scale, scale);
        t.translate(-frame.bounds.center().x(), -frame.bounds.center().y());
        return t;
    }

    std::optional<LayoutPreview::Distance> LayoutPreview::markAt(const QPointF& pos) const {
        const Frame frame = shown();
        const QTransform t = toWidget(frame);
        for (int i = 0; i < 3; ++i) {
            const auto& mark = frame.marks[i];
            if (mark && distanceToSegment(pos, t.map(mark->from), t.map(mark->to)) < 7.0)
                return static_cast<Distance>(i);
        }
        return std::nullopt;
    }

    void LayoutPreview::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Dotted "canvas" card, as the box dialogs' preview.
        const QRectF card = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath card_path;
        card_path.addRoundedRect(card, 14, 14);
        p.fillPath(card_path, QColor("#FBFBFE"));
        p.save();
        p.setClipPath(card_path);
        p.setPen(QPen(QColor("#DCDFEA"), 1.6, Qt::SolidLine, Qt::RoundCap));
        for (double x = 10; x < card.width(); x += 16)
            for (double y = 10; y < card.height(); y += 16)
                p.drawPoint(QPointF(x, y));
        p.restore();
        p.setPen(QPen(QColor("#E3E6EF"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(card_path);

        QFont small = font();
        small.setPointSizeF(8.0);
        p.setFont(small);
        p.setPen(style::palette::muted);
        p.drawText(card.adjusted(14, 7, -14, 0), Qt::AlignLeft | Qt::AlignTop,
            QStringLiteral("Arrastra un punto de arriba y mira cómo cambia; pasa el ratón por una medida para ver cuál es"));

        const Frame frame = shown();
        const QTransform t = toWidget(frame);
        p.save();
        p.setClipPath(card_path);

        // The connections, then the boxes, as on the canvas.
        p.setPen(HypergraphRenderer::connectionPen(true));
        p.setBrush(Qt::NoBrush);
        for (const auto& [edge, path] : frame.lines) p.drawPath(t.map(path));
        p.save();
        p.setTransform(t, true);
        for (const auto& [node, box] : frame.boxes) nv::paintNode(&p, node->getAttributes(), box);
        p.restore();

        // The marks: faint, but the active (or hovered) one stands out with its value.
        const std::optional<Distance> lit = hovered_ ? hovered_ : active_;
        for (int i = 0; i < 3; ++i) {
            const auto& mark = frame.marks[i];
            if (!mark) continue;
            const bool on = lit && static_cast<int>(*lit) == i;
            const QPointF a = t.map(mark->from), b = t.map(mark->to);
            const bool horizontal = std::abs(a.y() - b.y()) < 0.5;
            QColor colour = style::palette::accent;
            if (!on) colour.setAlpha(lit ? 70 : 140);
            p.setPen(QPen(colour, on ? 1.8 : 1.2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(a, b);
            // An arrowhead at each end, its tip on what is measured. A measure too
            // short for both has them outside, pointing in.
            const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
            if (len > 0.5) {
                const double head = on ? 7.0 : 5.5, half = on ? 3.5 : 2.8;
                const QPointF dir = (b - a) / len, side(-dir.y(), dir.x());
                const bool inside = len >= 2.0 * head + 2.0;
                p.setPen(Qt::NoPen);
                p.setBrush(colour);
                auto arrow = [&](const QPointF& tip, const QPointF& towards) {
                    p.drawPolygon(QPolygonF({ tip, tip - towards * head + side * half, tip - towards * head - side * half }));
                };
                arrow(a, inside ? -dir : dir);
                arrow(b, inside ? dir : -dir);
                if (!inside) {
                    // The arrows' shafts, outside.
                    p.setPen(QPen(colour, on ? 1.8 : 1.2, Qt::SolidLine, Qt::RoundCap));
                    p.drawLine(a, a - dir * (head + 5.0));
                    p.drawLine(b, b + dir * (head + 5.0));
                }
            }
            if (!on) continue;

            const QPointF d = mark->to - mark->from;
            const QString value = QStringLiteral("%1 px").arg(qRound(std::hypot(d.x(), d.y())));
            QFont bold = small;
            bold.setBold(true);
            p.setFont(bold);
            const QSizeF text = QFontMetricsF(bold).size(Qt::TextSingleLine, value);
            const QSizeF chip_size(text.width() + 12.0, text.height() + 4.0);
            const QPointF mid = (a + b) / 2.0;
            // Over a horizontal measure, beside a vertical one.
            QRectF chip = horizontal
                ? QRectF(QPointF(mid.x() - chip_size.width() / 2.0, mid.y() - chip_size.height() - 9.0), chip_size)
                : QRectF(QPointF(mid.x() + 9.0, mid.y() - chip_size.height() / 2.0), chip_size);
            p.setPen(Qt::NoPen);
            p.setBrush(style::palette::accent);
            p.drawRoundedRect(chip, chip.height() / 2.0, chip.height() / 2.0);
            p.setPen(Qt::white);
            p.drawText(chip, Qt::AlignCenter, value);
            p.setFont(small);
        }
        p.restore();
    }

    void LayoutPreview::mouseMoveEvent(QMouseEvent* event) {
        const std::optional<Distance> over = markAt(event->position());
        if (over != hovered_) {
            hovered_ = over;
            setCursor(over ? Qt::PointingHandCursor : Qt::ArrowCursor);
            setToolTip(over ? nameOf(*over) : QString());
            update();
        }
        QWidget::mouseMoveEvent(event);
    }

    void LayoutPreview::mousePressEvent(QMouseEvent* event) {
        if (event->button() == Qt::LeftButton)
            if (const std::optional<Distance> over = markAt(event->position())) {
                emit distancePicked(*over);
                return;
            }
        QWidget::mousePressEvent(event);
    }

    void LayoutPreview::leaveEvent(QEvent* event) {
        if (hovered_) {
            hovered_.reset();
            setCursor(Qt::ArrowCursor);
            update();
        }
        QWidget::leaveEvent(event);
    }

} // namespace ui
