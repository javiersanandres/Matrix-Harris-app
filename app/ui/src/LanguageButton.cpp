#include "LanguageButton.h"

#include "UiStyle.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>

namespace ui {

    namespace {

        constexpr QSize FLAG_SIZE(21, 14);

        // The language's flag (Spain's, the United Kingdom's), drawn at the
        // screen's pixel ratio with rounded corners and a faint outline. Small
        // as they are, Spain's has no coat of arms and the Union Jack's red
        // diagonals are not offset.
        QIcon flag(i18n::Language language, qreal dpr) {
            QPixmap pm(FLAG_SIZE * dpr);
            pm.setDevicePixelRatio(dpr);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r(0.5, 0.5, FLAG_SIZE.width() - 1.0, FLAG_SIZE.height() - 1.0);
            QPainterPath shape;
            shape.addRoundedRect(r, 2.0, 2.0);
            p.setClipPath(shape);

            if (language == i18n::Language::Spanish) {
                // Red, yellow twice as tall, red.
                const double band = r.height() / 4.0;
                p.fillRect(r, QColor(0xAA, 0x15, 0x1B));
                p.fillRect(QRectF(r.left(), r.top() + band, r.width(), 2.0 * band), QColor(0xF1, 0xBF, 0x00));
            }
            else {
                // Drawn on the flag's own 60 × 30 grid.
                p.save();
                p.translate(r.topLeft());
                p.scale(r.width() / 60.0, r.height() / 30.0);
                p.fillRect(QRectF(0, 0, 60, 30), QColor(0x01, 0x21, 0x69));
                const QColor red(0xC8, 0x10, 0x2E);
                auto line = [&p](const QColor& c, double width, QPointF a, QPointF b) {
                    p.setPen(QPen(c, width, Qt::SolidLine, Qt::FlatCap));
                    p.drawLine(a, b);
                };
                line(Qt::white, 6, { 0, 0 }, { 60, 30 });
                line(Qt::white, 6, { 60, 0 }, { 0, 30 });
                line(red, 2.5, { 0, 0 }, { 60, 30 });
                line(red, 2.5, { 60, 0 }, { 0, 30 });
                line(Qt::white, 10, { 30, 0 }, { 30, 30 });
                line(Qt::white, 10, { 0, 15 }, { 60, 15 });
                line(red, 6, { 30, 0 }, { 30, 30 });
                line(red, 6, { 0, 15 }, { 60, 15 });
                p.restore();
            }

            p.setClipping(false);
            p.setPen(QPen(QColor(0, 0, 0, 50), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawPath(shape);
            p.end();
            return QIcon(pm);
        }

    } // namespace

    LanguageButton::LanguageButton(QWidget* parent)
        : QToolButton(parent)
    {
        setObjectName("languageButton");
        const qreal dpr = devicePixelRatioF();
        // The flag and the code as two pieces with room between them (a tool
        // button's own icon and text sit too close), the whole centred.
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(10, 4, 11, 4);
        row->setSpacing(8);
        auto* flag_label = new QLabel(this);
        flag_label->setPixmap(flag(i18n::language(), dpr).pixmap(FLAG_SIZE, dpr));
        auto* code_label = new QLabel(i18n::code(i18n::language()).toUpper(), this);
        code_label->setObjectName("languageCode");
        for (QLabel* label : { flag_label, code_label }) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents);
            row->addWidget(label, 0, Qt::AlignVCenter);
        }
        setFixedSize(row->sizeHint());
        setPopupMode(QToolButton::InstantPopup);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        // In both languages: whoever needs it may not read the current one.
        setToolTip(QStringLiteral("Idioma · Language"));
        setStyleSheet(QStringLiteral(
            "QToolButton#languageButton { border: 1px solid #E3E6EF; border-radius: 8px; background: white; }"
            "QLabel#languageCode { color: #312E81; font-weight: 600; background: transparent; }"
            "QToolButton#languageButton:hover { background: #EEF0FF; border-color: #A5A8F5; }"
            "QToolButton#languageButton::menu-indicator { image: none; width: 0; }"));

        QMenu* menu = style::createMenu(this);
        const std::pair<i18n::Language, QString> languages[] = {
            { i18n::Language::Spanish, QStringLiteral("Español") },
            { i18n::Language::English, QStringLiteral("English") },
        };
        for (const auto& [language, name] : languages) {
            QAction* a = menu->addAction(flag(language, dpr), name);
            a->setCheckable(true);
            a->setChecked(language == i18n::language());
            connect(a, &QAction::triggered, this, [this, language] {
                if (language != i18n::language()) emit languageChosen(language);
            });
        }
        setMenu(menu);
    }

} // namespace ui
