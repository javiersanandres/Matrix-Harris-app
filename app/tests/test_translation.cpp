#include <gtest/gtest.h>

#include "DiagramTabBar.h"
#include "DiagramTabWidget.h"
#include "MainWindow.h"
#include "OptionsHelp.h"
#include "Project.h"
#include "Text.h"
#include "Translation.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QLocale>
#include <QMenuBar>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QXmlStreamReader>

using app_logic::Project;
using ui::i18n::Language;

namespace {

    void ensureApplication() {
        if (QCoreApplication::instance()) return;
        static int argc = 1;
        static char name[] = "app_logic_tests";
        static char* argv[] = { name, nullptr };
        qputenv("QT_QPA_PLATFORM", "offscreen");
        static QApplication app(argc, argv);
    }

    // English for the test's span, Spanish again afterwards (the other tests
    // expect the texts as written).
    struct InEnglish {
        InEnglish() { ui::i18n::apply(Language::English); }
        ~InEnglish() { ui::i18n::apply(Language::Spanish); }
    };

    struct Message {
        QString source;
        QString translation;
        bool finished = false;
    };

    std::vector<Message> messages() {
        QFile file(QStringLiteral(TRANSLATIONS_TS));
        if (!file.open(QIODevice::ReadOnly)) return {};
        std::vector<Message> out;
        QXmlStreamReader xml(&file);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("message")) out.emplace_back();
            else if (xml.isStartElement() && xml.name() == QLatin1String("source")) out.back().source = xml.readElementText();
            else if (xml.isStartElement() && xml.name() == QLatin1String("translation")) {
                const QString type = xml.attributes().value(QLatin1String("type")).toString();
                out.back().translation = xml.readElementText();
                out.back().finished = type.isEmpty() && !out.back().translation.isEmpty();
            }
        }
        return out;
    }

    QStringList sorted(QStringList list) { list.sort(); return list; }

    // The windows a test opened, and their projects, gone (there is only
    // one project at a time).
    void closeWindows() {
        // Gathered first: a window takes its menus (top-level widgets too) with it.
        QList<QPointer<ui::MainWindow>> windows;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* window = qobject_cast<ui::MainWindow*>(w)) windows << window;
        for (const auto& window : windows) delete window.data(); // closing would ask to save
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    QStringList matches(const QString& text, const QString& pattern) {
        QStringList out;
        for (const auto& m : QRegularExpression(pattern).globalMatch(text)) out << m.captured(0);
        return sorted(out);
    }

    QAction* menuNamed(QMainWindow& window, const QString& title) {
        for (QAction* a : window.menuBar()->actions())
            if (a->text() == title) return a;
        return nullptr;
    }

} // namespace

// Every text has its English, which keeps the source's %1, %2… and HTML tags
// (after adding texts: update_translations, then fill in the new ones).
TEST(Translation, EveryTextIsTranslated) {
    const std::vector<Message> all = messages();
    ASSERT_GT(all.size(), 500u);
    for (const Message& m : all) {
        EXPECT_TRUE(m.finished) << "Untranslated: " << m.source.toStdString();
        if (!m.finished) continue;
        EXPECT_EQ(matches(m.source, QStringLiteral("%\\d")), matches(m.translation, QStringLiteral("%\\d")))
            << m.source.toStdString();
        EXPECT_EQ(matches(m.source, QStringLiteral("</?\\w+")), matches(m.translation, QStringLiteral("</?\\w+")))
            << m.source.toStdString();
    }
}

// Spanish for Spanish and the languages close to it, English for the rest.
TEST(Translation, FirstLanguageFollowsTheSystem) {
    for (auto l : { QLocale::Spanish, QLocale::Catalan, QLocale::Galician, QLocale::Basque,
                    QLocale::Portuguese, QLocale::Italian })
        EXPECT_EQ(ui::i18n::defaultFor(QLocale(l)), Language::Spanish) << QLocale::languageToString(l).toStdString();
    for (auto l : { QLocale::English, QLocale::German, QLocale::French, QLocale::Chinese, QLocale::Japanese })
        EXPECT_EQ(ui::i18n::defaultFor(QLocale(l)), Language::English) << QLocale::languageToString(l).toStdString();
}

// The choice is saved, and read back before the system's default.
TEST(Translation, TheChoiceIsSaved) {
    ensureApplication();
    QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    ui::i18n::save(settings, Language::English);
    EXPECT_EQ(ui::i18n::saved(settings), Language::English);
    ui::i18n::save(settings, Language::Spanish);
    EXPECT_EQ(ui::i18n::saved(settings), Language::Spanish);
}

// In English the interface's, the logic's and Qt's texts change; back in
// Spanish they are as written.
TEST(Translation, ApplyingALanguageChangesEveryText) {
    ensureApplication();
    {
        InEnglish english;
        EXPECT_EQ(Tr::tr("Nuevo proyecto"), QStringLiteral("New Project"));
        EXPECT_EQ(hypergraph_logic::text(LogicText::tr("Esquema nuevo")), "New diagram");
        EXPECT_EQ(hypergraph_logic::text(LogicText::tr("eliminar «%1»"), "A"), "delete “A”");
        // The undo labels are built when shown, so they follow the language.
        Project project("P");
        project.addDiagram();
        EXPECT_EQ(project.projectUndoLabel(), "add “New diagram”");
    }
    EXPECT_EQ(Tr::tr("Nuevo proyecto"), QStringLiteral("Nuevo proyecto"));
    EXPECT_EQ(hypergraph_logic::text(LogicText::tr("Esquema nuevo")), "Esquema nuevo");
}

