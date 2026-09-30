#include "AppDialogs.h"
#include "UiStyle.h"

#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {

    namespace {

        // Round (or, for the app itself, rounded-square) emblem on the left.
        class BadgeWidget : public QWidget {
        public:
            BadgeWidget(StyledDialog::Badge badge, QWidget* parent)
                : QWidget(parent), badge_(badge)
            {
                setFixedSize(48, 48);
            }

        protected:
            void paintEvent(QPaintEvent*) override {
                using Badge = StyledDialog::Badge;
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                const QPointF c(24, 24);

                QColor halo, core;
                switch (badge_) {
                case Badge::Error:   halo = QColor(0xFD, 0xEC, 0xEC); core = style::palette::danger; break;
                case Badge::Warning: halo = QColor(0xFE, 0xF3, 0xC7); core = style::palette::amber; break;
                case Badge::Success: halo = QColor(0xDC, 0xF5, 0xEA); core = style::palette::green; break;
                default:             halo = style::palette::accent_soft; core = style::palette::accent; break;
                }

                if (badge_ == Badge::App) {
                    // The application's emblem: a tiny diagram on the brand gradient.
                    QLinearGradient g(0, 0, 48, 48);
                    g.setColorAt(0, style::palette::accent);
                    g.setColorAt(1, style::palette::violet);
                    p.setPen(Qt::NoPen);
                    p.setBrush(g);
                    p.drawRoundedRect(QRectF(2, 2, 44, 44), 12, 12);
                    p.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.drawLine(QPointF(17, 18), QPointF(17, 24));
                    p.drawLine(QPointF(31, 18), QPointF(31, 24));
                    p.drawLine(QPointF(17, 24), QPointF(31, 24));
                    p.drawLine(QPointF(24, 24), QPointF(24, 30));
                    p.setBrush(Qt::white);
                    p.setPen(Qt::NoPen);
                    p.drawRoundedRect(QRectF(11, 11, 12, 7), 2, 2);
                    p.drawRoundedRect(QRectF(25, 11, 12, 7), 2, 2);
                    p.drawRoundedRect(QRectF(18, 30, 12, 7), 2, 2);
                    return;
                }

                p.setPen(Qt::NoPen);
                p.setBrush(halo);
                p.drawEllipse(c, 24, 24);

                if (badge_ == Badge::Joint) {
                    style::icon(style::Icon::Joint).paint(&p, QRect(11, 11, 26, 26));
                    return;
                }

                QLinearGradient g(c - QPointF(15, 15), c + QPointF(15, 15));
                g.setColorAt(0, core.lighter(112));
                g.setColorAt(1, core);
                p.setBrush(g);
                p.drawEllipse(c, 15, 15);

                QPen white(Qt::white, 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
                p.setPen(white);
                p.setBrush(Qt::NoBrush);
                switch (badge_) {
                case Badge::Error:
                    p.drawLine(c + QPointF(-5, -5), c + QPointF(5, 5));
                    p.drawLine(c + QPointF(5, -5), c + QPointF(-5, 5));
                    break;
                case Badge::Warning:
                    p.drawLine(c + QPointF(0, -7), c + QPointF(0, 2));
                    p.setBrush(Qt::white);
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(c + QPointF(0, 6.5), 1.7, 1.7);
                    break;
                case Badge::Success: {
                    QPainterPath tick;
                    tick.moveTo(c + QPointF(-6, 0.5));
                    tick.lineTo(c + QPointF(-1.5, 5));
                    tick.lineTo(c + QPointF(6.5, -4.5));
                    p.drawPath(tick);
                    break;
                }
                case Badge::Info:
                    p.drawLine(c + QPointF(0, -1), c + QPointF(0, 7));
                    p.setBrush(Qt::white);
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(c + QPointF(0, -6), 1.7, 1.7);
                    break;
                case Badge::Question: {
                    QFont f = font();
                    f.setPixelSize(19);
                    f.setBold(true);
                    p.setFont(f);
                    p.drawText(QRectF(c.x() - 15, c.y() - 15, 30, 30), Qt::AlignCenter, QStringLiteral("?"));
                    break;
                }
                default:
                    break;
                }
            }

        private:
            StyledDialog::Badge badge_;
        };

        QString dialogStyleSheet() {
            return QStringLiteral(R"(
QFrame#dialogCard { background: white; border: 1px solid #E3E6EF; border-radius: 16px; }
QLabel#dialogTitle { color: #1F2330; font-size: 12pt; font-weight: 700; }
QLabel#dialogMessage { color: #4B5068; font-size: 10pt; }
QPushButton { outline: none; }
QPushButton#primary {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366F1, stop:1 #8B5CF6);
    color: white; border: none; border-radius: 9px; padding: 8px 22px; font-weight: 700; min-width: 70px;
}
QPushButton#primary:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #575AE8, stop:1 #7E4FEF);
}
QPushButton#primary:pressed { background: #4F46E5; }
QPushButton#danger {
    background: #E5484D; color: white; border: none; border-radius: 9px; padding: 8px 22px;
    font-weight: 700; min-width: 70px;
}
QPushButton#danger:hover { background: #D93D42; }
QPushButton#danger:pressed { background: #C4353A; }
QPushButton#secondary {
    background: white; color: #3A4050; border: 1px solid #D6DAE4; border-radius: 9px;
    padding: 8px 18px; font-weight: 600; min-width: 70px;
}
QPushButton#secondary:hover { background: #F1F2F7; }
)");
        }

    } // namespace

    // ============================================================================
    // StyledDialog
    // ============================================================================

    StyledDialog::StyledDialog(Badge badge, const QString& title, QWidget* parent)
        : QDialog(parent)
    {
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowTitle(title);
        setModal(true);
        setStyleSheet(dialogStyleSheet());

        // Room around the card for its shadow. The layout fixes the dialog's
        // size from the start (it is never resized by hand), so the window is
        // created at its real size instead of a provisional one that Windows
        // would then have to enlarge to the minimum.
        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(22, 18, 22, 26);
        outer->setSizeConstraint(QLayout::SetFixedSize);

        auto* card = new QFrame(this);
        card->setObjectName("dialogCard");
        auto* shadow = new QGraphicsDropShadowEffect(card);
        shadow->setBlurRadius(34);
        shadow->setOffset(0, 8);
        shadow->setColor(QColor(31, 35, 48, 70));
        card->setGraphicsEffect(shadow);
        outer->addWidget(card);

        auto* column = new QVBoxLayout(card);
        column->setContentsMargins(24, 22, 22, 18);
        column->setSpacing(18);

        auto* top = new QHBoxLayout;
        top_ = top;
        top->setSpacing(16);
        emblem_ = new BadgeWidget(badge, card);
        top->addWidget(emblem_, 0, Qt::AlignTop);

        text_column_ = new QVBoxLayout;
        text_column_->setSpacing(6);
        auto* t = new QLabel(title, card);
        t->setObjectName("dialogTitle");
        t->setWordWrap(true);
        t->setMinimumWidth(280);
        t->setMaximumWidth(440);
        text_column_->addWidget(t);
        top->addLayout(text_column_, 1);
        column->addLayout(top);

        // In a widget of its own, hidden until a button is added, so a dialog
        // without buttons does not keep an empty row (and its spacing).
        buttons_host_ = new QWidget(card);
        buttons_ = new QHBoxLayout(buttons_host_);
        buttons_->setContentsMargins(0, 0, 0, 0);
        buttons_->setSpacing(8);
        buttons_->addStretch();
        buttons_host_->hide();
        column->addWidget(buttons_host_);
    }

    void StyledDialog::setEmblem(QWidget* emblem) {
        emblem->setParent(emblem_->parentWidget());
        top_->replaceWidget(emblem_, emblem);
        delete emblem_;
        emblem_ = emblem;
    }

    QPushButton* StyledDialog::makeButton(const QString& text, ButtonStyle style) {
        auto* b = new QPushButton(text, buttons_host_);
        b->setObjectName(style == ButtonStyle::Primary ? "primary"
            : style == ButtonStyle::Danger ? "danger" : "secondary");
        b->setCursor(Qt::PointingHandCursor);
        b->setAutoDefault(false);
        buttons_->addWidget(b);
        buttons_host_->show();
        return b;
    }

    QPushButton* StyledDialog::addActionButton(const QString& text, ButtonStyle style) {
        return makeButton(text, style);
    }

    void StyledDialog::setMessage(const QString& text) {
        if (!message_) {
            message_ = new QLabel(this);
            message_->setObjectName("dialogMessage");
            message_->setWordWrap(true);
            message_->setMaximumWidth(440);
            message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
            text_column_->addWidget(message_);
        }
        message_->setText(text);
    }

    void StyledDialog::setBody(QWidget* body) {
        body->setParent(this);
        text_column_->addSpacing(4);
        text_column_->addWidget(body);
    }

    QPushButton* StyledDialog::addButton(const QString& text, int result, ButtonStyle style,
        bool is_default, bool is_escape)
    {
        auto* b = makeButton(text, style);
        if (is_default) {
            b->setDefault(true);
            b->setFocus();
        }
        if (is_escape) escape_result_ = result;
        connect(b, &QPushButton::clicked, this, [this, result] {
            choice_ = result;
            accept();
        });
        return b;
    }

    void StyledDialog::setVisible(bool visible) {
        if (visible && !isVisible()) {
            // Settle the final size and position *before* the native window is
            // shown. Otherwise Windows first gets a provisional size smaller
            // than the dialog's fixed one (and later a centring move whose
            // rounding at fractional screen scales asks for 1px more), and Qt
            // logs "Unable to set geometry" as Windows corrects it.
            if (layout()) layout()->activate();
            resize(sizeHint());
            if (QWidget* host = parentWidget() ? parentWidget()->window() : nullptr)
                move(host->geometry().center() - rect().center());
        }
        QDialog::setVisible(visible);
    }

    void StyledDialog::keyPressEvent(QKeyEvent* event) {
        if (event->key() == Qt::Key_Escape) {
            choice_ = escape_result_;
            reject();
            return;
        }
        QDialog::keyPressEvent(event);
    }

    void StyledDialog::mousePressEvent(QMouseEvent* event) {
        if (event->button() == Qt::LeftButton) {
            dragging_ = true;
            drag_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
        QDialog::mousePressEvent(event);
    }

    void StyledDialog::mouseMoveEvent(QMouseEvent* event) {
        if (dragging_) move(event->globalPosition().toPoint() - drag_offset_);
        QDialog::mouseMoveEvent(event);
    }

    void StyledDialog::mouseReleaseEvent(QMouseEvent* event) {
        dragging_ = false;
        QDialog::mouseReleaseEvent(event);
    }

    // ============================================================================
    // Ready-made dialogs
    // ============================================================================

    namespace dialogs {

        namespace {
            void showSimple(QWidget* parent, StyledDialog::Badge badge, const QString& title, const QString& text) {
                StyledDialog dlg(badge, title, parent);
                dlg.setMessage(text);
                dlg.addButton(QStringLiteral("Entendido"), 0, StyledDialog::ButtonStyle::Primary, true, true);
                dlg.exec();
            }
        }

        void showError(QWidget* parent, const QString& title, const QString& text) {
            showSimple(parent, StyledDialog::Badge::Error, title, text);
        }

        void showWarning(QWidget* parent, const QString& title, const QString& text) {
            showSimple(parent, StyledDialog::Badge::Warning, title, text);
        }

        void showInfo(QWidget* parent, const QString& title, const QString& text) {
            showSimple(parent, StyledDialog::Badge::Info, title, text);
        }

        bool confirm(QWidget* parent, const QString& title, const QString& text,
            const QString& accept_text, bool destructive)
        {
            StyledDialog dlg(destructive ? StyledDialog::Badge::Warning : StyledDialog::Badge::Question,
                title, parent);
            dlg.setMessage(text);
            dlg.addButton(QStringLiteral("Cancelar"), 0, StyledDialog::ButtonStyle::Secondary, false, true);
            dlg.addButton(accept_text, 1, destructive ? StyledDialog::ButtonStyle::Danger
                                                      : StyledDialog::ButtonStyle::Primary, true);
            dlg.exec();
            return dlg.choice() == 1;
        }

        SaveChoice askToSave(QWidget* parent, const QString& text) {
            StyledDialog dlg(StyledDialog::Badge::Question, QStringLiteral("¿Guardar los cambios?"), parent);
            dlg.setMessage(text);
            dlg.addButton(QStringLiteral("No guardar"), 1, StyledDialog::ButtonStyle::Secondary);
            dlg.addButton(QStringLiteral("Cancelar"), 2, StyledDialog::ButtonStyle::Secondary, false, true);
            dlg.addButton(QStringLiteral("Guardar"), 0, StyledDialog::ButtonStyle::Primary, true);
            dlg.exec();
            switch (dlg.choice()) {
            case 0:  return SaveChoice::Save;
            case 1:  return SaveChoice::Discard;
            default: return SaveChoice::Cancel;
            }
        }

    } // namespace dialogs

} // namespace ui
