#include "MinimizingProgressDialog.h"
#include "BusySpinner.h"
#include "UiStyle.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace hypergraph_logic;

namespace ui {

    // ============================================================================
    // ProgressTrack
    //
    // A slim rounded bar. With a fraction in [0, 1] it fills from the left in
    // the brand gradient; without one (indeterminate) a short segment glides
    // back and forth along it.
    // ============================================================================
    class ProgressTrack : public QWidget {
    public:
        explicit ProgressTrack(QWidget* parent) : QWidget(parent) {
            setFixedHeight(6);
            setMinimumWidth(300);
        }

        void setFraction(double fraction) { fraction_ = std::clamp(fraction, 0.0, 1.0); update(); }
        void setIndeterminate(double seconds) { fraction_ = -1.0; seconds_ = seconds; update(); }

    protected:
        void paintEvent(QPaintEvent*) override {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r = rect();
            const double radius = r.height() / 2.0;

            QPainterPath track;
            track.addRoundedRect(r, radius, radius);
            p.fillPath(track, QColor(0xEE, 0xF0, 0xF6));
            p.setClipPath(track);

            QRectF fill;
            if (fraction_ >= 0.0) {
                fill = QRectF(r.left(), r.top(), r.width() * fraction_, r.height());
            }
            else {
                // A segment of 35% of the width sweeping to and fro, easing at
                // both ends (one sweep every 1.1 s).
                const double w = r.width() * 0.35;
                constexpr double kPi = 3.14159265358979323846;
                const double s = 0.5 - 0.5 * std::cos(seconds_ * kPi / 1.1);
                fill = QRectF(r.left() + s * (r.width() - w), r.top(), w, r.height());
            }
            if (fill.width() <= 0.0) return;

            QLinearGradient g(r.topLeft(), r.topRight());
            g.setColorAt(0, style::palette::accent);
            g.setColorAt(1, style::palette::violet);
            QPainterPath bar;
            bar.addRoundedRect(fill, radius, radius);
            p.fillPath(bar, g);
        }

    private:
        double fraction_ = 0.0; // < 0: indeterminate
        double seconds_ = 0.0;
    };

    namespace {

        // "⚡ Modo rápido" / "🐢 Modo lento" pill, in the colours the floating
        // minimise panel uses for each mode.
        QWidget* modeChip(bool fast, QWidget* parent) {
            const QColor colour = fast ? style::palette::amber : style::palette::green;
            auto* chip = new QFrame(parent);
            chip->setObjectName("modeChip");
            chip->setStyleSheet(QStringLiteral(
                "QFrame#modeChip { background: rgba(%1, %2, %3, 28); border-radius: 10px; }"
                "QFrame#modeChip QLabel { color: %4; font-size: 9pt; font-weight: 600; background: transparent; }")
                .arg(colour.red()).arg(colour.green()).arg(colour.blue())
                .arg(colour.darker(125).name()));
            auto* row = new QHBoxLayout(chip);
            row->setContentsMargins(7, 2, 9, 2);
            row->setSpacing(4);
            auto* icon = new QLabel(chip);
            icon->setPixmap(style::icon(fast ? style::Icon::Fast : style::Icon::Slow)
                .pixmap(QSize(14, 14), chip->devicePixelRatioF()));
            row->addWidget(icon);
            row->addWidget(new QLabel(fast ? QStringLiteral("Modo rápido") : QStringLiteral("Modo lento"), chip));
            return chip;
        }

        QString clock(int seconds) {
            return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
        }

    } // namespace

    // ============================================================================
    // MinimizingProgressDialog
    // ============================================================================

