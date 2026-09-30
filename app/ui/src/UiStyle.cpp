#include "UiStyle.h"

#include <QHBoxLayout>
#include <QIconEngine>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

namespace ui::style {

    namespace {

        // ────────────────────────────────────────────────────────────────────────
        // Icon painting. Every glyph is drawn on a 20 x 20 canvas that the engine
        // scales to whatever size (and screen scale) Qt asks for.
        // ────────────────────────────────────────────────────────────────────────

        constexpr double CANVAS = 20.0;

        struct Canvas {
            QPainter& p;
            QIcon::Mode mode;

            QPen pen(const QColor& c, double w = 1.4, Qt::PenStyle s = Qt::SolidLine) const {
                QPen pen(c, w, s, Qt::RoundCap, Qt::RoundJoin);
                if (s == Qt::DashLine) pen.setDashPattern({ 1.6, 1.3 });
                return pen;
            }

            // A box that already exists.
            void existing(const QRectF& r) const {
                p.setPen(pen(palette::ink_soft));
                p.setBrush(Qt::white);
                p.drawRoundedRect(r, 1.8, 1.8);
            }
            // The box the operation starts from.
            void current(const QRectF& r) const {
                p.setPen(pen(palette::accent, 1.6));
                p.setBrush(palette::accent_soft);
                p.drawRoundedRect(r, 1.8, 1.8);
            }
            // A box the operation creates.
            void fresh(const QRectF& r) const {
                p.setPen(pen(palette::accent, 1.2));
                p.setBrush(palette::accent);
                p.drawRoundedRect(r, 1.8, 1.8);
                const double arm = std::min(r.width(), r.height()) * 0.28;
                p.setPen(pen(Qt::white, 1.3));
                p.drawLine(QPointF(r.center().x() - arm, r.center().y()), QPointF(r.center().x() + arm, r.center().y()));
                p.drawLine(QPointF(r.center().x(), r.center().y() - arm), QPointF(r.center().x(), r.center().y() + arm));
            }
            // An existing box the user will pick.
            void pick(const QRectF& r) const {
                p.setPen(pen(palette::accent, 1.4, Qt::DashLine));
                p.setBrush(Qt::white);
                p.drawRoundedRect(r, 1.8, 1.8);
            }
            void link(const QPointF& a, const QPointF& b, const QColor& c = palette::ink_soft) const {
                p.setPen(pen(c));
                p.drawLine(a, b);
            }
            void arrowDown(const QPointF& tip, const QColor& c = palette::ink_soft) const {
                p.setPen(pen(c));
                p.drawLine(tip, tip + QPointF(-2.2, -2.4));
                p.drawLine(tip, tip + QPointF(2.2, -2.4));
            }
            void cross(const QPointF& c, double r, const QColor& colour, double w = 1.8) const {
                p.setPen(pen(colour, w));
                p.drawLine(c + QPointF(-r, -r), c + QPointF(r, r));
                p.drawLine(c + QPointF(r, -r), c + QPointF(-r, r));
            }
            // A hyperedge: several boxes meeting at one bar, then continuing.
            void bar(double y, double x0, double x1) const {
                p.setPen(pen(palette::ink_soft));
                p.drawLine(QPointF(x0, y), QPointF(x1, y));
            }
        };

