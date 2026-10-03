#include "HypergraphEditor.h"
#include "PieceClipboard.h"
#include "Project.h"
#include <gtest/gtest.h>

#include <QApplication>
#include <QClipboard>
#include <QMimeData>

#include <limits>
#include <string>

// ============================================================================
// Copying, pasting and duplicating diagrams
//
// Project: a duplicated diagram is a new, independent diagram (another ID, so
// the joint takes both). Editor: pasting is one undoable step. Clipboard: a
// copy goes through the system clipboard as JSON and is read back as a new
// graph every time; anything else on the clipboard is nothing to paste.
// ============================================================================

namespace app_logic {
    namespace copy_paste_tests {

        constexpr double kFarRight = std::numeric_limits<double>::infinity();

        // The clipboard needs an application (offscreen: the test never touches
        // the real system clipboard).
        void ensureApplication() {
            if (QCoreApplication::instance()) return;
            static int argc = 1;
            static char name[] = "app_logic_tests";
            static char* argv[] = { name, nullptr };
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static QApplication app(argc, argv);
        }

        NodePtr named(const GraphicalHypergraph& g, const std::string& name) {
            for (const auto& n : g.getAllNodes())
                if (!n->isDummy() && n->getName() == name) return n;
            return nullptr;
        }

        int realBoxes(const GraphicalHypergraph& g) {
            int count = 0;
            for (const auto& n : g.getAllNodes()) count += !n->isDummy();
            return count;
        }

        // 1 -> 2, and 3 -> 4 beside it.
        void fill(HypergraphEditor& ed) {
            auto n1 = ed.createNode(NodeAttributes("1"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("2"), 1, -1, n1);
            auto n3 = ed.createNode(NodeAttributes("3"), 0, -1, nullptr);
            ed.createNode(NodeAttributes("4"), 1, -1, n3);
        }

        // ── Duplicating a diagram ───────────────────────────────────────────

        TEST(DuplicateDiagram, IsANewDiagramWithTheSameDrawing) {
            Project p("P");
            p.getEditor(0).setName("Esquema 1");
            fill(p.getEditor(0));

            const int copy = p.duplicateDiagram(0);
            EXPECT_EQ(copy, 1);
            EXPECT_EQ(p.getActiveIndex(), 1);
            EXPECT_EQ(p.getDiagramName(1), "Esquema 1 (copia)");
            EXPECT_NE(p.getEditor(1).getId(), p.getEditor(0).getId());
            EXPECT_FALSE(p.getEditor(1).canUndo()) << "its history starts empty";

            const auto& a = p.getEditor(0).getGraph();
            const auto& b = p.getEditor(1).getGraph();
            EXPECT_EQ(realBoxes(b), 4);
            EXPECT_DOUBLE_EQ(b.getNodeLayout().at(named(b, "4").get()).x,
                             a.getNodeLayout().at(named(a, "4").get()).x);

            // Independent: changing the copy leaves the original alone.
            p.getEditor(1).removeNode(named(b, "4"));
            EXPECT_EQ(realBoxes(p.getEditor(0).getGraph()), 4);
        }

        TEST(DuplicateDiagram, NamesAreNotRepeatedAndTheJointTakesBoth) {
            Project p("P");
            p.getEditor(0).setName("E");
            fill(p.getEditor(0));
            p.duplicateDiagram(0);
            p.duplicateDiagram(0);
            EXPECT_EQ(p.getDiagramName(1), "E (copia)");
            EXPECT_EQ(p.getDiagramName(2), "E (copia 2)");

            auto& joint = p.getJointEditor();
            for (int i = 0; i < 3; ++i)
                EXPECT_NO_THROW(joint.addHypergraph(const_cast<GraphicalHypergraph&>(p.getEditor(i).getGraph()), kFarRight));
        }

        // ── Pasting ─────────────────────────────────────────────────────────

        TEST(Paste, IsOneUndoableStep) {
            Project p("P");
            auto& ed = p.getEditor(0);
            fill(ed);
            const auto& g = ed.getGraph();
            auto piece = g.copyOf(g.getComponentNodes(named(g, "3").get()));

            ed.paste(std::move(piece), kFarRight, 0);
            EXPECT_EQ(realBoxes(ed.getGraph()), 6);
            EXPECT_EQ(ed.getGraph().getBlocks().size(), 3u);

            ed.undo();
            EXPECT_EQ(realBoxes(ed.getGraph()), 4);
            ed.redo();
            EXPECT_EQ(realBoxes(ed.getGraph()), 6);
        }

        // ── The clipboard ───────────────────────────────────────────────────

        TEST(PieceClipboard, ACopyIsReadBackAsANewGraphEveryTime) {
            ensureApplication();
            Project p("P");
            fill(p.getEditor(0));
            const auto& g = p.getEditor(0).getGraph();
            ui::clipboard::copy(g.copyOf(g.getComponentNodes(named(g, "1").get())), QStringLiteral("el bloque de «1»"));

            ASSERT_TRUE(ui::clipboard::hasPiece());
            EXPECT_EQ(ui::clipboard::description(), QStringLiteral("el bloque de «1» (2 cajas)"));
            auto first = ui::clipboard::piece();
            auto second = ui::clipboard::piece();
            ASSERT_TRUE(first && second);
            EXPECT_EQ(realBoxes(*first), 2);
            EXPECT_NE(named(*first, "1"), named(*second, "1")) << "boxes of their own each time";
        }

        TEST(PieceClipboard, AnythingElseIsNothingToPaste) {
            ensureApplication();
            QGuiApplication::clipboard()->setText(QStringLiteral("un texto"));
            EXPECT_FALSE(ui::clipboard::hasPiece());
            EXPECT_TRUE(ui::clipboard::description().isEmpty());

            // The application's type, but another version of the format.
            auto* data = new QMimeData;
            data->setData(QString::fromLatin1(ui::clipboard::MIME_TYPE),
                          QByteArray(R"({"format":"matrix-harris-piece","version":999,"graph":{}})"));
            QGuiApplication::clipboard()->setMimeData(data);
            EXPECT_FALSE(ui::clipboard::hasPiece());

            // Or something that is not even JSON.
            data = new QMimeData;
            data->setData(QString::fromLatin1(ui::clipboard::MIME_TYPE), QByteArray("{ roto"));
            QGuiApplication::clipboard()->setMimeData(data);
            EXPECT_FALSE(ui::clipboard::hasPiece());
        }

    } // namespace copy_paste_tests
} // namespace app_logic
