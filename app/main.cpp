#include "MainWindow.h"
#include "GurobiNotice.h"
#include "Updates.h"
#include "Translation.h"
#include <QApplication>
#include <QIcon>
#include <QPointer>
#include <QSettings>
#include <string>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    // Also where the settings are kept: the same in every language.
    app.setApplicationName(QStringLiteral("Taller Matrix Harris"));
    app.setOrganizationName(QStringLiteral("Javier San Andrés"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION)); // from CMake's project VERSION
    // Title bar and taskbar of every window: the executable's own icon, with
    // all its sizes (read by Qt's ico plugin).
    app.setWindowIcon(QIcon(QStringLiteral(":/app/app.ico")));

    // The language chosen last; before any choice, the system's (Spanish for
    // Spanish and close languages, English otherwise). Also Qt's own dialogs
    // (colour picker, message box buttons, ...) follow it.
    {
        QSettings settings;
        ui::i18n::apply(ui::i18n::saved(settings));
    }

    // The Gurobi developer options (see GurobiNotice.h) are taken out first,
    // so they are never mistaken for a project to open.
    QStringList args = app.arguments();
    const ui::gurobi::StartupOptions gurobi_options = ui::gurobi::takeStartupOptions(args);
    // --tutorial: show the introductory tour even if it was already seen.
    const bool force_tutorial = args.removeAll(QStringLiteral("--tutorial")) > 0;
    // --appcast=URL: look for updates there instead of the latest GitHub
    // release (to try a release before publishing it).
    QString appcast_url;
    for (int i = static_cast<int>(args.size()) - 1; i > 0; --i) {
        if (!args.at(i).startsWith(QStringLiteral("--appcast="))) continue;
        appcast_url = args.at(i).mid(static_cast<int>(QStringLiteral("--appcast=").size()));
        args.removeAt(i);
    }

    // Changing the language replaces the window (see MainWindow::switchLanguage):
    // `window` always points to the current one.
    QPointer<ui::MainWindow> window = new ui::MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    std::function<void(ui::MainWindow*)> follow = [&window, &follow](ui::MainWindow* w) {
        QObject::connect(w, &ui::MainWindow::replaced, w, [&window, &follow](ui::MainWindow* next) {
            window = next;
            updates::follow(*next);
            follow(next);
        });
    };
    follow(window);
    window->showMaximized();

    // "TallerMatrixHarris proyecto.json" (e.g. opening a project from the file
    // explorer) starts with that project open.
    if (args.size() > 1) window->openProject(args.at(1));

    // Whether Gurobi is installed and licensed; if not, a notice says how to
    // get it once the window is up. Then, on the first run, the introductory
    // tour (at once when there was no notice), so the user can play with the
    // app right after it. A notice the user asked not to see again points to
    // its guide in Ayuda.
    ui::gurobi::checkAtStartup(window, gurobi_options, [&window, force_tutorial](ui::gurobi::Notice silenced) {
        if (!window) return;
        using Entry = ui::MainWindow::HelpEntry;
        if (silenced == ui::gurobi::Notice::Install) window->pointToHelpEntry(Entry::GurobiInstall);
        if (silenced == ui::gurobi::Notice::License) window->pointToHelpEntry(Entry::GurobiLicense);
        window->startTutorialIfFirstRun(force_tutorial);
    });

    // Checks for a newer version (at most once a day) once the window is up;
    // Ayuda › Buscar actualizaciones… checks right away.
    updates::start(*window, appcast_url);

    const int result = app.exec();
    updates::stop();
    return result;
}
