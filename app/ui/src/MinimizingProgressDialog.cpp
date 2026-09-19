#include "MinimizingProgressDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace hypergraph_logic;

namespace ui {

    MinimizingProgressDialog::MinimizingProgressDialog(QWidget* parent, const Options& options)
        : QDialog(parent)
        , options_(options)
        , remaining_seconds_(options.countdown_seconds)
    {
        setWindowTitle("Minimizando cruces");
        setWindowModality(Qt::ApplicationModal);
        // No '?' / close buttons -- while the worker is running, the only ways
        // out are "Pausar" (cancel, when shown) or letting it finish; see
        // reject() below.
        setWindowFlags((windowFlags() | Qt::CustomizeWindowHint)
            & ~Qt::WindowCloseButtonHint & ~Qt::WindowContextHelpButtonHint);
        setFixedWidth(280);

        spinner_ = new BusySpinner(this);

        message_label_ = new QLabel(this);
        message_label_->setWordWrap(true);
        message_label_->setAlignment(Qt::AlignHCenter);
        updateCountdownLabel(); // sets the initial text for whichever mode this is

        auto* spinner_row = new QHBoxLayout;
        spinner_row->addStretch();
        spinner_row->addWidget(spinner_);
        spinner_row->addStretch();

        auto* layout = new QVBoxLayout(this);
        layout->addLayout(spinner_row);
        layout->addWidget(message_label_);

        if (options_.show_pausar) {
            pausar_btn_ = new QPushButton("Pausar", this);
            connect(pausar_btn_, &QPushButton::clicked,
                this, &MinimizingProgressDialog::onPausarClicked);

            auto* btn_row = new QHBoxLayout;
            btn_row->addStretch();
            btn_row->addWidget(pausar_btn_);
            btn_row->addStretch();

            layout->addSpacing(8);
            layout->addLayout(btn_row);
        }

        if (options_.countdown_seconds > 0) {
            countdown_timer_ = new QTimer(this);
            countdown_timer_->setInterval(1000);
            connect(countdown_timer_, &QTimer::timeout,
                this, &MinimizingProgressDialog::onCountdownTick);
            countdown_timer_->start();
        }

        connect(this, &MinimizingProgressDialog::workerFinished,
            this, &MinimizingProgressDialog::onWorkerFinished);
    }

    void MinimizingProgressDialog::updateCountdownLabel() {
        if (options_.countdown_seconds <= 0) {
            message_label_->setText("Buscando la solución óptima…\nEsto puede tardar.");
        }
        else if (remaining_seconds_ > 0) {
            message_label_->setText(
                QString("Buscando la solución óptima…\nTiempo restante: %1 s")
                .arg(remaining_seconds_));
        }
        else {
            message_label_->setText("Finalizando…");
        }
    }

    void MinimizingProgressDialog::onCountdownTick() {
        if (remaining_seconds_ > 0) --remaining_seconds_;
        updateCountdownLabel();
        if (remaining_seconds_ <= 0) countdown_timer_->stop();
    }

    void MinimizingProgressDialog::startWorker(Task task) {
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
        pausar_btn_->setEnabled(false);
        pausar_btn_->setText("Deteniendo…");
        message_label_->setText(
            "Deteniendo…\nSe usará la mejor solución encontrada hasta ahora.");
    }

    void MinimizingProgressDialog::onWorkerFinished() {
        if (worker_.joinable()) worker_.join();
        accept();
    }

    void MinimizingProgressDialog::reject() {
        if (worker_.joinable()) return; // still running: refuse to close
        QDialog::reject();
    }

    int MinimizingProgressDialog::run(QWidget* anchor, Task task, Options options) {
        QWidget* window = anchor ? anchor->window() : nullptr;
        MinimizingProgressDialog dlg(window, options);

        // Center over the main window: finalize the dialog's size from its
        // layout first, then center that rect over the window's current
        // geometry.
        dlg.adjustSize();
        if (window != nullptr) {
            dlg.move(window->frameGeometry().center() - dlg.rect().center());
        }

        dlg.startWorker(std::move(task));
        dlg.exec(); // returns once onWorkerFinished() calls accept()

        if (dlg.worker_exception_) std::rethrow_exception(dlg.worker_exception_);
        return dlg.crossings_;
    }

} // namespace ui