        void drawIcon(Icon which, Canvas& c) {
            QPainter& p = c.p;
            switch (which) {
            case Icon::Properties: {
                for (double y : { 5.0, 10.0, 15.0 }) c.link({ 3, y }, { 17, y });
                p.setPen(c.pen(palette::accent, 1.6));
                p.setBrush(Qt::white);
                p.drawEllipse(QPointF(7, 5), 2.2, 2.2);
                p.drawEllipse(QPointF(13, 10), 2.2, 2.2);
                p.drawEllipse(QPointF(9, 15), 2.2, 2.2);
                break;
            }
            case Icon::BoxAbove:
                c.link({ 10, 7.5 }, { 10, 12.5 });
                c.fresh({ 6, 1.5, 8, 6 });
                c.existing({ 4.5, 12.5, 11, 6 });
                break;
            case Icon::BoxBelow:
                c.link({ 10, 7.5 }, { 10, 12.5 });
                c.existing({ 4.5, 1.5, 11, 6 });
                c.fresh({ 6, 12.5, 8, 6 });
                break;
            case Icon::BoxLeft:
                c.fresh({ 1.5, 7, 7, 6 });
                c.existing({ 10.5, 5.5, 8, 9 });
                break;
            case Icon::BoxRight:
                c.existing({ 1.5, 5.5, 8, 9 });
                c.fresh({ 11.5, 7, 7, 6 });
                break;
            case Icon::CurrentBelow:
                c.link({ 10, 7.5 }, { 10, 12.2 });
                c.arrowDown({ 10, 12.2 });
                c.pick({ 5, 1.5, 10, 6 });
                c.current({ 4.5, 12.8, 11, 5.8 });
                break;
            case Icon::CurrentAbove:
                c.link({ 10, 7.5 }, { 10, 12.2 });
                c.arrowDown({ 10, 12.2 });
                c.current({ 4.5, 1.5, 11, 6 });
                c.pick({ 5, 12.8, 10, 5.8 });
                break;
            case Icon::RemovePartial:
                c.link({ 10, 7 }, { 10, 8.4 });
                c.link({ 10, 11.6 }, { 10, 13 });
                c.existing({ 5, 1.5, 10, 5.5 });
                c.existing({ 5, 13, 10, 5.5 });
                p.setPen(c.pen(palette::danger, 1.7));
                p.drawLine(QPointF(6.8, 11.6), QPointF(13.2, 8.4));
                break;
            case Icon::RemoveBox:
                p.setPen(c.pen(palette::danger, 1.5));
                p.setBrush(QColor(0xFD, 0xEC, 0xEC));
                p.drawRoundedRect(QRectF(3, 4.5, 14, 11), 2.2, 2.2);
                c.cross({ 10, 10 }, 2.4, palette::danger, 1.6);
                break;
            case Icon::Fuse:
                c.link({ 4.75, 6.5 }, { 9, 12.5 });
                c.link({ 15.25, 6.5 }, { 11, 12.5 });
                c.existing({ 1.5, 1.5, 6.5, 5 });
                c.existing({ 12, 1.5, 6.5, 5 });
                c.fresh({ 5.5, 12.5, 9, 6 });
                break;
            case Icon::BoxBetween:
                c.link({ 10, 1 }, { 10, 18.5 });
                c.arrowDown({ 10, 18.8 });
                c.fresh({ 4.5, 7, 11, 6 });
                break;
            case Icon::ForkUpNew:
            case Icon::ForkUpExisting:
            case Icon::Simplify: {
                const bool simplify = which == Icon::Simplify;
                c.link({ 5, 6.5 }, { 5, 10 });
                c.link({ 15, 6.5 }, { 15, 10 }, simplify ? palette::faint : palette::ink_soft);
                c.bar(10, 5, simplify ? 10 : 15);
                if (simplify) {
                    p.setPen(c.pen(palette::faint, 1.4, Qt::DashLine));
                    p.drawLine(QPointF(10, 10), QPointF(15, 10));
                }
                c.link({ 10, 10 }, { 10, 13 });
                c.existing({ 1.5, 1.5, 7, 5 });
                if (which == Icon::ForkUpNew) c.fresh({ 11.5, 1.5, 7, 5 });
                else if (which == Icon::ForkUpExisting) c.pick({ 11.5, 1.5, 7, 5 });
                else {
                    p.setPen(c.pen(palette::faint, 1.3, Qt::DashLine));
                    p.setBrush(Qt::white);
                    p.drawRoundedRect(QRectF(11.5, 1.5, 7, 5), 1.8, 1.8);
                    p.setPen(Qt::NoPen);
                    p.setBrush(palette::danger);
                    p.drawEllipse(QPointF(16.5, 6.5), 3.0, 3.0);
                    p.setPen(c.pen(Qt::white, 1.4));
                    p.drawLine(QPointF(15.1, 6.5), QPointF(17.9, 6.5));
                }
                c.existing({ 5.5, 13, 9, 5.5 });
                break;
            }
            case Icon::ForkDownNew:
            case Icon::ForkDownExisting:
                c.link({ 10, 7 }, { 10, 10 });
                c.bar(10, 5, 15);
                c.link({ 5, 10 }, { 5, 13.5 });
                c.link({ 15, 10 }, { 15, 13.5 });
                c.existing({ 5.5, 1.5, 9, 5.5 });
                c.existing({ 1.5, 13.5, 7, 5 });
                if (which == Icon::ForkDownNew) c.fresh({ 11.5, 13.5, 7, 5 });
                else c.pick({ 11.5, 13.5, 7, 5 });
                break;
            case Icon::RemoveConnection:
                c.link({ 10, 6.5 }, { 10, 13.5 }, palette::faint);
                c.existing({ 5, 1.5, 10, 5 });
                c.existing({ 5, 13.5, 10, 5 });
                c.cross({ 10, 10 }, 2.3, palette::danger);
                break;
            case Icon::NewBox:
                c.fresh({ 2.5, 4.5, 15, 11 });
                break;
            case Icon::Fast: {
                QPainterPath bolt;
                bolt.moveTo(12, 1.5);
                bolt.lineTo(4.5, 11.2);
                bolt.lineTo(9.3, 11.2);
                bolt.lineTo(7.8, 18.5);
                bolt.lineTo(15.5, 8.3);
                bolt.lineTo(10.6, 8.3);
                bolt.closeSubpath();
                p.setPen(c.pen(QColor(0xD9, 0x77, 0x06), 1.0));
                p.setBrush(palette::amber);
                p.drawPath(bolt);
                break;
            }
            case Icon::Slow: {
                const QColor dark(0x23, 0x80, 0x5A);
                const QColor skin(0x7C, 0xCB, 0xA3);
                p.setPen(c.pen(dark, 1.0));
                p.setBrush(skin);
                QPainterPath tail;
                tail.moveTo(3.2, 12.6);
                tail.lineTo(1.0, 13.9);
                tail.lineTo(3.2, 14.2);
                p.drawPath(tail);
                p.drawRoundedRect(QRectF(4.6, 13.2, 2.8, 3.4), 1.2, 1.2);
                p.drawRoundedRect(QRectF(11.0, 13.2, 2.8, 3.4), 1.2, 1.2);
                p.drawEllipse(QPointF(16.6, 11.4), 2.4, 2.1);
                QPainterPath shell;
                shell.moveTo(2.8, 13.6);
                shell.cubicTo(3.2, 4.8, 15.0, 4.8, 15.4, 13.6);
                shell.closeSubpath();
                p.setBrush(palette::green);
                p.drawPath(shell);
                p.setPen(c.pen(QColor(0xBD, 0xEB, 0xD5), 1.0));
                p.drawLine(QPointF(6.4, 13.2), QPointF(7.8, 9.4));
                p.drawLine(QPointF(7.8, 9.4), QPointF(10.4, 9.4));
                p.drawLine(QPointF(10.4, 9.4), QPointF(11.8, 13.2));
                p.setPen(Qt::NoPen);
                p.setBrush(dark);
                p.drawEllipse(QPointF(17.4, 10.9), 0.55, 0.55);
                break;
            }
            case Icon::Joint:
                p.setPen(c.pen(palette::ink_soft, 1.3));
                p.setBrush(Qt::white);
                p.drawRoundedRect(QRectF(1.5, 2.5, 11, 9), 2, 2);
                p.setPen(c.pen(palette::accent, 1.4));
                p.setBrush(palette::accent_soft);
                p.drawRoundedRect(QRectF(7.5, 8.5, 11, 9), 2, 2);
                break;
            case Icon::PdfDocument: {
                QPainterPath page;
                page.moveTo(4, 1.5);
                page.lineTo(12.5, 1.5);
                page.lineTo(16.5, 5.5);
                page.lineTo(16.5, 18.5);
                page.lineTo(4, 18.5);
                page.closeSubpath();
                p.setPen(c.pen(palette::ink_soft, 1.2));
                p.setBrush(Qt::white);
                p.drawPath(page);
                p.setBrush(QColor(0xE3, 0xE6, 0xEF));
                QPainterPath fold;
                fold.moveTo(12.5, 1.5);
                fold.lineTo(12.5, 5.5);
                fold.lineTo(16.5, 5.5);
                fold.closeSubpath();
                p.drawPath(fold);
                for (double y : { 7.5, 9.5 }) c.link({ 6.5, y }, { 11.5, y }, palette::faint);
                p.setPen(Qt::NoPen);
                p.setBrush(palette::danger);
                p.drawRoundedRect(QRectF(2.0, 11.5, 12.5, 5.5), 1.2, 1.2);
                QFont f;
                f.setPixelSize(4);
                f.setBold(true);
                p.setFont(f);
                p.setPen(Qt::white);
                p.drawText(QRectF(2.0, 11.5, 12.5, 5.5), Qt::AlignCenter, QStringLiteral("PDF"));
                break;
            }
            case Icon::LineSolid:
            case Icon::LineDashed: {
                // The line runs between two boxes and turns once, like a
                // connection on the canvas.
                QPen line = c.pen(palette::accent, 1.8);
                if (which == Icon::LineDashed) {
                    line.setCapStyle(Qt::FlatCap);
                    line.setDashPattern({ 1.6, 1.3 });
                }
                p.setPen(line);
                p.setBrush(Qt::NoBrush);
                QPainterPath path;
                path.moveTo(6.5, 6.5);
                path.lineTo(6.5, 10.0);
                path.lineTo(13.5, 10.0);
                path.lineTo(13.5, 13.5);
                p.drawPath(path);
                c.existing({ 2.5, 1.5, 8, 5 });
                c.existing({ 9.5, 13.5, 8, 5 });
                break;
            }
            case Icon::ImageFolder: {
                QPainterPath folder;
                folder.moveTo(1.5, 4.5);
                folder.lineTo(7.0, 4.5);
                folder.lineTo(8.8, 6.5);
                folder.lineTo(18.5, 6.5);
                folder.lineTo(18.5, 17.0);
                folder.lineTo(1.5, 17.0);
                folder.closeSubpath();
                p.setPen(c.pen(QColor(0xD9, 0x77, 0x06), 1.1));
                p.setBrush(QColor(0xFC, 0xD3, 0x4D));
                p.drawPath(folder);
                // A picture sticking out of the folder: frame, sun and hills.
                p.setPen(c.pen(palette::ink_soft, 1.1));
                p.setBrush(Qt::white);
                p.drawRoundedRect(QRectF(6.0, 8.8, 10.5, 7.2), 1.2, 1.2);
                p.setPen(Qt::NoPen);
                p.setBrush(palette::amber);
                p.drawEllipse(QPointF(13.8, 10.9), 1.1, 1.1);
                QPainterPath hills;
                hills.moveTo(6.8, 15.2);
                hills.lineTo(9.8, 11.6);
                hills.lineTo(12.0, 13.9);
                hills.lineTo(13.2, 12.8);
                hills.lineTo(15.7, 15.2);
                hills.closeSubpath();
                p.setBrush(palette::accent);
                p.drawPath(hills);
                break;
            }
            case Icon::Trash: {
                const bool hot = c.mode == QIcon::Active || c.mode == QIcon::Selected;
                const QColor col = hot ? palette::danger : palette::faint;
                p.setPen(c.pen(col, 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawLine(QPointF(3.5, 5), QPointF(16.5, 5));
                QPainterPath handle;
                handle.moveTo(7.8, 5);
                handle.lineTo(7.8, 3);
                handle.lineTo(12.2, 3);
                handle.lineTo(12.2, 5);
                p.drawPath(handle);
                QPainterPath body;
                body.moveTo(5.2, 5);
                body.lineTo(6.2, 17.5);
                body.lineTo(13.8, 17.5);
                body.lineTo(14.8, 5);
                p.drawPath(body);
                p.drawLine(QPointF(8.5, 8), QPointF(8.8, 14.5));
                p.drawLine(QPointF(11.5, 8), QPointF(11.2, 14.5));
                break;
            }
            case Icon::Move: {
                // Two joined boxes, and a four-way arrow at their right.
                p.setPen(c.pen(palette::ink_soft, 1.2));
                p.drawLine(QPointF(4.5, 7.5), QPointF(4.5, 12.5));
                c.existing({ 1.5, 3.0, 6.0, 4.5 });
                c.existing({ 1.5, 12.5, 6.0, 4.5 });
                const QPointF m(14.0, 10.0);
                p.setPen(c.pen(palette::accent, 1.5));
                p.drawLine(m + QPointF(-4.5, 0), m + QPointF(4.5, 0));
                p.drawLine(m + QPointF(0, -6.5), m + QPointF(0, 6.5));
                p.setBrush(palette::accent);
                auto head = [&](QPointF tip, QPointF dir) {
                    const QPointF side(-dir.y(), dir.x());
                    QPolygonF tri{ tip, tip - dir * 2.4 + side * 1.9, tip - dir * 2.4 - side * 1.9 };
                    p.drawPolygon(tri);
                };
                p.setPen(c.pen(palette::accent, 0.8));
                head(m + QPointF(5.5, 0), { 1, 0 });
                head(m + QPointF(-5.5, 0), { -1, 0 });
                head(m + QPointF(0, -7.5), { 0, -1 });
                head(m + QPointF(0, 7.5), { 0, 1 });
                break;
            }
            case Icon::TakeOut: {
                // The joint's dashed frame, and a piece leaving it up and to the right.
                p.setPen(c.pen(palette::faint, 1.2, Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(QRectF(1.5, 6.0, 12.0, 12.0), 2.0, 2.0);
                p.setPen(c.pen(palette::danger, 1.4));
                p.setBrush(QColor(0xFD, 0xEC, 0xEC));
                p.drawRoundedRect(QRectF(8.5, 1.5, 9.0, 6.5), 1.8, 1.8);
                p.drawLine(QPointF(5.0, 14.0), QPointF(10.0, 9.0));
                p.drawLine(QPointF(10.0, 9.0), QPointF(7.0, 9.0));
                p.drawLine(QPointF(10.0, 9.0), QPointF(10.0, 12.0));
                break;
            }
            case Icon::Pause: {
                p.setPen(Qt::NoPen);
                p.setBrush(palette::ink_soft);
                p.drawRoundedRect(QRectF(5.0, 4.0, 3.6, 12.0), 1.4, 1.4);
                p.drawRoundedRect(QRectF(11.4, 4.0, 3.6, 12.0), 1.4, 1.4);
                break;
            }
            }
        }

        // Paints an Icon at any size; disabled icons are drawn faded.
        class PaintedIconEngine : public QIconEngine {
        public:
            explicit PaintedIconEngine(Icon which) : which_(which) {}

            void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);
                const double side = std::min(rect.width(), rect.height());
                painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
                painter->scale(side / CANVAS, side / CANVAS);
                if (mode == QIcon::Disabled) painter->setOpacity(0.35);
                Canvas canvas{ *painter, mode };
                drawIcon(which_, canvas);
                painter->restore();
            }

            QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
                return scaledPixmap(size, mode, state, 1.0);
            }

            QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
                QPixmap pm(size * scale);
                pm.setDevicePixelRatio(scale);
                pm.fill(Qt::transparent);
                QPainter painter(&pm);
                paint(&painter, QRect(QPoint(0, 0), size), mode, state);
                return pm;
            }

            QIconEngine* clone() const override { return new PaintedIconEngine(which_); }

        private:
            Icon which_;
        };

