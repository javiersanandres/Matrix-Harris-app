#include <gtest/gtest.h>

#include "OptionsHelp.h"

#include <QFile>
#include <set>

using namespace ui::help;

namespace {

    std::set<QString> topicIds() {
        std::set<QString> ids;
        for (const auto& t : allTopics()) ids.insert(t.id);
        return ids;
    }

} // namespace

// Every option of the boxes', the connections' and the background's menus
// (but Propiedades…) leads to an entry that exists.
TEST(OptionsHelp, EveryMenuOptionHasAnEntry) {
    const QStringList options = {
        // background
        "Nueva caja",
        // box menu (regular and joint diagrams)
        "Crear caja arriba", "Crear caja debajo", "Crear caja a la izquierda", "Crear caja a la derecha",
        "Caja «2» por debajo de", "Caja «Un nombre largo» por encima de", "Fusionar",
        "Mover bloque", "Mover este esquema", "Mover esquema «Esquema 1»",
        "Conexiones hipotéticas", "Eliminar conexión parcial", "Eliminar caja", "Eliminar bloque", "Quitar bloque",
        // connection menu
        "Crear caja entre", "Bifurcar arriba con caja nueva", "Bifurcar abajo con caja nueva",
        "Bifurcar arriba con caja existente", "Bifurcar abajo con caja existente",
        "Usar línea continua", "Usar línea discontinua", "Simplificar conexión", "Eliminar conexión",
        // copying and pasting, tabs
        "Copiar bloque", "Copiar esquema", "Copiar esquema «Esquema 1»", "Copiar este esquema", "Pegar",
        "Duplicar esquema",
        // equivalences (the box's menu and the line's)
        "Hacer equivalente a…", "Quitar equivalencia con «4»", "Quitar equivalencia",
    };
    const auto ids = topicIds();
    for (const QString& option : options) {
        const QString topic = topicForMenuEntry(option);
        EXPECT_FALSE(topic.isEmpty()) << option.toStdString();
        EXPECT_TRUE(ids.count(topic)) << option.toStdString() << " -> " << topic.toStdString();
    }
    EXPECT_EQ(topicForMenuEntry("Mover esquema «Esquema 1»"), "moveDiagram");
    EXPECT_EQ(topicForMenuEntry("Mover este esquema"), "moveDiagram");
    EXPECT_EQ(topicForMenuEntry("Mover bloque"), "moveBlock");
    EXPECT_EQ(topicForMenuEntry("Copiar esquema «Esquema 1»"), "copyPiece");
    EXPECT_EQ(topicForMenuEntry("Pegar"), "paste");
    EXPECT_EQ(topicForMenuEntry("Duplicar esquema"), "duplicateDiagram");
    EXPECT_EQ(topicForMenuEntry("Hacer equivalente a…"), "addEquivalence");
    EXPECT_EQ(topicForMenuEntry("Quitar esquema «Esquema 1»"), "removeDiagram");
    EXPECT_EQ(topicForMenuEntry("Quitar este esquema"), "removeDiagram");
    EXPECT_EQ(topicForMenuEntry("Quitar equivalencia con «Un nombre»"), "removeEquivalence");
    // The equivalence line's style entries say where they go (TOPIC_PROPERTY):
    // by their text they are the connections'.
    EXPECT_EQ(topicForMenuEntry("Usar línea discontinua"), "lineStyle");
    EXPECT_TRUE(topicIds().count("equivalenceStyle"));
    EXPECT_TRUE(topicForMenuEntry("Renombrar").isEmpty());
    EXPECT_TRUE(topicForMenuEntry("Propiedades…").isEmpty());
    EXPECT_TRUE(topicForMenuEntry("Crear").isEmpty()) << "section titles have no entry";
}

// The regrouping options keep the pictures they were given, in order.
TEST(OptionsHelp, EveryPictureIsEmbedded) {
    int pictures = 0;
    for (const auto& t : allTopics()) {
        EXPECT_FALSE(t.title.isEmpty());
        for (const QString& p : t.pictures) {
            EXPECT_TRUE(QFile::exists(p)) << t.id.toStdString() << ": " << p.toStdString();
            ++pictures;
        }
    }
    EXPECT_GT(pictures, 40);
}
