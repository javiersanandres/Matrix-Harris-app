#pragma once

#include <QString>
#include <QStringList>

#include <vector>

class QMenu;
class QWidget;

namespace ui::help {

    // ============================================================================
    // Options help
    //
    // One window explaining every option of the boxes', the connections' and
    // the background's menus, laid out like an options window:
    //
    //   ┌──────────────────────────────────────────────────────────────────┐
    //   │ (?) Ayuda de las opciones                    [ Buscar…    ]  ×  │
    //   │     Qué hace cada opción de los menús…                          │
    //   │ ┌───────────────────┐ ┌────────────────────────────────────────┐ │
    //   │ │ FONDO DEL ESQUEMA │ │ [icon] Option                          │ │
    //   │ │  Nueva caja       │ │ What it does, in one line              │ │
    //   │ │ MENÚ DE LA CAJA   │ │ CÓMO SE USA                            │ │
    //   │ │ ▌Crear caja arriba│ │ Text…                                  │ │
    //   │ │  …                │ │ [ before ]  →  [ after ]               │ │
    //   │ │ (scrolls)         │ │ (scrolls)                              │ │
    //   │ └───────────────────┘ └────────────────────────────────────────┘ │
    //   └──────────────────────────────────────────────────────────────────┘
    //
    // Every option of those menus carries a small "?" button that opens this
    // window on its entry (see addHelpButtons). The pictures are embedded from
    // app/ui/resources/information/<topic>/NN.png; clicking one shows it
    // full screen.
    // ============================================================================

    // Opens the window, on the entry `topic` (or the first one).
    void showOptionsHelp(QWidget* parent, const QString& topic = {});

    // The entry explaining a menu option, by the option's text (e.g. "Crear
    // caja arriba", "Caja «2» por debajo de"); empty for options without one
    // (Propiedades…).
    QString topicForMenuEntry(const QString& entry_text);

    // Puts a "?" button at the right of every option of `menu` that has an
    // entry; clicking it closes the menu and opens the window there. Call
    // before showing the menu (it makes the menu a little wider for them).
    void addHelpButtons(QMenu* menu, QWidget* dialog_parent);

    // ── For tests ─────────────────────────────────────────────────────────────
    struct TopicInfo {
        QString id;
        QString title;
        QStringList pictures; // resource paths
    };
    std::vector<TopicInfo> allTopics();

} // namespace ui::help