        QString menuStyleSheet() {
            return QStringLiteral(R"(
QMenu {
    background: #FFFFFF; border: 1px solid #DDE0EA; border-radius: 12px; padding: 6px 5px;
}
QMenu::item {
    padding: 7px 26px 7px 6px; margin: 1px 2px; border-radius: 8px;
    color: #1F2330; background: transparent;
}
QMenu::item:selected { background: #EEF0FF; color: #312E81; }
QMenu::item:disabled { color: #B3B8C6; }
QMenu::separator { height: 1px; background: #ECEEF4; margin: 5px 10px; }
QMenu QLabel { background: transparent; }
QMenu QLabel#menuSection {
    color: #8A90A2; font-size: 7pt; font-weight: 700; letter-spacing: 1px; padding: 7px 12px 2px 12px;
}
QMenu QLabel#menuTitle { color: #1F2330; font-weight: 700; font-size: 10pt; }
QMenu QLabel#menuSubtitle { color: #8A90A2; font-size: 8pt; }
)");
        }

    } // namespace

    QIcon icon(Icon which) {
        return QIcon(new PaintedIconEngine(which));
    }

    void styleMenu(QMenu* menu) {
        // Frameless + translucent so the rounded corners show the window behind.
        menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        menu->setAttribute(Qt::WA_TranslucentBackground);
        menu->setStyleSheet(menuStyleSheet());
    }

    QMenu* createMenu(QWidget* parent) {
        auto* menu = new QMenu(parent);
        styleMenu(menu);
        return menu;
    }

    void addMenuHeader(QMenu* menu, const QPixmap& picture, const QString& title, const QString& subtitle) {
        auto* host = new QWidget(menu);
        auto* row = new QHBoxLayout(host);
        row->setContentsMargins(12, 6, 16, 8);
        row->setSpacing(10);

        if (!picture.isNull()) {
            auto* pic = new QLabel(host);
            pic->setPixmap(picture);
            row->addWidget(pic, 0, Qt::AlignVCenter);
        }

        auto* texts = new QVBoxLayout;
        texts->setSpacing(0);
        auto* t = new QLabel(title, host);
        t->setObjectName("menuTitle");
        t->setMaximumWidth(240);
        t->setText(t->fontMetrics().elidedText(title, Qt::ElideRight, 240));
        texts->addWidget(t);
        if (!subtitle.isEmpty()) {
            auto* s = new QLabel(subtitle, host);
            s->setObjectName("menuSubtitle");
            texts->addWidget(s);
        }
        row->addLayout(texts, 1);

        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(host);
        menu->addAction(action);
    }

    void addMenuSection(QMenu* menu, const QString& label) {
        auto* l = new QLabel(label.toUpper(), menu);
        l->setObjectName("menuSection");
        auto* action = new QWidgetAction(menu);
        action->setDefaultWidget(l);
        menu->addAction(action);
    }

    QColor mix(const QColor& a, const QColor& b, double t) {
        t = std::clamp(t, 0.0, 1.0);
        return QColor::fromRgbF(
            static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t),
            static_cast<float>(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
    }

} // namespace ui::style
