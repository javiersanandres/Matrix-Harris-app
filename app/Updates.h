#pragma once

#include <QString>

namespace ui { class MainWindow; }

namespace updates {

    // ============================================================================
    // Automatic updates (WinSparkle)
    //
    // On startup (at most once a day) the app downloads the update file
    // published with the latest GitHub release (appcast.xml, see
    // installer/make_release.ps1). When it announces a newer version, a window
    // in Spanish offers it with its release notes; if accepted, the new
    // installer is downloaded, its signature checked against the public key
    // below (only installers signed with the publisher's private key are run),
    // the user is asked to save unsaved changes as when closing, and the app
    // closes while the installer updates it and opens it again.
    //
    // Ayuda › Buscar actualizaciones… checks right away.
    //
    // WinSparkle only lives in the executable: the UI library and its tests
    // never load it.
    // ============================================================================

    // Starts checking (call once the main window is shown). appcast_url, when
    // given, replaces the published update file: the --appcast=URL developer
    // option, to try a release before publishing it. Installers must still be
    // signed with the publisher's key.
    void start(ui::MainWindow& window, const QString& appcast_url = {});

    // Stops every pending check or download (call when the app quits).
    void stop();

} // namespace updates
