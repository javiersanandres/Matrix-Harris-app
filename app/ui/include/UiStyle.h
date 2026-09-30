#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QMenu;
class QWidget;

namespace ui::style {

    // ============================================================================
    // UiStyle
    //
    // Look shared by the editor chrome (context menus, the floating minimize
    // panel, the tab bar and the selection highlights), in the same indigo
    // palette as the node dialogs.
    // ============================================================================

    namespace palette {
        inline const QColor ink{ 0x1F, 0x23, 0x30 };        // main text
        inline const QColor ink_soft{ 0x4B, 0x50, 0x68 };   // icon strokes, secondary text
        inline const QColor muted{ 0x8A, 0x90, 0xA2 };      // hints, section titles
        inline const QColor faint{ 0xA0, 0xA5, 0xB5 };      // disabled / idle glyphs
        inline const QColor line{ 0xE3, 0xE6, 0xEF };       // card borders
        inline const QColor surface{ 0xF5, 0xF6, 0xFA };    // app chrome background
        inline const QColor accent{ 0x63, 0x66, 0xF1 };     // indigo
        inline const QColor accent_dark{ 0x4F, 0x46, 0xE5 };
        inline const QColor accent_soft{ 0xEE, 0xF0, 0xFF };
        inline const QColor violet{ 0x8B, 0x5C, 0xF6 };
        inline const QColor amber{ 0xF5, 0x9E, 0x0B };      // "origin" of a selection, fast mode
        inline const QColor green{ 0x2F, 0x9E, 0x6E };      // slow mode
        inline const QColor danger{ 0xE5, 0x48, 0x4D };
    }

    // Every icon used by the editor chrome, drawn with QPainter so they stay
    // crisp at any size and screen scale.
    enum class Icon {
        Properties,
        BoxAbove, BoxBelow, BoxLeft, BoxRight,
        CurrentBelow,      // "Caja actual por debajo de": pick the box above
        CurrentAbove,      // "Caja actual por encima de": pick the box below
        RemovePartial,     // "Eliminar conexión parcial"
        RemoveBox,
        Fuse,
        BoxBetween,
        ForkUpNew, ForkDownNew,
        ForkUpExisting, ForkDownExisting,
        Simplify,
        RemoveConnection,
        NewBox,
        Fast,              // lightning bolt
        Slow,              // turtle
        Joint,             // several diagrams joined
        Trash,
        PdfDocument,       // a page with a red "PDF" label
        ImageFolder,       // a folder holding a picture
        LineSolid,         // two boxes joined by a continuous line
        LineDashed,        // two boxes joined by a discontinuous line
        Pause,             // two rounded bars
        Move,              // a piece of diagram with a four-way arrow
        TakeOut,           // a piece of diagram leaving through a dashed frame
    };

    QIcon icon(Icon which);

    // A styled popup menu: rounded white card, generous items, soft hover.
    QMenu* createMenu(QWidget* parent = nullptr);

    // Applies the same look to an existing menu (e.g. the menu bar's menus).
    void styleMenu(QMenu* menu);

    // Non-interactive header at the top of a context menu: a picture (e.g. the
    // node itself), a bold title and a muted subtitle.
    void addMenuHeader(QMenu* menu, const QPixmap& picture, const QString& title,
        const QString& subtitle);

    // Small upper-case label that opens a group of related entries.
    void addMenuSection(QMenu* menu, const QString& label);

    // Linear interpolation between two colours (t in [0, 1]).
    QColor mix(const QColor& a, const QColor& b, double t);

} // namespace ui::style
