#pragma once

#include "GurobiRuntime.h"

#include <QString>
#include <QStringList>

#include <optional>

class QSettings;
class QWidget;

namespace ui::gurobi {

    // ============================================================================
    // Gurobi notice at startup
    //
    // Every time the application starts, it checks (in the background) what
    // Gurobi offers on this machine and, when it is not fully usable, says so
    // once the window is up:
    //
    //   - not installed: Gurobi makes the application work better; a link
    //     opens the installation guide;
    //   - installed, no valid license: a link opens the license guide.
    //
    // Each notice has its own "No volver a recordármelo", stored in the user's
    // settings (it survives restarts). Dismissing the first one does not
    // silence the second, so installing Gurobi later still brings up the
    // license notice. Finding a valid license switches both back on: when
    // that license expires, the user hears about it again.
    // ============================================================================

    enum class Notice { None, Install, License };

    // Settings group for the reminders of a real start.
    inline const QString REMINDERS_GROUP = QStringLiteral("gurobi");
    // Separate group for simulated starts (--gurobi-test), so testing never
    // changes what a real start remembers.
    inline const QString TEST_REMINDERS_GROUP = QStringLiteral("gurobiTest");

    // The notice to show for `status`, given the reminders in `group`. A Ready
    // status switches both reminders back on (and shows nothing).
    Notice pendingNotice(hypergraph_logic::GurobiStatus status, QSettings& settings, const QString& group);

    // "No volver a recordármelo" for that notice.
    void stopReminding(Notice notice, QSettings& settings, const QString& group);

    // Both reminders back on.
    void resetReminders(QSettings& settings, const QString& group);

    // Developer options, for trying the notices without touching the real
    // Gurobi installation:
    //   --gurobi-test=not-installed|no-license|ready
    //       Pretend Gurobi is in that state (only for the notice: the solvers
    //       keep using whatever is really there). Reminders go to their own
    //       settings group.
    //   --gurobi-test-reset
    //       Switch the reminders back on first (the test group's when combined
    //       with --gurobi-test, the real ones otherwise).
    struct StartupOptions {
        std::optional<hypergraph_logic::GurobiStatus> simulated;
        bool reset_reminders = false;
    };

    // Reads (and removes) those options from the command line, so the rest of
    // it, e.g. a project to open, is left as it was.
    StartupOptions takeStartupOptions(QStringList& args);

    // Starts the check; the notice, if any, appears over `window` when ready.
    void checkAtStartup(QWidget* window, const StartupOptions& options);

    // Shows `notice` (Install or License) over `parent`, recording a "No volver
    // a recordármelo" in `group`.
    void showNotice(Notice notice, QWidget* parent, const QString& group);

} // namespace ui::gurobi