// The help still finds the topic of an entry by its text in English.
TEST(Translation, MenuEntriesFindTheirHelpInEnglish) {
    ensureApplication();
    InEnglish english;
    EXPECT_EQ(ui::help::topicForMenuEntry(QStringLiteral("Merge")), QStringLiteral("fuse"));
    EXPECT_EQ(ui::help::topicForMenuEntry(QStringLiteral("Place “2” Below")), QStringLiteral("addChildConnection"));
    EXPECT_EQ(ui::help::topicForMenuEntry(QStringLiteral("Move Diagram “Diagram 1”")), QStringLiteral("moveDiagram"));
    EXPECT_EQ(ui::help::topicForMenuEntry(QStringLiteral("Set Equivalence Between…")), QStringLiteral("addEquivalence"));
}

// A picture without an English version is the Spanish one.
TEST(Translation, PicturesFallBackToSpanish) {
    ensureApplication();
    const QString path = QStringLiteral("information/fuse/01.png");
    EXPECT_EQ(ui::i18n::picture(path), QStringLiteral(":/es/information/fuse/01.png"));
    InEnglish english;
    const QString shown = ui::i18n::picture(path);
    EXPECT_TRUE(QFile::exists(shown)) << shown.toStdString();
}

// Changing the language hands the project, history included, to a new window
// whose menus are in the new language.
TEST(Translation, SwitchingKeepsTheProject) {
    ensureApplication();
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    Project project("P");
    project.getEditor(0).setName("A");
    project.addDiagram();
    project.getEditor(1).setName("B");
    auto* window = new ui::MainWindow(std::move(project));
    window->setAttribute(Qt::WA_DeleteOnClose);
    QPointer<ui::MainWindow> old = window;
    ui::MainWindow* next = nullptr;
    QObject::connect(window, &ui::MainWindow::replaced, [&next](ui::MainWindow* w) { next = w; });
    ASSERT_TRUE(menuNamed(*window, QStringLiteral("Archivo")));

    window->switchLanguage(Language::English);
    ASSERT_TRUE(next);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(old.isNull()); // closed and gone
    EXPECT_EQ(ui::i18n::language(), Language::English);
    EXPECT_TRUE(menuNamed(*next, QStringLiteral("File")));
    auto* bar = next->findChild<ui::DiagramTabBar*>();
    ASSERT_TRUE(bar);
    EXPECT_EQ(bar->tabCount(), 2);
    // The project's history came along: adding B can be undone.
    bool found = false;
    for (QAction* a : next->findChildren<QAction*>())
        if (a->text() == QStringLiteral("Undo: add “B”")) found = true;
    EXPECT_TRUE(found);
    QSettings saved;
    EXPECT_EQ(ui::i18n::saved(saved), Language::English);

    next->switchLanguage(Language::Spanish);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_EQ(ui::i18n::language(), Language::Spanish);
    closeWindows();
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}

// Switching, a diagram still called by the default name takes the new
// language's, whatever it holds and outside its history; a name the user gave
// stays.
TEST(Translation, DefaultDiagramNamesFollowTheLanguage) {
    ensureApplication();
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    Project project("P");
    project.getEditor(0).setName("Esquema nuevo");
    project.getEditor(0).createNode(hypergraph_logic::NodeAttributes("1"), 0, -1, nullptr);
    project.addDiagram(); // "Esquema nuevo", empty
    project.addDiagram();
    project.getEditor(2).setName("Mi esquema");
    auto* window = new ui::MainWindow(std::move(project));
    window->setAttribute(Qt::WA_DeleteOnClose);
    ui::MainWindow* next = nullptr;
    QObject::connect(window, &ui::MainWindow::replaced, [&next](ui::MainWindow* w) { next = w; });
    window->switchLanguage(Language::English);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_TRUE(next);
    auto* bar = next->findChild<ui::DiagramTabBar*>();
    ASSERT_TRUE(bar);
    EXPECT_EQ(bar->tabAt(0)->displayName(), QStringLiteral("New diagram"));
    EXPECT_EQ(bar->tabAt(1)->displayName(), QStringLiteral("New diagram"));
    EXPECT_EQ(bar->tabAt(2)->displayName(), QStringLiteral("Mi esquema"));

    next->switchLanguage(Language::Spanish);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    closeWindows();
    QSettings::setDefaultFormat(QSettings::NativeFormat);
}

// A click on an empty diagram's miniature selects the tab; on one with
// something to pan, it is the miniature's.
TEST(Translation, ClickingAnEmptyMiniatureSelectsTheTab) {
    ensureApplication();
    QGraphicsScene scene;
    ui::DiagramTabWidget tab(&scene, QStringLiteral("A"), false);
    tab.show();
    int clicks = 0;
    QObject::connect(&tab, &ui::DiagramTabWidget::clicked, [&clicks] { ++clicks; });
    auto* view = qobject_cast<QGraphicsView*>(tab.miniature());
    ASSERT_TRUE(view);
    auto press = [view] {
        const QPointF at(view->viewport()->rect().center());
        QMouseEvent event(QEvent::MouseButtonPress, at, view->viewport()->mapToGlobal(at),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QMouseEvent release(QEvent::MouseButtonRelease, at, view->viewport()->mapToGlobal(at),
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &release);
    };
    press();
    EXPECT_EQ(clicks, 1);
    scene.addRect(0, 0, 100, 50);
    press();
    EXPECT_EQ(clicks, 1);
}