    MinimizingProgressDialog::MinimizingProgressDialog(QWidget* parent, const Options& options)
        : StyledDialog(Badge::Info, QStringLiteral("Minimizando cruces"), parent)
        , options_(options)
    {
        setWindowModality(Qt::ApplicationModal);
        setEmblem(new BusySpinner);

        const bool fast = options_.countdown_seconds > 0;
        setMessage(fast
            ? QStringLiteral("Reordenando las cajas para que las conexiones se crucen lo menos "
                             "posible. Tardará %1 segundos como máximo.").arg(options_.countdown_seconds)
            : QStringLiteral("Buscando el orden de las cajas con el menor número de cruces, sin "
                             "límite de tiempo. Puedes pausar cuando quieras: se usará el mejor "
                             "dibujo encontrado hasta entonces."));

        auto* body = new QWidget;
        auto* column = new QVBoxLayout(body);
        column->setContentsMargins(0, 4, 0, 0);
        column->setSpacing(8);
        track_ = new ProgressTrack(body);
        column->addWidget(track_);
        auto* captions = new QHBoxLayout;
        captions->setSpacing(8);
        caption_ = new QLabel(body);
        caption_->setStyleSheet(QStringLiteral("color: #8A90A2; font-size: 9pt;"));
        // Its text changes while the dialog is on screen: it must not resize it.
        caption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        captions->addWidget(caption_, 1);
        captions->addWidget(modeChip(fast, body));
        column->addLayout(captions);
        setBody(body);

        if (options_.show_pausar) {
            pausar_btn_ = addActionButton(QStringLiteral("Pausar"), ButtonStyle::Secondary);
            pausar_btn_->setIcon(style::icon(style::Icon::Pause));
            pausar_btn_->setToolTip(QStringLiteral("Detiene la búsqueda y usa el mejor dibujo encontrado hasta ahora"));
            connect(pausar_btn_, &QPushButton::clicked,
                this, &MinimizingProgressDialog::onPausarClicked);
        }

        frame_timer_ = new QTimer(this);
        frame_timer_->setInterval(16);
        connect(frame_timer_, &QTimer::timeout, this, &MinimizingProgressDialog::onFrame);

        connect(this, &MinimizingProgressDialog::workerFinished,
            this, &MinimizingProgressDialog::onWorkerFinished);

        onFrame(); // initial caption and bar
    }

    void MinimizingProgressDialog::onFrame() {
        const double t = clock_.isValid() ? clock_.elapsed() / 1000.0 : 0.0;
        const int budget = options_.countdown_seconds;

        if (stopping_) {
            track_->setIndeterminate(t);
            caption_->setText(QStringLiteral("Deteniendo la búsqueda…"));
        }
        else if (budget > 0 && t < budget) {
            track_->setFraction(t / budget);
            const int left = static_cast<int>(std::ceil(budget - t));
            caption_->setText(left == 1 ? QStringLiteral("Queda 1 segundo")
                                        : QStringLiteral("Quedan %1 segundos").arg(left));
        }
        else if (budget > 0) {
            // The budget is only the solver's: applying the result takes a moment more.
            track_->setIndeterminate(t);
            caption_->setText(QStringLiteral("Aplicando el resultado…"));
        }
        else {
            track_->setIndeterminate(t);
            caption_->setText(QStringLiteral("Tiempo transcurrido: %1").arg(clock(static_cast<int>(t))));
        }
    }

    void MinimizingProgressDialog::startWorker(Task task) {
        clock_.start();
        frame_timer_->start();
        worker_ = std::thread([this, task = std::move(task)]() {
            try {
                crossings_ = task(token_);
            }
            catch (...) {
                worker_exception_ = std::current_exception();
            }
            emit workerFinished(); // queued to the GUI thread automatically
            });
    }

    void MinimizingProgressDialog::onPausarClicked() {
        token_.cancel();
        stopping_ = true;
        pausar_btn_->setEnabled(false);
        pausar_btn_->setText(QStringLiteral("Deteniendo…"));
        onFrame();
    }

    void MinimizingProgressDialog::onWorkerFinished() {
        if (worker_.joinable()) worker_.join();
        frame_timer_->stop();
        accept();
    }

    void MinimizingProgressDialog::reject() {
        if (worker_.joinable()) return; // still running: refuse to close
        StyledDialog::reject();
    }

    int MinimizingProgressDialog::run(QWidget* anchor, Task task, Options options) {
        QWidget* window = anchor ? anchor->window() : nullptr;
        MinimizingProgressDialog dlg(window, options);
        // StyledDialog sizes the dialog and centres it over the window as it is shown.
        dlg.startWorker(std::move(task));
        dlg.exec(); // returns once onWorkerFinished() calls accept()

        if (dlg.worker_exception_) std::rethrow_exception(dlg.worker_exception_);
        return dlg.crossings_;
    }

} // namespace ui
