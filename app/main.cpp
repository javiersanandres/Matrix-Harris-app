#include "MainWindow.h"
#include "GurobiNotice.h"
#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Matrix-Harris");
    app.setOrganizationName("Javier San Andrés");
    app.setApplicationVersion("0.1.0");

    // The app is in Spanish, so Qt's own dialogs (colour picker, file dialogs,
    // message box buttons, ...) must be too. Look for Qt's Spanish translations
    // next to the executable first (deployed builds), then in the Qt install.
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
    QTranslator qt_translator;
    const QStringList dirs = {
        QApplication::applicationDirPath() + "/translations",
        QLibraryInfo::path(QLibraryInfo::TranslationsPath) };
    for (const QString& dir : dirs) {
        if (qt_translator.load(QLocale(), "qtbase", "_", dir)) {
            app.installTranslator(&qt_translator);
            break;
        }
    }

    // The Gurobi developer options (see GurobiNotice.h) are taken out first,
    // so they are never mistaken for a project to open.
    QStringList args = app.arguments();
    const ui::gurobi::StartupOptions gurobi_options = ui::gurobi::takeStartupOptions(args);

    ui::MainWindow window;
    window.showMaximized();

    // "MatrixHarrisApp proyecto.json" (e.g. opening a project from the file
    // explorer) starts with that project open.
    if (args.size() > 1) window.openProject(args.at(1));

    // Whether Gurobi is installed and licensed; if not, a notice says how to
    // get it once the window is up.
    ui::gurobi::checkAtStartup(&window, gurobi_options);

    return app.exec();
}
