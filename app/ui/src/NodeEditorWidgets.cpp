#include "NodeEditorWidgets.h"
#include "NodeVisuals.h"

#include <QColorDialog>
#include <QEnterEvent>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>

using namespace hypergraph_logic;

namespace ui {

    namespace nv = node_visuals;

    // ============================================================================
    // Theme
    // ============================================================================

    namespace {
        const QColor ACCENT("#6366F1");
        const QColor INK("#1F2330");
        const QColor MUTED("#7A8194");
        const QColor BORDER("#D6DAE4");

        QString hexOf(const QColor& c) { return c.name(QColor::HexRgb).toUpper(); }

        QString shapeName(NodeShape shape) {
            switch (shape) {
            case NodeShape::Circle:  return QStringLiteral("Círculo");
            case NodeShape::Rhombus: return QStringLiteral("Rombo");
            default:                 return QStringLiteral("Rectángulo");
            }
        }

        QString fireName(FireState fire) {
            switch (fire) {
            case FireState::Fire:          return QStringLiteral("Fuego");
            case FireState::FireWithAshes: return QStringLiteral("Fuego con cenizas");
            default:                       return QStringLiteral("Sin fuego");
            }
        }

        // Small rounded colour sample with a thin border.
        QPixmap swatchPixmap(const QColor& colour, const QSize& size, qreal dpr) {
            QPixmap pm(size * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(0, 0, 0, 60), 1.0));
            p.setBrush(colour);
            p.drawRoundedRect(QRectF(0.5, 0.5, size.width() - 1.0, size.height() - 1.0), 4, 4);
            return pm;
        }

        // Neutral attributes used for the shape / fire icons.
        NodeAttributes iconAttributes(NodeShape shape, FireState fire = FireState::None) {
            NodeAttributes a;
            a.shape = shape;
            a.fire = fire;
            if (fire == FireState::None) a.colour = nv::fromQColor(QColor("#E8E9FF"));
            else                         a.colour = { 255, 255, 255, 255 };
            return a;
        }

        QColor mix(const QColor& c, const QColor& towards, double t) {
            return QColor::fromRgbF(
                c.redF() + (towards.redF() - c.redF()) * t,
                c.greenF() + (towards.greenF() - c.greenF()) * t,
                c.blueF() + (towards.blueF() - c.blueF()) * t);
        }

        QLabel* sectionTitle(const QString& text) {
            auto* label = new QLabel(text.toUpper());
            label->setObjectName("sectionTitle");
            return label;
        }

