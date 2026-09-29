#include "MainWindow.h"
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

    ui::MainWindow window;
    window.showMaximized();

    // "MatrixHarrisApp proyecto.json" (e.g. opening a project from the file
    // explorer) starts with that project open.
    const QStringList args = app.arguments();
    if (args.size() > 1) window.openProject(args.at(1));

    return app.exec();
}
