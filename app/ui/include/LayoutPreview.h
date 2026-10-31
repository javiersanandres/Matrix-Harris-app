#pragma once

#include "GraphicalHypergraph.h"
#include "LayoutTypes.h"

#include <QPainterPath>
#include <QPointer>
#include <QRectF>
#include <QWidget>

#include <array>
#include <map>
#include <optional>

class QVariantAnimation;

namespace ui {

    // ============================================================================
    // LayoutPreview
    //
    // A small sample diagram drawn with the layout distances of the project
    // settings' form, by the real layout and renderer, so what it shows is what
    // the diagrams will look like:
    //
    //      ┌────┐ ←→ ┌────┐    ┌────┐
    //      │ 1  │    │ 2  │    │ 3  │
    //      └─┬──┘    └─┬┬─┘    └─┬──┘
    //        ↕ ────────┘│        │       the bars of two connections, stacked
    //        ↕ ─────────┴──┬─────┴──┐
    //      ┌────┐       ┌──┴─┐   ┌──┴─┐
    //      │ 4  │       │ 5  │   │ 6  │
    //      └────┘       └────┘   └────┘
    //
    // Each distance has a mark on the drawing where it can be seen; the active
    // one (setActive: the field under the mouse or being edited) stands out,
    // with its value. When the settings change, the boxes and lines glide to
    // their new places, or follow at once while a slider is dragged. The
    // drawing shrinks to fit the card, except while the scale is held
    // (holdScale), so a distance being dragged is seen to grow. Clicking a
    // mark asks for its field (distancePicked).
    // ============================================================================
    class LayoutPreview : public QWidget {
        Q_OBJECT
    public:
        enum class Distance { BlockSep, LayerGap, HorizontalSep };

        explicit LayoutPreview(QWidget* parent = nullptr);

        // Draws the sample with these distances, gliding there or at once. Their
        // default box sizes are left out: the sample's boxes keep the built-in
        // size, so only the distances change it.
        void setSettings(const hypergraph_logic::LayoutSettings& settings, bool glide = true);

        // Keeps the drawing's scale (while a distance is dragged); let go, it
        // glides back to the scale that fits.
        void holdScale(bool hold);

        // The distance to stand out (none: all marks are shown faintly).
        void setActive(std::optional<Distance> distance);

        // The distance as it is on the drawing once laid out (none if the
        // sample has nowhere to show it).
        std::optional<double> measured(Distance distance) const;

        QSize sizeHint() const override { return QSize(460, 230); }

    signals:
        void distancePicked(Distance distance);

    protected:
        void paintEvent(QPaintEvent* event) override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void leaveEvent(QEvent* event) override;

    private:
        // Where a distance is shown: a measure from `from` to `to` (diagram
        // coordinates), horizontal or vertical.
        struct Mark {
            QPointF from, to;
        };
        // The drawing at some settings.
        struct Frame {
            std::map<const hypergraph_logic::Node*, QRectF> boxes;
            std::map<const hypergraph_logic::Hyperedge*, QPainterPath> lines;
            std::array<std::optional<Mark>, 3> marks;
            QRectF bounds;
        };

        Frame layOut() const;
        Frame shown() const;           // part way between from_ and to_
        double fitScale(const Frame& frame) const;
        QTransform toWidget(const Frame& frame) const;
        std::optional<Distance> markAt(const QPointF& pos) const;

        hypergraph_logic::GraphicalHypergraph graph_;
        Frame from_, to_;
        double progress_ = 1.0;
        QVariantAnimation* glide_;
        std::optional<Distance> active_;
        std::optional<Distance> hovered_;
        double held_scale_ = 0.0;           // 0: not held
        QVariantAnimation* unhold_;         // from unhold_from_ to the scale that fits
        double unhold_from_ = 0.0, unhold_t_ = 1.0;
    };

} // namespace ui
