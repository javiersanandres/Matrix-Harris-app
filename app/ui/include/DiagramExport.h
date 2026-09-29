#pragma once

#include <QString>
#include <vector>

class QGraphicsScene;

namespace ui::exporting {

    // ============================================================================
    // DiagramExport
    //
    // Writes diagrams to files, used by "Exportar esquema como imagen" (one
    // diagram) and "Exportar proyecto" (every diagram plus the joint one).
    // Scenes are pictured exactly as they are drawn on screen, so any pending
    // selection should be cancelled first.
    // ============================================================================

    // One diagram in an export.
    struct Page {
        QString title;          // the diagram's name
        QGraphicsScene* scene;  // may be empty: it then gets an "empty" page
    };

    // A PDF with one A4 page per diagram, in order. Each page carries the
    // diagram's name, the project's name, the drawing scaled to fit (never
    // enlarged) in the orientation that suits it best, and "Página i de N".
    // Returns false if the file cannot be written.
    bool writePdf(const QString& path, const QString& project_name, const std::vector<Page>& pages);

    // The drawing alone, at twice the on-screen resolution on a white
    // background (PNG or JPEG, chosen from the extension). Returns false if the
    // scene is empty or the file cannot be written.
    bool writeImage(const QString& path, QGraphicsScene* scene);

    // name made safe as a file name on every system: characters such as
    // \ / : * ? " < > | become "-", and it is never empty.
    QString safeFileName(const QString& name);

} // namespace ui::exporting
