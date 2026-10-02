#include "GurobiNotice.h"
#include "AppDialogs.h"
#include "GurobiGuide.h"
#include "UiStyle.h"

#include <QAbstractButton>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QtDebug>

namespace ui::gurobi {

    using hypergraph_logic::GurobiStatus;

    namespace {

        QString reminderKey(Notice notice, const QString& group) {
            return group + (notice == Notice::Install ? QStringLiteral("/remindInstall")
                                                      : QStringLiteral("/remindLicense"));
        }

        // "No volver a recordármelo": a rounded box that fills with the accent
        // gradient and a white tick, and its label. The whole row is clickable.
        class TickBox : public QAbstractButton {
        public:
            TickBox(const QString& text, QWidget* parent) : QAbstractButton(parent) {
                setText(text);
                setCheckable(true);
                setCursor(Qt::PointingHandCursor);
                QFont f = font();
                f.setPointSizeF(9.5);
                setFont(f);
            }

            QSize sizeHint() const override {
                return QSize(BOX + 9 + fontMetrics().horizontalAdvance(text()) + 2,
                    std::max(BOX, fontMetrics().height()) + 4);
            }

        protected:
            void enterEvent(QEnterEvent*) override { hovered_ = true; update(); }
            void leaveEvent(QEvent*) override { hovered_ = false; update(); }

            void paintEvent(QPaintEvent*) override {
                QPainter p(this);
                p.setRenderHint(QPainter::Antialiasing);
                const QRectF box(0.5, (height() - BOX) / 2.0 + 0.5, BOX - 1, BOX - 1);
                if (isChecked()) {
                    QLinearGradient g(box.topLeft(), box.bottomRight());
                    g.setColorAt(0, style::palette::accent);
                    g.setColorAt(1, style::palette::violet);
                    p.setPen(Qt::NoPen);
                    p.setBrush(g);
                    p.drawRoundedRect(box, 5, 5);
                    QPainterPath tick;
                    tick.moveTo(box.left() + 4.2, box.center().y() + 0.3);
                    tick.lineTo(box.left() + 7.3, box.center().y() + 3.3);
                    tick.lineTo(box.right() - 3.8, box.center().y() - 3.6);
                    p.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.setBrush(Qt::NoBrush);
                    p.drawPath(tick);
                }
                else {
                    p.setPen(QPen(hovered_ ? style::palette::accent : QColor(0xC3, 0xC8, 0xD6), 1.3));
                    p.setBrush(hovered_ ? style::palette::accent_soft : QColor(Qt::white));
                    p.drawRoundedRect(box, 5, 5);
                }
                p.setPen(style::palette::ink_soft);
                p.drawText(QRectF(BOX + 9, 0, width() - BOX - 9, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
            }

        private:
            static constexpr int BOX = 18;
            bool hovered_ = false;
        };

        // Shows the notice for `status` over `window`, if there is one to show.
        void present(QWidget* window, GurobiStatus status, const QString& group) {
            QSettings settings;
            const Notice notice = pendingNotice(status, settings, group);
            if (notice != Notice::None) showNotice(notice, window, group);
        }

        // The window has had time to appear before a notice covers it.
        constexpr int NOTICE_DELAY_MS = 450;

    } // namespace

    Notice pendingNotice(GurobiStatus status, QSettings& settings, const QString& group) {
        switch (status) {
        case GurobiStatus::Ready:
            // A working license: whatever was silenced, a future problem (the
            // license expiring) must be heard about again.
            resetReminders(settings, group);
            return Notice::None;
        case GurobiStatus::NotInstalled:
            return settings.value(reminderKey(Notice::Install, group), true).toBool() ? Notice::Install : Notice::None;
        case GurobiStatus::NoLicense:
            return settings.value(reminderKey(Notice::License, group), true).toBool() ? Notice::License : Notice::None;
        }
        return Notice::None;
    }

    void stopReminding(Notice notice, QSettings& settings, const QString& group) {
        if (notice == Notice::None) return;
        settings.setValue(reminderKey(notice, group), false);
    }

    void resetReminders(QSettings& settings, const QString& group) {
        settings.remove(reminderKey(Notice::Install, group));
        settings.remove(reminderKey(Notice::License, group));
    }

