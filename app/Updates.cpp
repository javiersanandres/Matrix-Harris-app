#include "Updates.h"
#include "MainWindow.h"

#include <QApplication>
#include <QPointer>

#include <winsparkle.h>

namespace updates {

    namespace {

        // Always the update file of the latest release: GitHub redirects this
        // address to the asset of whichever release is the newest.
        constexpr const char* APPCAST_URL =
            "https://github.com/javiersanandres/Matrix-Harris-app/releases/latest/download/appcast.xml";

        // Public half of the publisher's signing key (winsparkle-tool generate-key).
        // Not secret; the private half never leaves the publisher's machine.
        constexpr const char* PUBLIC_KEY = "r4Ps1qG6S0ESQi3o9qr9jJAP4XlwOpCrSOXeDR8WCG4=";

        QPointer<ui::MainWindow> g_window;

        // WinSparkle asks from a thread of its own; the question (save the
        // changes?) is asked on the main one, and the answer awaited.
        int __cdecl canShutdown() {
            bool may_close = false;
            if (g_window) {
                QMetaObject::invokeMethod(g_window.data(), [&may_close] {
                    may_close = g_window && g_window->prepareToCloseForUpdate();
                }, Qt::BlockingQueuedConnection);
            }
            return may_close ? 1 : 0;
        }

        // The installer is running: the app closes so it can be replaced.
        void __cdecl shutdownRequest() {
            QMetaObject::invokeMethod(qApp, [] { QApplication::quit(); }, Qt::QueuedConnection);
        }

    } // namespace

    void start(ui::MainWindow& window, const QString& appcast_url) {
        g_window = &window;

        win_sparkle_set_appcast_url(appcast_url.isEmpty() ? APPCAST_URL : appcast_url.toUtf8().constData());
        win_sparkle_set_eddsa_public_key(PUBLIC_KEY);
        // Shown in its windows; also where it keeps its settings (next to the app's own).
        win_sparkle_set_app_details(
            QCoreApplication::organizationName().toStdWString().c_str(),
            QCoreApplication::applicationName().toStdWString().c_str(),
            QCoreApplication::applicationVersion().toStdWString().c_str());
        win_sparkle_set_lang("es");
        // Checking needs no permission prompt: it is on from the start.
        win_sparkle_set_automatic_check_for_updates(1);
        win_sparkle_set_can_shutdown_callback(canShutdown);
        win_sparkle_set_shutdown_request_callback(shutdownRequest);
        win_sparkle_init();

        window.setUpdateChecker([] { win_sparkle_check_update_with_ui(); });
    }

    void stop() {
        win_sparkle_cleanup();
    }

} // namespace updates
