#include "MainWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Matrix-Harris");
    app.setOrganizationName("Javier San Andrés");
    
    ui::MainWindow window;
    window.showMaximized();

    return app.exec();
}
