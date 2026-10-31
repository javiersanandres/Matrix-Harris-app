#include "SettingsWidgets.h"
#include "NodeVisuals.h"
#include "UiStyle.h"

#include <QEasingCurve>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

using namespace hypergraph_logic;

namespace ui {

    namespace nv = node_visuals;

    namespace {

        // The dotted "canvas" card of the previews.
        QPainterPath paintCard(QPainter& p, const QRect& rect) {
            const QRectF card = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
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
            return card_path;
        }

        double lerp(double a, double b, double t) { return a + (b - a) * t; }

    } // namespace

    // ============================================================================
    // DistanceSlider
    // ============================================================================

    namespace {
        constexpr double KNOB_RADIUS = 7.0;
        constexpr double KNOB_RADIUS_ACTIVE = 9.0;
    }

    DistanceSlider::DistanceSlider(int min, int max, QWidget* parent)
        : QWidget(parent)
        , min_(min), max_(max), value_(min), shown_(min)
        , glide_(new QVariantAnimation(this))
    {
        setFixedHeight(28);
        setMinimumWidth(160);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        glide_->setDuration(260);
        glide_->setEasingCurve(QEasingCurve::OutCubic);
        connect(glide_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            shown_ = v.toDouble();
            update();
        });
    }

    void DistanceSlider::setValue(int value) {
        value = std::clamp(value, min_, max_);
        if (value == value_) return;
        value_ = value;
        if (dragging_ || !isVisible()) {
            glide_->stop();
            shown_ = value_;
        }
        else {
            glide_->stop();
            glide_->setStartValue(shown_);
            glide_->setEndValue(static_cast<double>(value_));
            glide_->start();
        }
        update();
    }

    void DistanceSlider::pick(int value) {
        value = std::clamp(value, min_, max_);
        if (value == value_) return;
        value_ = value;
        glide_->stop();
        shown_ = value_;
        update();
        emit moved(value_);
    }

    double DistanceSlider::trackLeft() const { return KNOB_RADIUS_ACTIVE + 1.0; }
    double DistanceSlider::trackRight() const { return width() - KNOB_RADIUS_ACTIVE - 1.0; }

    double DistanceSlider::xOf(double value) const {
        const double f = max_ > min_ ? (value - min_) / (max_ - min_) : 0.0;
        return trackLeft() + f * (trackRight() - trackLeft());
    }

    int DistanceSlider::valueAt(double x) const {
        const double f = std::clamp((x - trackLeft()) / std::max(1.0, trackRight() - trackLeft()), 0.0, 1.0);
        return static_cast<int>(std::lround(min_ + f * (max_ - min_)));
    }

    void DistanceSlider::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double y = height() / 2.0;
        const double knob_x = xOf(shown_);

        // The track, filled up to the point.
        const QRectF track(trackLeft(), y - 2.0, trackRight() - trackLeft(), 4.0);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#E3E6EF"));
        p.drawRoundedRect(track, 2, 2);
        QRectF done = track;
        done.setRight(knob_x);
        QLinearGradient fill(track.topLeft(), track.topRight());
        fill.setColorAt(0, style::palette::accent);
        fill.setColorAt(1, style::palette::violet);
        p.setBrush(fill);
        p.drawRoundedRect(done, 2, 2);

        // The point: larger under the mouse, while dragged or with the focus.
        const bool active = dragging_ || hovered_ || hasFocus();
        const double r = active ? KNOB_RADIUS_ACTIVE : KNOB_RADIUS;
        if (dragging_ || hasFocus()) {
            QColor halo = style::palette::accent;
            halo.setAlpha(40);
            p.setBrush(halo);
            p.drawEllipse(QPointF(knob_x, y), r + 5.0, r + 5.0);
        }
        p.setBrush(QColor(31, 35, 48, 40));
        p.drawEllipse(QPointF(knob_x, y + 1.2), r, r);
        p.setBrush(Qt::white);
        p.setPen(QPen(style::palette::accent, 2.0));
        p.drawEllipse(QPointF(knob_x, y), r - 1.0, r - 1.0);
        p.setPen(Qt::NoPen);
        p.setBrush(style::palette::accent);
        p.drawEllipse(QPointF(knob_x, y), 2.5, 2.5);
    }

    void DistanceSlider::mousePressEvent(QMouseEvent* event) {
        if (event->button() != Qt::LeftButton) return QWidget::mousePressEvent(event);
        setFocus(Qt::MouseFocusReason);
        dragging_ = true;
        emit draggingChanged(true);
        pick(valueAt(event->position().x()));
        update();
    }

    void DistanceSlider::mouseMoveEvent(QMouseEvent* event) {
        if (dragging_) pick(valueAt(event->position().x()));
        QWidget::mouseMoveEvent(event);
    }

    void DistanceSlider::mouseReleaseEvent(QMouseEvent* event) {
        if (dragging_ && event->button() == Qt::LeftButton) {
            dragging_ = false;
            emit draggingChanged(false);
            update();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void DistanceSlider::keyPressEvent(QKeyEvent* event) {
        switch (event->key()) {
        case Qt::Key_Left:     case Qt::Key_Down:     pick(value_ - 1); break;
        case Qt::Key_Right:    case Qt::Key_Up:       pick(value_ + 1); break;
        case Qt::Key_PageDown: pick(value_ - 10); break;
        case Qt::Key_PageUp:   pick(value_ + 10); break;
        case Qt::Key_Home:     pick(min_); break;
        case Qt::Key_End:      pick(max_); break;
        default: QWidget::keyPressEvent(event); return;
        }
    }

    void DistanceSlider::enterEvent(QEnterEvent* event) {
        hovered_ = true;
        update();
        QWidget::enterEvent(event);
    }

    void DistanceSlider::leaveEvent(QEvent* event) {
        hovered_ = false;
        update();
        QWidget::leaveEvent(event);
    }

    // ============================================================================
    // ShapeSizesPreview
    // ============================================================================

    namespace {
        constexpr NodeShape SHAPES[] = { NodeShape::Rectangle, NodeShape::Circle, NodeShape::Rhombus };
        const QString NAMES[] = { QStringLiteral("Rectángulo y redondeado"), QStringLiteral("Círculo"), QStringLiteral("Rombo") };
        constexpr double SHAPE_GAP = 56.0;       // between two shapes, on screen
        constexpr double HANDLE_REACH = 7.0;
        constexpr double LABELS_HEIGHT = 40.0;   // the names and sizes under the shapes
    }

    ShapeSizesPreview::ShapeSizesPreview(QWidget* parent)
        : QWidget(parent)
        , glide_(new QVariantAnimation(this))
        , zoom_(new QVariantAnimation(this))
    {
        setMouseTracking(true);
        setMinimumHeight(200);
        const BoxSizes defaults;
        sizes_ = from_ = { QSizeF(defaults.rectangle.width, defaults.rectangle.height),
                           QSizeF(defaults.circle.width, defaults.circle.height),
                           QSizeF(defaults.rhombus.width, defaults.rhombus.height) };
        glide_->setDuration(280);
        glide_->setEasingCurve(QEasingCurve::OutCubic);
        glide_->setStartValue(0.0);
        glide_->setEndValue(1.0);
        connect(glide_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            progress_ = v.toDouble();
            update();
        });
        zoom_->setDuration(300);
        zoom_->setEasingCurve(QEasingCurve::OutCubic);
        zoom_->setStartValue(0.0);
        zoom_->setEndValue(1.0);
        connect(zoom_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            zoom_t_ = v.toDouble();
            scale_ = lerp(zoom_from_, zoom_to_, zoom_t_);
            update();
        });
        connect(zoom_, &QVariantAnimation::finished, this, [this] { releasing_ = false; });
    }

    void ShapeSizesPreview::setSizes(const BoxSizes& sizes) {
        const std::array<QSizeF, COUNT> next = { QSizeF(sizes.rectangle.width, sizes.rectangle.height),
                                                 QSizeF(sizes.circle.width, sizes.circle.height),
                                                 QSizeF(sizes.rhombus.width, sizes.rhombus.height) };
        if (next == sizes_) return;
        if (drag_shape_ >= 0 || !isVisible()) {
            // Dragged: right under the mouse, no glide.
            glide_->stop();
            sizes_ = from_ = next;
            progress_ = 1.0;
            if (drag_shape_ < 0) scale_ = 0.0;
        }
        else {
            from_ = shownSizes();
            sizes_ = next;
            progress_ = 0.0;
            glide_->stop();
            glide_->start();
            zoomToFit();
        }
        update();
    }

    std::array<QSizeF, ShapeSizesPreview::COUNT> ShapeSizesPreview::shownSizes() const {
        if (progress_ >= 1.0) return sizes_;
        std::array<QSizeF, COUNT> shown;
        for (int i = 0; i < COUNT; ++i)
            shown[i] = QSizeF(lerp(from_[i].width(), sizes_[i].width(), progress_),
                lerp(from_[i].height(), sizes_[i].height(), progress_));
        return shown;
    }

    QRectF ShapeSizesPreview::stage() const {
        return QRectF(rect()).adjusted(0.0, 26.0, 0.0, -LABELS_HEIGHT - 8.0); // under the hint, over the labels
    }

    double ShapeSizesPreview::fitScale() const {
        const QRectF s = stage();
        double total = 0.0, tallest = 0.0;
        for (const QSizeF& size : sizes_) {
            total += size.width();
            tallest = std::max(tallest, size.height());
        }
        const double room_x = s.width() - (COUNT - 1) * SHAPE_GAP - 48.0;
        return std::max(0.05, std::min({ 1.2, room_x / total, (s.height() - 20.0) / tallest }));
    }

    std::array<double, ShapeSizesPreview::COUNT> ShapeSizesPreview::layOutCentres(double scale) const {
        const std::array<QSizeF, COUNT> sizes = shownSizes();
        double total = (COUNT - 1) * SHAPE_GAP;
        for (const QSizeF& size : sizes) total += size.width() * scale;
        std::array<double, COUNT> centres;
        double x = stage().center().x() - total / 2.0;
        for (int i = 0; i < COUNT; ++i) {
            centres[i] = x + sizes[i].width() * scale / 2.0;
            x += sizes[i].width() * scale + SHAPE_GAP;
        }
        return centres;
    }

    std::array<QRectF, ShapeSizesPreview::COUNT> ShapeSizesPreview::boxes() const {
        const double scale = scale_ > 0.0 ? scale_ : fitScale();
        const std::array<QSizeF, COUNT> sizes = shownSizes();
        std::array<double, COUNT> centres = layOutCentres(scale);
        if (drag_shape_ >= 0) centres = held_centres_;
        else if (releasing_)
            for (int i = 0; i < COUNT; ++i) centres[i] = lerp(held_centres_[i], centres[i], zoom_t_);
        std::array<QRectF, COUNT> boxes;
        const double y = stage().center().y();
        for (int i = 0; i < COUNT; ++i) {
            const QSizeF s = sizes[i] * scale;
            boxes[i] = QRectF(centres[i] - s.width() / 2.0, y - s.height() / 2.0, s.width(), s.height());
        }
        return boxes;
    }

    void ShapeSizesPreview::zoomToFit() {
        const double target = fitScale();
        if (scale_ <= 0.0 || !isVisible()) {
            zoom_->stop();
            scale_ = target;
            releasing_ = false;
            return;
        }
        zoom_->stop();
        zoom_from_ = scale_;
        zoom_to_ = target;
        zoom_t_ = 0.0;
        zoom_->start();
    }

    void ShapeSizesPreview::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPainterPath card = paintCard(p, rect());

        QFont small = font();
        small.setPointSizeF(8.0);
        p.setFont(small);
        p.setPen(style::palette::muted);
        p.drawText(QRectF(rect()).adjusted(14, 7, -14, 0), Qt::AlignLeft | Qt::AlignTop,
            QStringLiteral("Arrastra los bordes de una forma para cambiar su tamaño por defecto"));

        p.save();
        p.setClipPath(card);
        const double scale = scale_ > 0.0 ? scale_ : fitScale();
        const std::array<QRectF, COUNT> shown = boxes();
        const std::array<QSizeF, COUNT> sizes = shownSizes();
        double bottom = 0.0;
        for (const QRectF& b : shown) bottom = std::max(bottom, b.bottom());

        // The line they are centred on, as the boxes of a level.
        QColor line = style::palette::accent;
        line.setAlpha(45);
        p.setPen(QPen(line, 1.0, Qt::DashLine));
        p.drawLine(QPointF(14.0, stage().center().y()), QPointF(width() - 14.0, stage().center().y()));

        for (int i = 0; i < COUNT; ++i) {
            NodeAttributes a;
            a.shape = SHAPES[i];
            a.colour = { 0xEE, 0xF0, 0xFF, 255 };
            const QRectF box(-sizes[i].width() / 2.0, -sizes[i].height() / 2.0, sizes[i].width(), sizes[i].height());
            p.save();
            p.translate(shown[i].center());
            p.scale(scale, scale);
            // Soft drop shadow, as in the box dialog.
            p.save();
            p.translate(2.5 / scale, 3.5 / scale);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(31, 35, 48, 28));
            p.drawPath(nv::shapePath(a.shape, box));
            p.restore();
            nv::PaintOptions opts;
            opts.outline = QPen(QColor("#3A3F52"), 1.5 / scale);
            nv::paintNode(&p, a, box, opts);
            const bool lit = i == drag_shape_ || (drag_shape_ < 0 && i == hover_shape_);
            if (lit) nv::paintResizeFrame(&p, box, drag_shape_ >= 0 ? drag_edges_ : hover_edges_, 1.0 / scale);
            p.restore();

            // Its name and size underneath.
            const QRectF label(shown[i].center().x() - 80.0, bottom + 8.0, 160.0, 16.0);
            QFont bold = small;
            bold.setBold(true);
            p.setFont(bold);
            p.setPen(lit ? style::palette::accent_dark : QColor("#3A3F52"));
            p.drawText(label, Qt::AlignHCenter | Qt::AlignTop, NAMES[i]);
            p.setFont(small);
            p.setPen(lit ? style::palette::accent : style::palette::muted);
            p.drawText(label.translated(0.0, 16.0), Qt::AlignHCenter | Qt::AlignTop,
                QStringLiteral("%1 × %2 px").arg(qRound(sizes[i].width())).arg(qRound(sizes[i].height())));
        }
        p.restore();
    }

    void ShapeSizesPreview::resizeEvent(QResizeEvent* event) {
        QWidget::resizeEvent(event);
        zoom_->stop();
        releasing_ = false;
        scale_ = fitScale();
    }

    void ShapeSizesPreview::hoverAt(const QPointF& pos) {
        int shape = -1, edges = 0;
        const std::array<QRectF, COUNT> b = boxes();
        for (int i = 0; i < COUNT && !edges; ++i)
            if ((edges = nv::resizeEdgesAt(b[i], pos, HANDLE_REACH))) shape = i;
        // Over a shape, its frame shows where to drag.
        if (!edges)
            for (int i = 0; i < COUNT; ++i)
                if (b[i].adjusted(-HANDLE_REACH, -HANDLE_REACH, HANDLE_REACH, HANDLE_REACH).contains(pos)) shape = i;
        if (shape == hover_shape_ && edges == hover_edges_) return;
        hover_shape_ = shape;
        hover_edges_ = edges;
        if (edges) setCursor(nv::resizeCursor(edges));
        else       unsetCursor();
        update();
    }

    void ShapeSizesPreview::mousePressEvent(QMouseEvent* event) {
        if (event->button() == Qt::LeftButton && hover_edges_ && hover_shape_ >= 0) {
            // The shapes stay where they are while one is dragged, so it follows the mouse.
            const std::array<QRectF, COUNT> b = boxes();
            for (int i = 0; i < COUNT; ++i) held_centres_[i] = b[i].center().x();
            zoom_->stop();
            releasing_ = false;
            if (scale_ <= 0.0) scale_ = fitScale();
            glide_->stop();
            progress_ = 1.0;
            drag_shape_ = hover_shape_;
            drag_edges_ = hover_edges_;
            press_pos_ = event->position();
            press_size_ = sizes_[drag_shape_];
            update();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void ShapeSizesPreview::mouseMoveEvent(QMouseEvent* event) {
        if (drag_shape_ >= 0) {
            const QSizeF size = nv::resizedBox(press_size_, drag_edges_, (event->position() - press_pos_) / scale_);
            if (size != sizes_[drag_shape_]) emit sizeEdited(SHAPES[drag_shape_], size.width(), size.height());
            return;
        }
        hoverAt(event->position());
    }

    void ShapeSizesPreview::mouseReleaseEvent(QMouseEvent* event) {
        if (drag_shape_ >= 0 && event->button() == Qt::LeftButton) {
            drag_shape_ = -1;
            drag_edges_ = 0;
            // Room again for all of them: they glide back to their places.
            releasing_ = true;
            zoomToFit();
            hover_shape_ = -1;
            hover_edges_ = 0;
            hoverAt(event->position());
            update();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void ShapeSizesPreview::leaveEvent(QEvent* event) {
        if (drag_shape_ < 0 && hover_shape_ >= 0) {
            hover_shape_ = -1;
            hover_edges_ = 0;
            unsetCursor();
            update();
        }
        QWidget::leaveEvent(event);
    }

} // namespace ui
