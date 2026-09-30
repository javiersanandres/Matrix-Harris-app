#pragma once

#include "AppDialogs.h"
#include "ILPCancellationToken.h"

#include <QElapsedTimer>
#include <functional>
#include <thread>

class QLabel;
class QPushButton;
class QTimer;

namespace ui {

    class BusySpinner;
    class ProgressTrack;

    // ============================================================================
    // MinimizingProgressDialog
    //
    // Modal dialog shown while a crossing-minimization runs, in the application's
    // own dialog style (StyledDialog: rounded card, soft shadow, draggable):
    //
    //   ┌─────────────────────────────────────────────────────┐
    //   │  (spinner)  Minimizando cruces                      │
    //   │             What is going on, in plain words        │
    //   │             ▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▬▭▭▭▭▭▭▭▭▭▭▭▭▭▭             │
    //   │             Quedan 3 s               ⚡ Modo rápido  │
    //   │                                           [Pausar]  │
    //   └─────────────────────────────────────────────────────┘
    //
    // Two modes (see Options below):
    //   - fast: the bar fills over the solver's time budget with a countdown,
    //     then runs back and forth while the result is applied; no way to
    //     cancel -- there's nothing meaningful to cancel in a ~5s window.
    //   - slow: open-ended, so the bar runs back and forth and the caption shows
    //     the elapsed time; a "Pausar" button is wired to token.cancel().
    //
    // Either way it runs the caller-supplied `task` on a worker thread, so the
    // dialog's own event loop stays responsive while everything else in the
    // application is blocked by its modality -- this is what gives the user
    // feedback that something is happening, and what stops a frozen window from
    // silently queuing up repeat clicks that would otherwise all fire at once
    // when the call finally returns.
    //
    // Usage (see MainWindow::onMinimizeCrossings()):
    //
    //   int crossings = MinimizingProgressDialog::run(this,
    //       [&](hypergraph_logic::ILPCancellationToken& token) {
    //           return editor.minimizeCrossingsInterruptible(token);
    //       },
    //       { /*show_pausar=*/true, /*countdown_seconds=*/0 });
    //
    // run() blocks the calling (GUI) thread until the worker finishes -- one way
    // or another -- then either returns the achieved crossing count or rethrows
    // whatever exception the worker's task threw, so callers can catch it
    // exactly like a synchronous call.
    // ============================================================================
    class MinimizingProgressDialog : public StyledDialog {
        Q_OBJECT

    public:
        using Task = std::function<int(hypergraph_logic::ILPCancellationToken&)>;

        struct Options {
            // Slow mode: true -- shows "Pausar", wired to token.cancel(). Fast
            // mode: false -- the exact solve is already capped internally at
            // kILPTimeBudgetSeconds, so there's nothing useful to cancel in
            // that short a window. (Task always takes a token regardless, for
            // interface uniformity; a fast-mode task simply ignores it.)
            bool show_pausar = true;

            // > 0: the bar fills over this many seconds with a countdown, then
            // shows that the result is being applied (fast mode:
            // kILPTimeBudgetSeconds). 0: open-ended, with the elapsed time
            // (slow mode). Purely cosmetic -- the dialog always actually closes
            // on the worker finishing, never when the countdown reaches 0, since
            // the real run can take a little longer than the nominal budget
            // (heuristic fallback + layout recompute on top of the solve
            // itself).
            int countdown_seconds = 0;
        };

        // Runs `task` on a worker thread while showing a modal dialog centered
        // over anchor's top-level window (anchor is also used as this dialog's
        // Qt parent, so it stays on top of and closes with that window). Blocks
        // until the worker finishes; rethrows whatever exception `task` threw,
        // on the calling thread.
        static int run(QWidget* anchor, Task task, Options options = {});

    private slots:
        void onPausarClicked();
        void onWorkerFinished();
        void onFrame();

    protected:
        // Escape (and any other route to reject()) is ignored while the worker
        // is still running: destroying this dialog with worker_ still joinable
        // would call std::terminate(), and the whole point of this dialog is
        // that the only ways out are Pausar (still waits for an actual stop,
        // when shown at all) or the worker finishing on its own.
        void reject() override;

    private:
        explicit MinimizingProgressDialog(QWidget* parent, const Options& options);

        void startWorker(Task task);

    signals:
        // Emitted from the worker thread; Qt marshals this to the GUI thread
        // automatically (queued connection, since the receiver lives there).
        void workerFinished();

    private:
        Options options_;
        bool stopping_ = false; // "Pausar" pressed; waiting for the solver to stop

        hypergraph_logic::ILPCancellationToken token_;
        std::thread worker_;
        int crossings_ = 0;
        std::exception_ptr worker_exception_;

        ProgressTrack* track_;
        QLabel* caption_;
        QPushButton* pausar_btn_ = nullptr; // null when options_.show_pausar is false
        QTimer* frame_timer_;
        QElapsedTimer clock_;               // since the worker started
    };

} // namespace ui
