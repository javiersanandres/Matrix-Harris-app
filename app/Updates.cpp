#include "Updates.h"
#include "MainWindow.h"
#include "Translation.h"

#include <QApplication>
#include <QPointer>

#include <string>

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

        std::string g_appcast;   // where updates are announced
        std::string g_language;  // the language WinSparkle was started in

        // WinSparkle takes its language when it starts: started again for
        // another one, with everything set again.
        void startWinSparkle() {
            win_sparkle_set_appcast_url(g_appcast.c_str());
            win_sparkle_set_eddsa_public_key(PUBLIC_KEY);
            // Shown in its windows; also where it keeps its settings (next to the app's own).
            win_sparkle_set_app_details(
                QCoreApplication::organizationName().toStdWString().c_str(),
                QCoreApplication::applicationName().toStdWString().c_str(),
                QCoreApplication::applicationVersion().toStdWString().c_str());
            g_language = ui::i18n::code(ui::i18n::language()).toStdString();
            win_sparkle_set_lang(g_language.c_str());
            // Checking needs no permission prompt: it is on from the start.
            win_sparkle_set_automatic_check_for_updates(1);
            win_sparkle_set_can_shutdown_callback(canShutdown);
            win_sparkle_set_shutdown_request_callback(shutdownRequest);
            win_sparkle_init();
        }

    } // namespace

    void start(ui::MainWindow& window, const QString& appcast_url) {
        g_window = &window;
        g_appcast = appcast_url.isEmpty() ? std::string(APPCAST_URL) : appcast_url.toStdString();
        startWinSparkle();
        window.setUpdateChecker([] { win_sparkle_check_update_with_ui(); });
    }

    void follow(ui::MainWindow& window) {
        g_window = &window;
        // The language changed: its windows too (a check under way is dropped;
        // the daily one comes again).
        if (!g_language.empty() && g_language != ui::i18n::code(ui::i18n::language()).toStdString()) {
            win_sparkle_cleanup();
            startWinSparkle();
        }
    }

    void stop() {
        win_sparkle_cleanup();
    }

} // namespace updates