    StartupOptions takeStartupOptions(QStringList& args) {
        StartupOptions options;
        const QString test_prefix = QStringLiteral("--gurobi-test=");
        for (int i = 1; i < args.size();) {
            const QString& arg = args.at(i);
            if (arg.startsWith(test_prefix)) {
                const QString state = arg.mid(test_prefix.size());
                if (state == QLatin1String("not-installed"))   options.simulated = GurobiStatus::NotInstalled;
                else if (state == QLatin1String("no-license")) options.simulated = GurobiStatus::NoLicense;
                else if (state == QLatin1String("ready"))      options.simulated = GurobiStatus::Ready;
                else qWarning().noquote() << "--gurobi-test: unknown state" << state
                                          << "(expected not-installed, no-license or ready)";
                args.removeAt(i);
            }
            else if (arg == QLatin1String("--gurobi-test-reset")) {
                options.reset_reminders = true;
                args.removeAt(i);
            }
            else {
                ++i;
            }
        }
        return options;
    }

    void checkAtStartup(QWidget* window, const StartupOptions& options) {
        const QString group = options.simulated ? TEST_REMINDERS_GROUP : REMINDERS_GROUP;
        if (options.reset_reminders) {
            QSettings settings;
            resetReminders(settings, group);
        }

        if (options.simulated) {
            const GurobiStatus status = *options.simulated;
            QTimer::singleShot(NOTICE_DELAY_MS, window, [window, status, group] { present(window, status, group); });
            return;
        }

        // The license check can take a moment (a license server may be asked),
        // so it runs off the UI thread; its answer is cached for the solvers.
        QThread* check = QThread::create([] { hypergraph_logic::gurobiStatus(); });
        QObject::connect(check, &QThread::finished, check, &QObject::deleteLater);
        QObject::connect(check, &QThread::finished, window, [window, group] {
            QTimer::singleShot(NOTICE_DELAY_MS, window, [window, group] {
                present(window, hypergraph_logic::gurobiStatus(), group);
            });
        });
        check->start();
    }

    void showNotice(Notice notice, QWidget* parent, const QString& group) {
        if (notice == Notice::None) return;
        const bool install = notice == Notice::Install;

        StyledDialog dlg(install ? StyledDialog::Badge::Info : StyledDialog::Badge::Warning,
            install ? QStringLiteral("Gurobi no está instalado")
                    : QStringLiteral("Falta la licencia de Gurobi"),
            parent);
        dlg.setMessage(install
            ? QStringLiteral("Esta aplicación funciona mejor con Gurobi instalado. Si eres estudiante, profesor "
                             "o investigador universitario, te recomendamos instalar el programa para una mejor "
                             "experiencia.")
            : QStringLiteral("No se ha encontrado una licencia de Gurobi válida. Si eres estudiante, profesor o "
                             "investigador universitario puedes obtener una licencia válida de manera gratuita."));

        auto* body = new QWidget;
        auto* col = new QVBoxLayout(body);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(14);

        auto* link = new QPushButton(install
            ? QStringLiteral("Haga clic aquí para más información sobre cómo instalar Gurobi  ›")
            : QStringLiteral("Haga clic aquí para ver cómo instalar una licencia de Gurobi  ›"), body);
        link->setObjectName("guideLink");
        link->setCursor(Qt::PointingHandCursor);
        link->setAutoDefault(false);
        link->setStyleSheet(QStringLiteral(
            "QPushButton#guideLink { background: #EEF0FF; color: #4F46E5; border: 1px solid #DCDFFD;"
            " border-radius: 10px; padding: 10px 14px; font-weight: 700; text-align: left; }"
            "QPushButton#guideLink:hover { background: #E2E5FF; border-color: #C9CDFB; }"
            "QPushButton#guideLink:pressed { background: #D7DBFF; }"));
        col->addWidget(link);

        auto* tick = new TickBox(QStringLiteral("No volver a recordármelo"), body);
        col->addWidget(tick, 0, Qt::AlignLeft);
        dlg.setBody(body);

        QObject::connect(link, &QPushButton::clicked, &dlg, [&dlg, install] {
            showGuide(install ? Guide::Install : Guide::License, &dlg);
        });
        dlg.addButton(QStringLiteral("Entendido"), 0, StyledDialog::ButtonStyle::Primary, true, true);
        dlg.exec();

        if (tick->isChecked()) {
            QSettings settings;
            stopReminding(notice, settings, group);
        }
    }

} // namespace ui::gurobi