        QLabel* fieldLabel(const QString& text) {
            auto* label = new QLabel(text);
            label->setObjectName("fieldLabel");
            return label;
        }
    } // namespace

    namespace dialog_theme {
        QString styleSheet() {
            return QStringLiteral(R"(
QDialog#NodeDialog { background: transparent; }
QFrame#nodeDialogCard { background: #F5F6FA; border: 1px solid #C9CDD9; border-radius: 14px; }
QFrame#card { background: white; border: 1px solid #E3E6EF; border-radius: 14px; }
QLabel { color: #1F2330; }
QLabel#sectionTitle { color: #8A90A2; font-size: 8pt; font-weight: 700; letter-spacing: 1px; padding-top: 6px; }
QLabel#fieldLabel { color: #3A4050; font-weight: 600; }
QLabel#hint { color: #8A90A2; font-size: 8pt; }
QLabel#columnTitle { color: #1F2330; font-weight: 700; font-size: 10pt; }
QLabel#previewTitle { color: #8A90A2; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
QLineEdit, QComboBox {
    border: 1px solid #D6DAE4; border-radius: 8px; padding: 5px 8px;
    background: white; color: #1F2330; selection-background-color: #6366F1;
}
QLineEdit:focus, QComboBox:focus { border: 1px solid #6366F1; }
QComboBox::drop-down { border: none; width: 18px; }
QToolButton#pickerButton {
    border: 1px solid #D6DAE4; border-radius: 8px; padding: 4px 22px 4px 8px;
    background: white; color: #1F2330; text-align: left;
}
QToolButton#pickerButton:hover { border-color: #6366F1; background: #F7F7FF; }
QToolButton#pickerButton:disabled { color: #A0A5B5; background: #F3F4F7; border-color: #E3E6EF; }
QToolButton#pickerButton::menu-indicator { subcontrol-position: right center; right: 8px; }
QToolButton#sizeButton {
    border: 1px solid #D6DAE4; border-radius: 8px; background: white; padding: 3px;
}
QToolButton#sizeButton:hover { border-color: #6366F1; background: #F7F7FF; }
QMenu#colourMenu { background: white; border: 1px solid #D6DAE4; }
QToolButton#paletteWide {
    border: 1px solid transparent; border-radius: 6px; padding: 4px 6px;
    background: transparent; color: #1F2330; text-align: left;
}
QToolButton#paletteWide:hover { background: #EEF0FF; border-color: #C7C9F9; }
QPushButton#primary {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366F1, stop:1 #8B5CF6);
    color: white; border: none; border-radius: 9px; padding: 9px 26px; font-weight: 700;
}
QPushButton#primary:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #575AE8, stop:1 #7E4FEF);
}
QPushButton#primary:pressed { background: #4F46E5; }
QPushButton#primary:disabled { background: #C7C9D9; color: #F4F5FA; }
QPushButton#secondary {
    background: white; color: #3A4050; border: 1px solid #D6DAE4; border-radius: 9px;
    padding: 9px 20px; font-weight: 600;
}
QPushButton#secondary:hover { background: #F1F2F7; }
QPushButton#ghost {
    background: #EEF0FF; color: #4F46E5; border: 1px solid #D9DBFB; border-radius: 8px;
    padding: 5px 12px; font-weight: 600;
}
QPushButton#ghost:hover { background: #E2E4FF; }
QListWidget#shapeList { border: none; background: transparent; outline: none; }
QListWidget#shapeList::item {
    border: 1px solid #E3E6EF; border-radius: 10px; margin: 3px; padding-top: 4px;
    color: #3A4050; background: white;
}
QListWidget#shapeList::item:hover { border-color: #A5A8F5; background: #FAFAFF; }
QListWidget#shapeList::item:selected { border: 2px solid #6366F1; background: #EEF0FF; color: #1F2330; }
QToolButton#takeButton {
    border: 1px solid #D6DAE4; border-radius: 12px; background: white;
    min-width: 24px; min-height: 24px; color: #6366F1; font-weight: 700;
}
QToolButton#takeButton:hover { background: #EEF0FF; border-color: #6366F1; }
QToolButton#takeButton:disabled { color: #C4C7D3; border-color: #E3E6EF; }
QToolButton#takeButton[active="true"], QToolButton#takeButton[active="true"]:disabled {
    background: #6366F1; color: white; border-color: #6366F1;
}
QFrame#chip { background: #F4F5F9; border: 1px solid #E6E8EF; border-radius: 8px; }
QFrame#chip[conflict="true"] { background: #FFF7E6; border-color: #F5C26B; }
QFrame#sideHeader { background: #F8F8FC; border: 1px solid #E6E8EF; border-radius: 10px; }
QScrollArea#dialogScroll { background: transparent; border: none; }
QWidget#dialogBody { background: transparent; }
QFrame#dialogFooter {
    background: #F5F6FA; border: none; border-bottom-left-radius: 13px; border-bottom-right-radius: 13px;
}
QFrame#dialogFooter[divided="true"] { border-top: 1px solid #E3E6EF; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px 4px 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0 4px 2px 4px; }
QScrollBar::handle:vertical { background: #C9CDD9; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:horizontal { background: #C9CDD9; border-radius: 4px; min-width: 30px; }
QScrollBar::handle:hover { background: #A5A8F5; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
)");
        }

        void repolish(QWidget* w) {
            w->style()->unpolish(w);
            w->style()->polish(w);
            w->update();
        }
    } // namespace dialog_theme

    QPixmap renderNodeIcon(const NodeAttributes& attributes, const QSize& size, qreal dpr) {
        QPixmap pm(size * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);

        const QSizeF box = nv::boxSize(attributes);
        const double scale = std::min({ 1.0,
            (size.width() - 2.0) / box.width(),
            (size.height() - 2.0) / box.height() });

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(size.width() / 2.0, size.height() / 2.0);
        p.scale(scale, scale);
        nv::PaintOptions opts;
        opts.outline = QPen(INK, 1.5 / std::max(scale, 0.35));
        nv::paintNode(&p, attributes, QRectF(-box.width() / 2.0, -box.height() / 2.0,
            box.width(), box.height()), opts);
        return pm;
    }

    // ============================================================================
    // DialogBanner
    // ============================================================================

    DialogBanner::DialogBanner(Glyph glyph, const QString& title, const QString& subtitle, QWidget* parent)
        : QWidget(parent), glyph_(glyph), title_(title), subtitle_(subtitle)
    {
        setFixedHeight(86);
        setCursor(Qt::SizeAllCursor); // it is how the dialog is moved

        close_ = new QToolButton(this);
        close_->setText(QStringLiteral("✕"));
        close_->setToolTip(QStringLiteral("Cerrar (Esc)"));
        close_->setCursor(Qt::PointingHandCursor);
        close_->setFixedSize(30, 30);
        close_->setStyleSheet(QStringLiteral(
            "QToolButton { color: white; background: transparent; border: none; border-radius: 15px;"
            " font-size: 11pt; font-weight: 700; }"
            "QToolButton:hover { background: rgba(255, 255, 255, 45); }"
            "QToolButton:pressed { background: rgba(255, 255, 255, 75); }"));
        connect(close_, &QToolButton::clicked, this, &DialogBanner::closeRequested);
    }

    void DialogBanner::resizeEvent(QResizeEvent* event) {
        QWidget::resizeEvent(event);
        close_->move(width() - close_->width() - 12, 12);
    }

    void DialogBanner::mousePressEvent(QMouseEvent* event) {
        if (event->button() != Qt::LeftButton) return QWidget::mousePressEvent(event);
        dragging_ = true;
        drag_offset_ = event->globalPosition().toPoint() - window()->frameGeometry().topLeft();
        event->accept();
    }

    void DialogBanner::mouseMoveEvent(QMouseEvent* event) {
        if (!dragging_) return QWidget::mouseMoveEvent(event);
        window()->move(event->globalPosition().toPoint() - drag_offset_);
        event->accept();
    }

    void DialogBanner::mouseReleaseEvent(QMouseEvent* event) {
        dragging_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void DialogBanner::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect();

        // Only the top corners are rounded, following the dialog's card (whose
        // radius is one pixel larger, as the banner sits inside its border).
        constexpr double RADIUS = 13.0;
        QPainterPath top_rounded;
        top_rounded.addRoundedRect(r.adjusted(0, 0, 0, RADIUS), RADIUS, RADIUS);
        QPainterPath clip;
        clip.addRect(r);
        p.setClipPath(top_rounded.intersected(clip));

        QLinearGradient bg(r.topLeft(), r.bottomRight());
        bg.setColorAt(0.0, QColor("#4F46E5"));
        bg.setColorAt(0.55, QColor("#7C3AED"));
        bg.setColorAt(1.0, QColor("#C026D3"));
        p.fillRect(r, bg);

        // Decorative bubbles.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 22));
        p.drawEllipse(QPointF(r.right() - 60, r.top() + 10), 70, 70);
        p.setBrush(QColor(255, 255, 255, 14));
        p.drawEllipse(QPointF(r.right() - 170, r.bottom() + 20), 55, 55);

        // Glyph badge.
        const QPointF c(46, r.center().y());
        p.setBrush(QColor(255, 255, 255, 50));
        p.drawEllipse(c, 24, 24);
        p.setPen(QPen(Qt::white, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        switch (glyph_) {
        case Glyph::Create:
            p.drawLine(c + QPointF(-9, 0), c + QPointF(9, 0));
            p.drawLine(c + QPointF(0, -9), c + QPointF(0, 9));
            break;
        case Glyph::Edit: {
            p.save();
            p.translate(c);
            p.rotate(-45);
            p.drawRoundedRect(QRectF(-9, -3.5, 15, 7), 1.5, 1.5);
            QPainterPath tip;
            tip.moveTo(6, -3.5);
            tip.lineTo(11, 0);
            tip.lineTo(6, 3.5);
            p.drawPath(tip);
            p.restore();
            break;
        }
        case Glyph::Fuse:
            p.drawEllipse(c + QPointF(-5, 0), 8, 8);
            p.drawEllipse(c + QPointF(5, 0), 8, 8);
            break;
        }

        // Title + subtitle.
        QFont title_font = font();
        title_font.setPointSizeF(font().pointSizeF() * 1.55);
        title_font.setBold(true);
        p.setFont(title_font);
        p.setPen(Qt::white);
        const QRectF text_area(84, 16, r.width() - 130, 30); // clear of the close button
        p.drawText(text_area, Qt::AlignLeft | Qt::AlignVCenter, title_);

        QFont sub_font = font();
        sub_font.setPointSizeF(font().pointSizeF() * 0.95);
        p.setFont(sub_font);
        p.setPen(QColor(255, 255, 255, 215));
        p.drawText(QRectF(84, 46, r.width() - 130, 24), Qt::AlignLeft | Qt::AlignVCenter, subtitle_);
    }

    // ============================================================================
    // TickBox
    // ============================================================================

    TickBox::TickBox(const QString& text, const QIcon& icon, QWidget* parent)
        : QAbstractButton(parent)
    {
        setText(text);
        setIcon(icon);
        setIconSize(QSize(30, 18));
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        QFont f = font();
        f.setWeight(QFont::DemiBold);
        setFont(f);
    }

    QSize TickBox::sizeHint() const {
        const QFontMetrics fm(font());
        int w = 1 + BOX + GAP + fm.horizontalAdvance(text()) + 2;
        if (!icon().isNull()) w += iconSize().width() + 6;
        const int h = std::max({ BOX + 4, fm.height() + 4, icon().isNull() ? 0 : iconSize().height() + 4 });
        return QSize(w, h);
    }

    void TickBox::enterEvent(QEnterEvent* event) {
        hovered_ = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void TickBox::leaveEvent(QEvent* event) {
        hovered_ = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

    void TickBox::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool enabled = isEnabled();

        // Box: same border width and radius on every side.
        const QRectF box(1.5, (height() - BOX) / 2.0 + 0.5, BOX - 1.0, BOX - 1.0);
        QColor border = isChecked() ? ACCENT : (hovered_ || hasFocus() ? ACCENT : QColor("#B8BDCC"));
        if (!enabled) border = QColor("#D6DAE4");
        p.setPen(QPen(border, 1.5));
        p.setBrush(isChecked() ? (enabled ? ACCENT : QColor("#C7C9D9")) : QColor(Qt::white));
        p.drawRoundedRect(box, 5, 5);

        if (isChecked()) {
            QPainterPath tick;
            tick.moveTo(box.left() + box.width() * 0.25, box.top() + box.height() * 0.52);
            tick.lineTo(box.left() + box.width() * 0.43, box.top() + box.height() * 0.70);
            tick.lineTo(box.left() + box.width() * 0.76, box.top() + box.height() * 0.32);
            p.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(tick);
        }

        int x = BOX + GAP + 1;
        if (!icon().isNull()) {
            const QSize is = iconSize();
            icon().paint(&p, QRect(x, (height() - is.height()) / 2, is.width(), is.height()),
                Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
            x += is.width() + 6;
        }
        p.setPen(enabled ? QColor("#3A4050") : QColor("#A0A5B5"));
        p.drawText(QRect(x, 0, width() - x, height()), Qt::AlignLeft | Qt::AlignVCenter, text());
    }

    // ============================================================================
    // ColorPickerButton
    // ============================================================================

    ColorPickerButton::ColorPickerButton(QWidget* parent)
        : QToolButton(parent)
    {
        setObjectName("pickerButton");
        setPopupMode(QToolButton::InstantPopup);
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        setIconSize(QSize(30, 18));
        setMinimumWidth(180);
        setCursor(Qt::PointingHandCursor);

        menu_ = new QMenu(this);
        menu_->setObjectName("colourMenu");
        setMenu(menu_);
        connect(menu_, &QMenu::aboutToShow, this, &ColorPickerButton::rebuildMenu);

        refreshFace();
    }

    void ColorPickerButton::setColour(const QColor& colour, bool automatic) {
        colour_ = colour;
        automatic_ = automatic;
        refreshFace();
    }

    void ColorPickerButton::refreshFace() {
        setIcon(QIcon(swatchPixmap(colour_, iconSize(), devicePixelRatioF())));
        setText(automatic_ ? QStringLiteral("Automático") : hexOf(colour_));
        setToolTip(automatic_
            ? QStringLiteral("Automático (%1): negro o blanco según el fondo").arg(hexOf(colour_))
            : hexOf(colour_));
    }

    void ColorPickerButton::rebuildMenu() {
        menu_->clear();

        auto* panel = new QWidget;
        auto* v = new QVBoxLayout(panel);
        v->setContentsMargins(10, 10, 10, 8);
        v->setSpacing(6);

        const qreal dpr = devicePixelRatioF();

        auto choose = [this](const QColor& c) {
            menu_->close();
            setColour(c, false);
            emit colourPicked(c);
        };

        auto makeSwatch = [&](const QColor& c) {
            auto* b = new QToolButton;
            b->setFixedSize(20, 20);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(hexOf(c));
            const bool selected = !automatic_ && c.rgb() == colour_.rgb();
            b->setStyleSheet(QStringLiteral(
                "QToolButton { background: %1; border: %2; border-radius: 4px; }"
                "QToolButton:hover { border: 2px solid #6366F1; }")
                .arg(c.name(), selected ? "2px solid #1F2330" : "1px solid rgba(0,0,0,45)"));
            connect(b, &QToolButton::clicked, this, [choose, c] { choose(c); });
            return b;
        };

        auto addGrid = [&](const QList<QColor>& colours, int columns) {
            auto* grid = new QGridLayout;
            grid->setSpacing(4);
            for (int i = 0; i < colours.size(); ++i)
                grid->addWidget(makeSwatch(colours[i]), i / columns, i % columns);
            grid->setColumnStretch(columns, 1); // keep short rows packed to the left
            v->addLayout(grid);
        };

        auto addCaption = [&](const QString& text) {
            auto* l = new QLabel(text);
            l->setStyleSheet("color: #8A90A2; font-size: 8pt; font-weight: 700;");
            v->addWidget(l);
        };

        if (automatic_available_) {
            auto* automatic = new QToolButton;
            automatic->setObjectName("paletteWide");
            automatic->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            automatic->setIcon(QIcon(swatchPixmap(colour_, QSize(18, 18), dpr)));
            automatic->setText(QStringLiteral("Automático  —  negro o blanco según el fondo"));
            automatic->setCheckable(true);
            automatic->setChecked(automatic_);
            automatic->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            connect(automatic, &QToolButton::clicked, this, [this] {
                menu_->close();
                emit automaticPicked();
            });
            v->addWidget(automatic);
        }

        // Theme colours with tints (lighter) and shades (darker), as in Word.
        const QList<QColor> base = {
            QColor("#FFFFFF"), QColor("#000000"), QColor("#E7E6E6"), QColor("#44546A"), QColor("#4472C4"),
            QColor("#ED7D31"), QColor("#A5A5A5"), QColor("#FFC000"), QColor("#5B9BD5"), QColor("#70AD47") };
        QList<QColor> theme = base;
        for (int row = 0; row < 5; ++row) {
            for (const QColor& c : base) {
                const double l = c.lightnessF();
                if (l > 0.85) {
                    static const double darken[] = { 0.05, 0.15, 0.25, 0.35, 0.5 };
                    theme << mix(c, Qt::black, darken[row]);
                }
                else if (l < 0.1) {
                    static const double lighten[] = { 0.5, 0.35, 0.25, 0.15, 0.05 };
                    theme << mix(c, Qt::white, lighten[row]);
                }
                else {
                    static const double amount[] = { 0.8, 0.6, 0.4, 0.25, 0.5 };
                    theme << (row < 3 ? mix(c, Qt::white, amount[row]) : mix(c, Qt::black, amount[row]));
                }
            }
        }
        addCaption(QStringLiteral("Colores del tema"));
        addGrid(theme, 10);

        addCaption(QStringLiteral("Colores estándar"));
        addGrid({ QColor("#FFFFC8"), QColor("#C00000"), QColor("#FF0000"), QColor("#FFFF00"), QColor("#92D050"),
                  QColor("#00B050"), QColor("#00B0F0"), QColor("#0070C0"), QColor("#002060"), QColor("#7030A0") }, 10);

        addCaption(QStringLiteral("Usados en el proyecto"));
        if (recent_.isEmpty()) {
            auto* none = new QLabel(QStringLiteral("Aún no se ha elegido ningún color."));
            none->setStyleSheet("color: #A0A5B5; font-size: 8pt;");
            v->addWidget(none);
        }
        else {
            addGrid(recent_, 10);
        }

        auto* line = new QFrame;
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("color: #E3E6EF;");
        v->addWidget(line);

        auto* more = new QToolButton;
        more->setObjectName("paletteWide");
        more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        {
            // Little colour wheel as the icon.
            QPixmap pm(QSize(18, 18) * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            QConicalGradient wheel(9, 9, 0);
            for (int i = 0; i <= 6; ++i) wheel.setColorAt(i / 6.0, QColor::fromHsvF(std::fmod(i / 6.0, 1.0), 0.85, 1.0));
            p.setPen(Qt::NoPen);
            p.setBrush(wheel);
            p.drawEllipse(QRectF(1, 1, 16, 16));
            more->setIcon(QIcon(pm));
        }
        more->setText(QStringLiteral("Más colores…"));
        more->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(more, &QToolButton::clicked, this, [this] {
            menu_->close();
            QTimer::singleShot(0, this, &ColorPickerButton::openMoreColours);
        });
        v->addWidget(more);

        auto* action = new QWidgetAction(menu_);
        action->setDefaultWidget(panel);
        menu_->addAction(action);
    }

    void ColorPickerButton::openMoreColours() {
        for (int i = 0; i < recent_.size() && i < QColorDialog::customCount(); ++i)
            QColorDialog::setCustomColor(i, recent_[i]);

        const QColor chosen = QColorDialog::getColor(colour_, window(), QStringLiteral("Más colores"),
            QColorDialog::DontUseNativeDialog);
        if (!chosen.isValid()) return;
        setColour(chosen, false);
        emit colourPicked(chosen);
    }

    // ============================================================================
    // FontSizeSelector
    // ============================================================================

    namespace {
        const QList<int>& fontPresets() {
            static const QList<int> presets = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72 };
            return presets;
        }

        // "A" with a small up/down triangle, like Word's grow/shrink font buttons.
        QIcon sizeStepIcon(bool grow, qreal dpr) {
            const QSize size(22, 20);
            QPixmap pm(size * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            QFont f;
            f.setBold(true);
            f.setPixelSize(grow ? 15 : 12);
            p.setFont(f);
            p.setPen(INK);
            p.drawText(QRectF(0, 0, 14, 20), Qt::AlignCenter, QStringLiteral("A"));
            QPainterPath tri;
            if (grow) { tri.moveTo(15, 8); tri.lineTo(21, 8); tri.lineTo(18, 3); }
            else      { tri.moveTo(15, 4); tri.lineTo(21, 4); tri.lineTo(18, 9); }
            tri.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(ACCENT);
            p.drawPath(tri);
            return QIcon(pm);
        }
    } // namespace

    FontSizeSelector::FontSizeSelector(QWidget* parent)
        : QWidget(parent)
    {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);

        combo_ = new QComboBox(this);
        combo_->setEditable(true);
        combo_->setInsertPolicy(QComboBox::NoInsert);
        for (int s : fontPresets()) combo_->addItem(QString::number(s));
        combo_->setValidator(new QIntValidator(MIN_SIZE, MAX_SIZE, combo_));
        combo_->setFixedWidth(84);
        combo_->setToolTip(QStringLiteral("Tamaño de fuente (pt)"));

        grow_ = new QToolButton(this);
        grow_->setObjectName("sizeButton");
        grow_->setIcon(sizeStepIcon(true, devicePixelRatioF()));
        grow_->setIconSize(QSize(22, 20));
        grow_->setToolTip(QStringLiteral("Aumentar tamaño de fuente"));
        grow_->setCursor(Qt::PointingHandCursor);

        shrink_ = new QToolButton(this);
        shrink_->setObjectName("sizeButton");
        shrink_->setIcon(sizeStepIcon(false, devicePixelRatioF()));
        shrink_->setIconSize(QSize(22, 20));
        shrink_->setToolTip(QStringLiteral("Disminuir tamaño de fuente"));
        shrink_->setCursor(Qt::PointingHandCursor);

        auto* unit = new QLabel(QStringLiteral("pt"), this);
        unit->setObjectName("hint");

        h->addWidget(combo_);
        h->addWidget(unit);
        h->addSpacing(4);
        h->addWidget(grow_);
        h->addWidget(shrink_);
        h->addStretch();

        // Live update while typing, as long as the text is a valid size.
        connect(combo_, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            bool ok = false;
            const int v = text.toInt(&ok);
            if (!ok || v < MIN_SIZE || v > MAX_SIZE || v == value_) return;
            value_ = v;
            emit valueChanged(value_);
        });
        connect(combo_->lineEdit(), &QLineEdit::editingFinished, this, &FontSizeSelector::commitText);
        connect(grow_, &QToolButton::clicked, this, [this] { step(true); });
        connect(shrink_, &QToolButton::clicked, this, [this] { step(false); });

        setValue(value_);
    }

    void FontSizeSelector::setValue(int size) {
        size = std::clamp(size, MIN_SIZE, MAX_SIZE);
        const bool changed = size != value_;
        value_ = size;
        const QSignalBlocker block(combo_);
        combo_->setCurrentText(QString::number(size));
        if (changed) emit valueChanged(value_);
    }

    void FontSizeSelector::commitText() {
        // Restore the last valid value if the text was left empty or invalid.
        bool ok = false;
        const int v = combo_->currentText().toInt(&ok);
        if (!ok || v < MIN_SIZE || v > MAX_SIZE) {
            const QSignalBlocker block(combo_);
            combo_->setCurrentText(QString::number(value_));
        }
    }

    void FontSizeSelector::step(bool grow) {
        const auto& presets = fontPresets();
        int next = value_;
        if (grow) {
            auto it = std::upper_bound(presets.begin(), presets.end(), value_);
            next = (it != presets.end()) ? *it : value_ + 10;
        }
        else {
            auto it = std::lower_bound(presets.begin(), presets.end(), value_);
            if (it != presets.begin()) next = *std::prev(it);
            else                       next = value_ - 1;
        }
        setValue(next);
    }

    // ============================================================================
    // NodePreview
    // ============================================================================

    NodePreview::NodePreview(QWidget* parent)
        : QWidget(parent)
    {
        setMinimumSize(300, 140);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    void NodePreview::setAttributes(const NodeAttributes& attributes) {
        attributes_ = attributes;
        update();
    }

    void NodePreview::paintEvent(QPaintEvent*) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Dotted "canvas" card.
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

        // The node, scaled to fit (up to 1.6x so small nodes read well).
        const QSizeF box = nv::boxSize(attributes_);
        const double scale = std::min({ 1.6,
            (card.width() - 40.0) / box.width(),
            (card.height() - 36.0) / box.height() });
        const QRectF node_box(-box.width() / 2.0, -box.height() / 2.0, box.width(), box.height());

        p.translate(card.center());
        p.scale(scale, scale);

        // Soft drop shadow.
        p.save();
        p.translate(2.5, 3.5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(31, 35, 48, 28));
        p.drawPath(nv::shapePath(attributes_.shape, node_box));
        p.restore();

        nv::paintNode(&p, attributes_, node_box);
    }

    // ============================================================================
    // NodeAttributesForm
    // ============================================================================

    NodeAttributesForm::NodeAttributesForm(const NodeAttributes& initial,
        const QList<QColor>& recent_colours, QWidget* parent)
        : QWidget(parent)
    {
        build(recent_colours);
        setAttributes(initial);
    }

    NodeAttributesForm::NodeAttributesForm(const NodeAttributes& initial,
        const QList<QColor>& recent_colours,
        const NodeAttributes& left, const QString& left_title,
        const NodeAttributes& right, const QString& right_title,
        QWidget* parent)
        : QWidget(parent)
        , left_(left)
        , right_(right)
        , left_title_(left_title)
        , right_title_(right_title)
    {
        build(recent_colours);
        setAttributes(initial);
    }

    NodeAttributes NodeAttributesForm::attributes() const {
        NodeAttributes a = current_;
        a.name = name_->text().trimmed().toStdString();
        return a;
    }

    void NodeAttributesForm::setAttributes(const NodeAttributes& attributes) {
        current_ = attributes;
        if (current_.isFire()) {
            // Colours before the fire are unknown: fall back to the defaults.
            const NodeAttributes defaults;
            pre_fire_colour_ = defaults.colour;
            pre_fire_font_colour_ = defaults.font_colour;
            pre_fire_font_auto_ = true;
            font_auto_ = true;
        }
        else {
            font_auto_ = nv::toQColor(current_.font_colour).rgb()
                == nv::contrastingTextColour(nv::toQColor(current_.colour)).rgb();
        }
        sync();
        emit attributesChanged();
    }

    void NodeAttributesForm::focusName() {
        name_->setFocus();
        name_->selectAll();
    }

    // ── Building ──────────────────────────────────────────────────────────────────

    void NodeAttributesForm::build(const QList<QColor>& recent_colours) {
        const bool merge = left_.has_value();

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);

        auto* card = new QFrame(this);
        card->setObjectName("card");
        outer->addWidget(card);

        auto* grid = new QGridLayout(card);
        grid->setContentsMargins(20, 14, 20, 18);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(8);

        // Column layout: normal = [label][editor]; merge = [left][→][label][editor][←][right].
        const int label_col = merge ? 2 : 0;
        const int editor_col = merge ? 3 : 1;
        const int columns = merge ? 6 : 2;
        grid->setColumnStretch(editor_col, 1);
        if (merge) {
            grid->setColumnStretch(0, 1);
            grid->setColumnStretch(5, 1);
        }

        int row = 0;
        if (merge) {
            grid->addWidget(createSideHeader(*left_, left_title_, true), row, 0, 1, 2);
            auto* result_title = new QLabel(QStringLiteral("Resultado"));
            result_title->setObjectName("columnTitle");
            result_title->setAlignment(Qt::AlignCenter);
            auto* result_hint = new QLabel(QStringLiteral("Toma cada campo de un lado o edítalo a mano"));
            result_hint->setObjectName("hint");
            result_hint->setAlignment(Qt::AlignCenter);
            auto* result_box = new QVBoxLayout;
            result_box->addStretch();
            result_box->addWidget(result_title);
            result_box->addWidget(result_hint);
            result_box->addStretch();
            grid->addLayout(result_box, row, 2, 1, 2);
            grid->addWidget(createSideHeader(*right_, right_title_, false), row, 4, 1, 2);
            ++row;
        }

        auto addSection = [&](const QString& title) {
            grid->addWidget(sectionTitle(title), row++, 0, 1, columns);
        };

        auto addField = [&](Field field, const QString& label) {
            if (!label.isEmpty())
                grid->addWidget(fieldLabel(label), row, label_col, Qt::AlignRight | Qt::AlignVCenter);
            grid->addWidget(createEditor(field, recent_colours), row, editor_col);
            if (merge) {
                MergeRow& mr = merge_rows_[field];
                mr.left_chip = createChip(field, *left_);
                mr.right_chip = createChip(field, *right_);

                mr.take_left = new QToolButton;
                mr.take_left->setObjectName("takeButton");
                mr.take_left->setText(QStringLiteral("→"));
                mr.take_left->setToolTip(QStringLiteral("Usar el valor de %1").arg(left_title_));
                mr.take_left->setCursor(Qt::PointingHandCursor);
                connect(mr.take_left, &QToolButton::clicked, this, [this, field] { takeField(field, *left_); });

                mr.take_right = new QToolButton;
                mr.take_right->setObjectName("takeButton");
                mr.take_right->setText(QStringLiteral("←"));
                mr.take_right->setToolTip(QStringLiteral("Usar el valor de %1").arg(right_title_));
                mr.take_right->setCursor(Qt::PointingHandCursor);
                connect(mr.take_right, &QToolButton::clicked, this, [this, field] { takeField(field, *right_); });

                grid->addWidget(mr.left_chip, row, 0, Qt::AlignVCenter);
                grid->addWidget(mr.take_left, row, 1, Qt::AlignCenter);
                grid->addWidget(mr.take_right, row, 4, Qt::AlignCenter);
                grid->addWidget(mr.right_chip, row, 5, Qt::AlignVCenter);
            }
            ++row;
        };

        addSection(QStringLiteral("Identidad"));
        addField(Name, QStringLiteral("Nombre"));
        addField(Shape, QStringLiteral("Forma"));
        addSection(QStringLiteral("Apariencia"));
        addField(Colour, QStringLiteral("Color de fondo"));
        addField(FontColour, QStringLiteral("Color del texto"));
        addField(FontSize, QStringLiteral("Tamaño de fuente"));
        addSection(QStringLiteral("Estado"));
        addField(Fire, QString()); // the tick boxes already say it all
    }

    QWidget* NodeAttributesForm::createEditor(Field field, const QList<QColor>& recent_colours) {
        const qreal dpr = devicePixelRatioF();
        switch (field) {
        case Name: {
            name_ = new QLineEdit;
            name_->setPlaceholderText(QStringLiteral("Nombre de la caja"));
            name_->setClearButtonEnabled(true);
            name_->setMinimumWidth(220);
            connect(name_, &QLineEdit::textEdited, this, [this](const QString& text) {
                current_.name = text.toStdString();
                sync();
                emit attributesChanged();
            });
            return name_;
        }
        case Shape: {
            shape_ = new QListWidget;
            shape_->setObjectName("shapeList");
            shape_->setViewMode(QListView::IconMode);
            shape_->setFlow(QListView::LeftToRight);
            shape_->setWrapping(false);
            shape_->setMovement(QListView::Static);
            shape_->setResizeMode(QListView::Adjust);
            shape_->setIconSize(QSize(46, 34));
            shape_->setGridSize(QSize(98, 72));
            shape_->setFixedHeight(80);
            shape_->setMinimumWidth(3 * 98 + 8);
            shape_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            shape_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            shape_->setSelectionMode(QAbstractItemView::SingleSelection);
            shape_->setCursor(Qt::PointingHandCursor);
            for (NodeShape s : { NodeShape::Rectangle, NodeShape::Circle, NodeShape::Rhombus }) {
                auto* item = new QListWidgetItem(
                    QIcon(renderNodeIcon(iconAttributes(s), QSize(46, 34), dpr)), shapeName(s));
                item->setData(Qt::UserRole, static_cast<int>(s));
                item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
                item->setSizeHint(QSize(92, 66));
                shape_->addItem(item);
            }
            connect(shape_, &QListWidget::currentRowChanged, this, [this](int r) {
                if (syncing_ || r < 0) return;
                current_.shape = static_cast<NodeShape>(shape_->item(r)->data(Qt::UserRole).toInt());
                sync();
                emit attributesChanged();
            });
            return shape_;
        }
        case Colour: {
            colour_ = new ColorPickerButton;
            colour_->setRecentColours(recent_colours);
            connect(colour_, &ColorPickerButton::colourPicked, this, [this](const QColor& c) {
                rememberPicked(c);
                setBackground(c);
            });
            auto* wrap = new QWidget;
            auto* h = new QHBoxLayout(wrap);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(colour_);
            h->addStretch();
            return wrap;
        }
        case FontColour: {
            font_colour_ = new ColorPickerButton;
            font_colour_->setRecentColours(recent_colours);
            font_colour_->setAutomaticAvailable(true);
            connect(font_colour_, &ColorPickerButton::colourPicked, this, [this](const QColor& c) {
                rememberPicked(c);
                setFontColourManual(c);
            });
            connect(font_colour_, &ColorPickerButton::automaticPicked, this, [this] { setFontColourAutomatic(); });
            auto* wrap = new QWidget;
            auto* h = new QHBoxLayout(wrap);
            h->setContentsMargins(0, 0, 0, 0);
            h->addWidget(font_colour_);
            h->addStretch();
            return wrap;
        }
        case FontSize: {
            font_size_ = new FontSizeSelector;
            connect(font_size_, &FontSizeSelector::valueChanged, this, [this](int v) {
                if (syncing_) return;
                current_.font_size = v;
                sync();
                emit attributesChanged();
            });
            return font_size_;
        }
        case Fire: {
            auto* wrap = new QWidget;
            auto* v = new QVBoxLayout(wrap);
            v->setContentsMargins(0, 0, 0, 0);
            v->setSpacing(4);
            auto* h = new QHBoxLayout;
            h->setSpacing(18);

            fire_ = new TickBox(QStringLiteral("Fuego"),
                QIcon(renderNodeIcon(iconAttributes(NodeShape::Rectangle, FireState::Fire), QSize(30, 18), dpr)));
            ashes_ = new TickBox(QStringLiteral("Fuego con cenizas"),
                QIcon(renderNodeIcon(iconAttributes(NodeShape::Rectangle, FireState::FireWithAshes), QSize(30, 18), dpr)));

            // Mutually exclusive, but both may be unticked.
            connect(fire_, &TickBox::toggled, this, [this](bool on) {
                if (syncing_) return;
                setFireState(on ? FireState::Fire
                    : (ashes_->isChecked() ? FireState::FireWithAshes : FireState::None));
            });
            connect(ashes_, &TickBox::toggled, this, [this](bool on) {
                if (syncing_) return;
                setFireState(on ? FireState::FireWithAshes
                    : (fire_->isChecked() ? FireState::Fire : FireState::None));
            });

            h->addWidget(fire_);
            h->addWidget(ashes_);
            h->addStretch();
            v->addLayout(h);
            return wrap;
        }
        default:
            return new QWidget;
        }
    }

    QFrame* NodeAttributesForm::createChip(Field field, const NodeAttributes& side) {
        auto* chip = new QFrame;
        chip->setObjectName("chip");
        chip->setMinimumWidth(150);
        chip->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* h = new QHBoxLayout(chip);
        h->setContentsMargins(8, 5, 8, 5);
        h->setSpacing(6);
        const qreal dpr = devicePixelRatioF();

        auto addIcon = [&](const QPixmap& pm) {
            auto* icon = new QLabel;
            icon->setPixmap(pm);
            h->addWidget(icon);
        };
        auto addText = [&](const QString& text) {
            auto* label = new QLabel;
            label->setText(label->fontMetrics().elidedText(text, Qt::ElideRight, 150));
            label->setToolTip(text);
            h->addWidget(label, 1);
        };

        switch (field) {
        case Name:
            addText(QString::fromStdString(side.name));
            break;
        case Shape:
            addIcon(renderNodeIcon(iconAttributes(side.shape), QSize(26, 18), dpr));
            addText(shapeName(side.shape));
            break;
        case Colour:
            addIcon(swatchPixmap(nv::toQColor(side.colour), QSize(22, 16), dpr));
            addText(hexOf(nv::toQColor(side.colour)));
            break;
        case FontColour:
            addIcon(swatchPixmap(nv::toQColor(side.font_colour), QSize(22, 16), dpr));
            addText(hexOf(nv::toQColor(side.font_colour)));
            break;
        case FontSize:
            addText(QStringLiteral("%1 pt").arg(side.font_size));
            break;
        case Fire:
            if (side.isFire())
                addIcon(renderNodeIcon(iconAttributes(NodeShape::Rectangle, side.fire), QSize(26, 16), dpr));
            addText(fireName(side.fire));
            break;
        default:
            break;
        }
        return chip;
    }

    QWidget* NodeAttributesForm::createSideHeader(const NodeAttributes& side, const QString& title, bool left) {
        auto* frame = new QFrame;
        frame->setObjectName("sideHeader");
        auto* v = new QVBoxLayout(frame);
        v->setContentsMargins(10, 8, 10, 10);
        v->setSpacing(6);

        // No title: the node's name is already visible inside its preview.
        auto* preview = new QLabel;
        preview->setPixmap(renderNodeIcon(side, QSize(150, 70), devicePixelRatioF()));
        preview->setAlignment(Qt::AlignCenter);
        v->addWidget(preview);

        auto* take_all = new QPushButton(left ? QStringLiteral("Conservar todo  →") : QStringLiteral("←  Conservar todo"));
        take_all->setObjectName("ghost");
        take_all->setCursor(Qt::PointingHandCursor);
        take_all->setToolTip(QStringLiteral("Usar todos los valores de %1").arg(title));
        connect(take_all, &QPushButton::clicked, this, [this, left] { takeAll(left ? *left_ : *right_); });
        v->addWidget(take_all, 0, Qt::AlignCenter);
        return frame;
    }

    // ── Mutations ─────────────────────────────────────────────────────────────────

    void NodeAttributesForm::rememberPicked(const QColor& colour) {
        picked_colours_.removeAll(colour);
        picked_colours_.prepend(colour);
    }

    void NodeAttributesForm::setBackground(const QColor& colour) {
        if (current_.isFire()) return; // locked by the fire state
        current_.colour = nv::fromQColor(colour);
        if (font_auto_)
            current_.font_colour = nv::fromQColor(nv::contrastingTextColour(colour));
        sync();
        emit attributesChanged();
    }

    void NodeAttributesForm::setFontColourManual(const QColor& colour) {
        if (current_.isFire()) return;
        font_auto_ = false;
        current_.font_colour = nv::fromQColor(colour);
        sync();
        emit attributesChanged();
    }

    void NodeAttributesForm::setFontColourAutomatic() {
        if (current_.isFire()) return;
        font_auto_ = true;
        current_.font_colour = nv::fromQColor(nv::contrastingTextColour(nv::toQColor(current_.colour)));
        sync();
        emit attributesChanged();
    }

    void NodeAttributesForm::setFireState(FireState fire) {
        if (current_.fire == fire) {
            sync();
            return;
        }
        if (!current_.isFire() && fire != FireState::None) {
            // Entering a fire state: remember the colours, then force white/black.
            pre_fire_colour_ = current_.colour;
            pre_fire_font_colour_ = current_.font_colour;
            pre_fire_font_auto_ = font_auto_;
            current_.colour = { 255, 255, 255, 255 };
            current_.font_colour = { 0, 0, 0, 255 };
        }
        else if (current_.isFire() && fire == FireState::None) {
            // Leaving the fire state: give the user back their colours.
            current_.colour = pre_fire_colour_;
            current_.font_colour = pre_fire_font_colour_;
            font_auto_ = pre_fire_font_auto_;
        }
        current_.fire = fire;
        sync();
        emit attributesChanged();
    }

    void NodeAttributesForm::takeField(Field field, const NodeAttributes& side) {
        switch (field) {
        case Name:
            current_.name = side.name;
            break;
        case Shape:
            current_.shape = side.shape;
            break;
        case Colour:
            setBackground(nv::toQColor(side.colour));
            return;
        case FontColour: {
            if (current_.isFire()) return;
            const QColor c = nv::toQColor(side.font_colour);
            font_auto_ = c.rgb() == nv::contrastingTextColour(nv::toQColor(current_.colour)).rgb();
            current_.font_colour = side.font_colour;
            break;
        }
        case FontSize:
            current_.font_size = side.font_size;
            break;
        case Fire:
            setFireState(side.fire);
            return;
        default:
            break;
        }
        sync();
        emit attributesChanged();
    }

    void NodeAttributesForm::takeAll(const NodeAttributes& side) {
        setAttributes(side);
    }

    // ── Sync ──────────────────────────────────────────────────────────────────────

    bool NodeAttributesForm::sameField(Field field, const NodeAttributes& a, const NodeAttributes& b) {
        switch (field) {
        case Name:       return a.name == b.name;
        case Shape:      return a.shape == b.shape;
        case Colour:     return a.colour == b.colour;
        case FontColour: return a.font_colour == b.font_colour;
        case FontSize:   return a.font_size == b.font_size;
        case Fire:       return a.fire == b.fire;
        default:         return true;
        }
    }

    void NodeAttributesForm::sync() {
        syncing_ = true;

        const QString name = QString::fromStdString(current_.name);
        if (name_->text() != name) name_->setText(name);

        for (int r = 0; r < shape_->count(); ++r) {
            if (shape_->item(r)->data(Qt::UserRole).toInt() == static_cast<int>(current_.shape)) {
                shape_->setCurrentRow(r);
                break;
            }
        }

        const bool fire = current_.isFire();
        colour_->setColour(nv::toQColor(current_.colour), false);
        font_colour_->setColour(nv::toQColor(current_.font_colour), font_auto_ && !fire);
        colour_->setEnabled(!fire);
        font_colour_->setEnabled(!fire);
        const QString locked = QStringLiteral("Bloqueado mientras la caja sea un fuego");
        if (fire) {
            colour_->setToolTip(locked);
            font_colour_->setToolTip(locked);
        }

        font_size_->setValue(current_.font_size);
        fire_->setChecked(current_.fire == FireState::Fire);
        ashes_->setChecked(current_.fire == FireState::FireWithAshes);

        if (left_ && right_) {
            for (int f = 0; f < FieldCount; ++f) {
                MergeRow& mr = merge_rows_[f];
                if (!mr.take_left) continue;
                const Field field = static_cast<Field>(f);
                const bool from_left = sameField(field, current_, *left_);
                const bool from_right = sameField(field, current_, *right_);
                const bool conflict = !sameField(field, *left_, *right_);
                const bool colour_locked = fire && (field == Colour || field == FontColour);

                mr.take_left->setProperty("active", from_left);
                mr.take_right->setProperty("active", from_right);
                mr.take_left->setEnabled(!from_left && !colour_locked);
                mr.take_right->setEnabled(!from_right && !colour_locked);
                mr.left_chip->setProperty("conflict", conflict);
                mr.right_chip->setProperty("conflict", conflict);
                for (QWidget* w : { static_cast<QWidget*>(mr.take_left), static_cast<QWidget*>(mr.take_right),
                                    static_cast<QWidget*>(mr.left_chip), static_cast<QWidget*>(mr.right_chip) })
                    dialog_theme::repolish(w);
            }
        }

        syncing_ = false;
    }

} // namespace ui
