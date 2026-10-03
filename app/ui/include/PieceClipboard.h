#pragma once

#include "GraphicalHypergraph.h"

#include <QString>
#include <optional>

namespace ui::clipboard {

    // ============================================================================
    // Copied pieces of diagram on the system clipboard
    //
    // A copied block or diagram (GraphicalHypergraph::copyOf) goes on the system
    // clipboard as JSON under a type of the application's own (MIME_TYPE), so
    // any running instance of the application can paste it, also after the one
    // that copied it is closed. Copying anything else in any program replaces
    // it, as usual. Other programs ignore it: it is not text.
    //
    // The JSON is { "format": FORMAT, "version": VERSION, "description": <text>,
    // "boxes": <count>, "graph": <GraphicalHypergraph JSON> }. A copy in another
    // format or version, or one that cannot be read, counts as nothing to paste.
    // ============================================================================

    inline constexpr const char* MIME_TYPE = "application/x-matrix-harris-piece";
    inline constexpr const char* FORMAT = "matrix-harris-piece";
    inline constexpr int VERSION = 1;

    // Puts piece on the clipboard. description says what it is ("el bloque de
    // «1»", "el esquema «Esquema 1»"), for the paste entry's tooltip.
    void copy(const hypergraph_logic::GraphicalHypergraph& piece, const QString& description);

    // The piece on the clipboard, read back as a new graph each time (so it can
    // be pasted again and again), or nullopt when there is none to paste.
    std::optional<hypergraph_logic::GraphicalHypergraph> piece();

    // What the copy on the clipboard is and how many boxes it has, or an empty
    // string when there is none to paste.
    QString description();

    bool hasPiece();

} // namespace ui::clipboard
