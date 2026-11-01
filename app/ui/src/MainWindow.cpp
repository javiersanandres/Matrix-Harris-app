#include "MainWindow.h"
#include "Translation.h"
#include "AppDialogs.h"
#include "NodeDialogs.h"
#include "NodeVisuals.h"
#include "MinimizingProgressDialog.h"
#include "HelpButton.h"
#include "UiStyle.h"
#include "ViewOverlays.h"
#include "DiagramExport.h"
#include "GurobiGuide.h"
#include "DiagramTabWidget.h"
#include "HelpNotifier.h"
#include "OptionsHelp.h"
#include "Tutorial.h"
#include "PieceClipboard.h"
#include "ProjectSettingsDialog.h"
#include "LanguageButton.h"
#include "NativeWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCursor>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QStandardPaths>
#include <QUrl>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QPainter>
#include <QPdfWriter>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QWindow>
#include <QElapsedTimer>
#include <QVBoxLayout>
#include <QWidget>

using namespace app_logic;
using namespace hypergraph_logic;

namespace ui {

    namespace {
        constexpr int MAX_RECENT_PROJECTS = 8;
        const char* const RECENT_KEY = "recentProjects";
        QString projectFilter() { return Tr::tr("Proyectos de Taller Matrix Harris (*.json)"); }

        // "1 caja" / "3 cajas".
        QString count(qsizetype n, const QString& one, const QString& many) {
            return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
        }
    }

    // ============================================================================
    // Construction
    // ============================================================================

    MainWindow::MainWindow(QWidget* parent)
        : MainWindow(Project(Tr::tr("Nuevo proyecto").toStdString(), app_defaults::load()), parent)
    {
    }

    MainWindow::MainWindow(Project&& project, QWidget* parent)
        : MainWindow(std::make_unique<Project>(std::move(project)), parent)
    {
    }

    MainWindow::MainWindow(std::unique_ptr<Project> project, QWidget* parent)
        : QMainWindow(parent)
        , project_(std::move(project))
    {
        setAcceptDrops(true);
        setupMenuBar();
        setupCentralArea();
        setupStatusBar();
        buildFromProject();
        updateWindowTitle();
    }

    MainWindow::~MainWindow() {
        // The scenes show the project's editors: they go first (children are
        // deleted after the members). A window that handed its project over
        // (see switchLanguage) has nothing left to tell its tabs.
        tab_bar_->blockSignals(true);
        central_view_->setScene(nullptr);
        for (auto* s : scenes_) delete s;
        scenes_.clear();
        delete joint_scene_;
        joint_scene_ = nullptr;
    }

    // ============================================================================
    // Setup
    // ============================================================================

    void MainWindow::setupMenuBar() {
        menuBar()->setStyleSheet(QStringLiteral(R"(
QMenuBar { background: #FFFFFF; border-bottom: 1px solid #E3E6EF; padding: 2px 6px; }
QMenuBar::item { padding: 5px 11px; border-radius: 6px; color: #1F2330; background: transparent; }
QMenuBar::item:selected { background: #EEF0FF; color: #312E81; }
QMenuBar::item:pressed { background: #E2E4FF; color: #312E81; }
)"));

        auto addMenu = [this](const QString& title) {
            QMenu* m = style::createMenu(this);
            m->setTitle(title);
            m->setToolTipsVisible(true);
            menuBar()->addMenu(m);
            return m;
        };
        auto add = [](QMenu* m, const QString& text, const QKeySequence& keys, auto slot_owner, auto slot) {
            QAction* a = m->addAction(text);
            if (!keys.isEmpty()) a->setShortcut(keys);
            QObject::connect(a, &QAction::triggered, slot_owner, slot);
            return a;
        };

        // ── Archivo ──────────────────────────────────────────────────────────────
        QMenu* archivo = addMenu(Tr::tr("Archivo"));
        add(archivo, Tr::tr("Nuevo proyecto"), QKeySequence::New, this, &MainWindow::onNuevoProyecto);
        add(archivo, Tr::tr("Abrir proyecto…"), QKeySequence::Open, this, &MainWindow::onAbrirProyecto);
        recent_menu_ = style::createMenu(archivo);
        recent_menu_->setTitle(Tr::tr("Abrir reciente"));
        recent_menu_->setToolTipsVisible(true);
        archivo->addMenu(recent_menu_);
        connect(recent_menu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
        archivo->addSeparator();
        add(archivo, Tr::tr("Guardar"), QKeySequence::Save, this, &MainWindow::onGuardarProyecto);
        add(archivo, Tr::tr("Guardar como…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")),
            this, &MainWindow::onGuardarComo);
        archivo->addSeparator();
        add(archivo, Tr::tr("Exportar esquema como imagen…"), QKeySequence(QStringLiteral("Ctrl+E")),
            this, &MainWindow::onExportarImagen);
        add(archivo, Tr::tr("Exportar proyecto…"), QKeySequence(QStringLiteral("Ctrl+Shift+E")),
            this, &MainWindow::onExportarProyecto);
        archivo->addSeparator();
        add(archivo, Tr::tr("Salir"), QKeySequence(QStringLiteral("Ctrl+Q")), this, &MainWindow::onSalir);

        // ── Editar ────────────────────────────────────────────────────────────────
        QMenu* editar = addMenu(Tr::tr("Editar"));
        action_deshacer_ = add(editar, Tr::tr("Deshacer"), QKeySequence::Undo, this, &MainWindow::onDeshacer);
        action_deshacer_->setEnabled(false);
        action_rehacer_ = add(editar, Tr::tr("Rehacer"), QKeySequence(QStringLiteral("Ctrl+Y")),
            this, &MainWindow::onRehacer);
        action_rehacer_->setShortcuts({ QKeySequence(QStringLiteral("Ctrl+Y")), QKeySequence(QStringLiteral("Ctrl+Shift+Z")) });
        action_rehacer_->setEnabled(false);
        editar->addSeparator();
        QAction* copiar = add(editar, Tr::tr("Copiar"), QKeySequence::Copy, this, &MainWindow::onCopiar);
        copiar->setIcon(style::icon(style::Icon::Copy));
        copiar->setToolTip(Tr::tr("Copia el bloque que está bajo el ratón, o el esquema entero"));
        action_pegar_ = add(editar, Tr::tr("Pegar"), QKeySequence::Paste, this, &MainWindow::onPegar);
        action_pegar_->setIcon(style::icon(style::Icon::Paste));
        action_pegar_->setToolTip(Tr::tr("Pega lo copiado donde está el ratón"));
        action_pegar_->setEnabled(false);
        // Also when something is copied in another window or program.
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::updatePasteAction);

        // ── Ver ───────────────────────────────────────────────────────────────────
        QMenu* ver = addMenu(Tr::tr("Ver"));
        QAction* acercar = add(ver, Tr::tr("Acercar"), QKeySequence::ZoomIn, this, &MainWindow::onAcercar);
        acercar->setShortcuts({ QKeySequence(QStringLiteral("Ctrl++")), QKeySequence(QStringLiteral("Ctrl+=")) });
        add(ver, Tr::tr("Alejar"), QKeySequence::ZoomOut, this, &MainWindow::onAlejar);
        add(ver, Tr::tr("Ajustar a la ventana"), QKeySequence(QStringLiteral("Ctrl+0")),
            this, &MainWindow::onAjustarVentana);
        add(ver, Tr::tr("Tamaño real (100 %)"), QKeySequence(QStringLiteral("Ctrl+1")),
            this, &MainWindow::onTamanoReal);
        ver->addSeparator();
        action_panel_ = ver->addAction(Tr::tr("Panel «Minimizar cruces»"));
        action_panel_->setCheckable(true);
        action_panel_->setChecked(true);
        connect(action_panel_, &QAction::toggled, this, [this](bool on) { minimize_panel_->setVisible(on); });
        QAction* barra = ver->addAction(Tr::tr("Barra de estado"));
        barra->setCheckable(true);
        barra->setChecked(true);
        connect(barra, &QAction::toggled, this, [this](bool on) { statusBar()->setVisible(on); });
        ver->addSeparator();
        action_pantalla_completa_ = ver->addAction(Tr::tr("Pantalla completa"));
        action_pantalla_completa_->setShortcut(QKeySequence(QStringLiteral("F11")));
        action_pantalla_completa_->setCheckable(true);
        connect(action_pantalla_completa_, &QAction::toggled, this, &MainWindow::onPantallaCompleta);

        // ── Esquema ───────────────────────────────────────────────────────────────
        QMenu* esquema = addMenu(Tr::tr("Esquema"));
        QAction* nuevo = add(esquema, Tr::tr("Nuevo esquema"), QKeySequence(QStringLiteral("Ctrl+T")),
            this, &MainWindow::onNuevoDiagrama);
        nuevo->setIcon(style::icon(style::Icon::NewBox));
        action_renombrar_ = add(esquema, Tr::tr("Renombrar esquema"), QKeySequence(QStringLiteral("F2")),
            this, &MainWindow::onRenombrarEsquema);
        action_eliminar_esquema_ = add(esquema, Tr::tr("Eliminar esquema"), QKeySequence(),
            this, &MainWindow::onEliminarEsquemaActivo);
        action_eliminar_esquema_->setIcon(style::icon(style::Icon::RemoveBox));
        action_duplicar_esquema_ = add(esquema, Tr::tr("Duplicar esquema"), QKeySequence(QStringLiteral("Ctrl+D")),
            this, [this] { duplicateDiagram(project_->getActiveIndex()); });
        action_duplicar_esquema_->setIcon(style::icon(style::Icon::Duplicate));
        esquema->addSeparator();
        add(esquema, Tr::tr("Esquema siguiente"), QKeySequence(QStringLiteral("Ctrl+Tab")),
            this, &MainWindow::onEsquemaSiguiente);
        add(esquema, Tr::tr("Esquema anterior"), QKeySequence(QStringLiteral("Ctrl+Shift+Tab")),
            this, &MainWindow::onEsquemaAnterior);
        QAction* conjunto = add(esquema, Tr::tr("Ir al esquema conjunto"), QKeySequence(QStringLiteral("Ctrl+J")),
            this, &MainWindow::onJointTabClicked);
        conjunto->setIcon(style::icon(style::Icon::Joint));
        esquema->addSeparator();
        add(esquema, Tr::tr("Minimizar cruces"), QKeySequence(QStringLiteral("Ctrl+M")),
            this, &MainWindow::onMinimizeCrossings);
        auto* modes = new QActionGroup(this);
        // No icons here: the check mark is what shows the current mode.
        action_menu_fast_ = esquema->addAction(Tr::tr("Modo rápido"));
        action_menu_slow_ = esquema->addAction(Tr::tr("Modo lento"));
        for (QAction* a : { action_menu_fast_, action_menu_slow_ }) {
            a->setCheckable(true);
            modes->addAction(a);
        }
        action_menu_fast_->setChecked(true);
        connect(action_menu_fast_, &QAction::triggered, this, [this] { mode_fast_btn_->setChecked(true); });
        connect(action_menu_slow_, &QAction::triggered, this, [this] { mode_slow_btn_->setChecked(true); });

        // ── Proyecto ──────────────────────────────────────────────────────────────
        QMenu* proyecto = addMenu(Tr::tr("Proyecto"));
        action_deshacer_proyecto_ = add(proyecto, Tr::tr("Deshacer"), QKeySequence(QStringLiteral("Ctrl+Alt+Z")),
            this, [this] { onProjectHistory(true); });
        action_deshacer_proyecto_->setIcon(style::icon(style::Icon::ProjectUndo));
        action_deshacer_proyecto_->setToolTip(Tr::tr("Deshace el último cambio del proyecto: añadir, duplicar o eliminar un esquema, o cambiar los ajustes"));
        action_rehacer_proyecto_ = add(proyecto, Tr::tr("Rehacer"), QKeySequence(QStringLiteral("Ctrl+Alt+Y")),
            this, [this] { onProjectHistory(false); });
        action_rehacer_proyecto_->setToolTip(Tr::tr("Rehace el último cambio del proyecto deshecho"));
        action_deshacer_proyecto_->setEnabled(false);
        action_rehacer_proyecto_->setEnabled(false);
        proyecto->addSeparator();
        QAction* ajustes = add(proyecto, Tr::tr("Ajustes del proyecto…"), QKeySequence(QStringLiteral("Ctrl+Alt+S")),
            this, &MainWindow::onAjustesProyecto);
        ajustes->setIcon(style::icon(style::Icon::Settings));
        ajustes->setToolTip(Tr::tr("Lo que este proyecto decide para todos sus esquemas: cajas nuevas, "
                                           "equivalencias nuevas, distancias y tamaños por defecto"));
        QAction* predeterminados = add(proyecto, Tr::tr("Aplicar los ajustes predeterminados de la aplicación"),
            QKeySequence(), this, &MainWindow::onAplicarPredeterminados);
        predeterminados->setToolTip(Tr::tr("El proyecto toma los ajustes con los que empiezan los proyectos nuevos"));

        // ── Ayuda ─────────────────────────────────────────────────────────────────
        QMenu* ayuda = addMenu(Tr::tr("Ayuda"));
        QAction* tutorial = add(ayuda, Tr::tr("Tutorial de introducción"), QKeySequence(), this,
            [this] { runTutorial(false); });
        add(ayuda, Tr::tr("Atajos y controles"), QKeySequence(QStringLiteral("F1")), this, &MainWindow::onAtajos);
        add(ayuda, Tr::tr("Ayuda de las opciones"), QKeySequence(), this,
            [this] { help::showOptionsHelp(this); });
        ayuda->addSeparator();
        QAction* install = add(ayuda, Tr::tr("Cómo instalar Gurobi"), QKeySequence(), this,
            [this] { gurobi::showGuide(gurobi::Guide::Install, this); });
        QAction* license = add(ayuda, Tr::tr("Cómo obtener una licencia de Gurobi"), QKeySequence(), this,
            [this] { gurobi::showGuide(gurobi::Guide::License, this); });
        ayuda->addSeparator();
        // Shown once there is something to check with (setUpdateChecker).
        action_actualizaciones_ = add(ayuda, Tr::tr("Buscar actualizaciones…"), QKeySequence(), this,
            [this] { if (update_checker_) update_checker_(); });
        action_actualizaciones_->setVisible(false);
        add(ayuda, Tr::tr("Acerca de Taller Matrix Harris"), QKeySequence(), this, &MainWindow::onAcercaDe);

        help_notifier_ = new HelpNotifier(menuBar(), ayuda, this);
        help_notifier_->addEntry(QStringLiteral("tutorial"), tutorial);
        help_notifier_->addEntry(QStringLiteral("gurobiInstall"), install);
        help_notifier_->addEntry(QStringLiteral("gurobiLicense"), license);

        // ── Language ──────────────────────────────────────────────────────────
        // Always in sight at the end of the menu bar, for whoever does not
        // read the current language.
        auto* corner = new QWidget(menuBar());
        auto* corner_layout = new QHBoxLayout(corner);
        corner_layout->setContentsMargins(0, 2, 8, 2);
        auto* language = new LanguageButton(corner);
        corner_layout->addWidget(language);
        connect(language, &LanguageButton::languageChosen, this,
            [this](i18n::Language chosen) { switchLanguage(chosen); });
        menuBar()->setCornerWidget(corner, Qt::TopRightCorner);
    }

    // ============================================================================
    // Pointing to Help, introductory tour
    // ============================================================================

    void MainWindow::pointToHelpEntry(HelpEntry entry) {
        switch (entry) {
        case HelpEntry::Tutorial:      help_notifier_->flag(QStringLiteral("tutorial")); break;
        case HelpEntry::GurobiInstall: help_notifier_->flag(QStringLiteral("gurobiInstall")); break;
        case HelpEntry::GurobiLicense: help_notifier_->flag(QStringLiteral("gurobiLicense")); break;
        }
    }

    void MainWindow::startTutorialIfFirstRun(bool force) {
        QSettings settings;
        if (!force && !tutorial::pendingAtStartup(settings)) return;
        // As soon as the window is on screen.
        QTimer::singleShot(0, this, [this] { runTutorial(true); });
    }

    void MainWindow::runTutorial(bool first_run) {
        if (tutorial_running_) return;

        // The user practises on an example project, which takes the place of
        // theirs (there is only one project at a time): unsaved changes are
        // saved or dropped first, as when opening another project, and their
        // project comes back from its file afterwards (a new one if it never
        // had a file, as on the first run).
        if (!mayContinue()) return;
        const std::filesystem::path own_file = project_->getFilePath();
        teardownProject();
        project_.reset();
        try {
            adoptProject(tutorial::exampleProject());
        }
        catch (const std::exception&) {
            // Without the example the tour still explains everything.
            adoptProject(std::make_unique<Project>(Tr::tr("Nuevo proyecto").toStdString(), app_defaults::load()));
        }
        startTour({ first_run, own_file });
    }

    void MainWindow::startTour(const Tour& state) {
        tutorial_running_ = true;
        const bool first_run = state.first_run;
        const std::filesystem::path own_file = state.own_file;

        auto* tour = new tutorial::TutorialOverlay(this, tutorial::introSteps(),
            [this](tutorial::Target target) { return tutorialTarget(target); },
            [this](tutorial::View view) {
                if (view == tutorial::View::Joint) switchToTab(-1);
                else if (view == tutorial::View::FirstDiagram && project_->getDiagramCount() > 0) switchToTab(0);
            });
        connect(tour, &tutorial::TutorialOverlay::languageChosen, this,
            [this, tour, first_run, own_file](i18n::Language language) {
                // The example goes to the new window, where the tour starts
                // again at once; the user's project comes back when it ends.
                // This tour stays on screen until the new window covers it,
                // and goes with this window.
                tutorial_running_ = false;
                switchLanguage(language, Tour{ first_run, own_file });
            });
        connect(tour, &tutorial::TutorialOverlay::finished, this, [this, first_run, own_file](bool completed) {
            tutorial_running_ = false;
            restoreAfterTutorial(own_file);
            QSettings settings;
            tutorial::markSeen(settings);
            // Skipped on the first run: show where to find it again.
            if (first_run && !completed) pointToHelpEntry(HelpEntry::Tutorial);
            central_view_->setFocus();
        });
        tour->start();
    }

    void MainWindow::restoreAfterTutorial(const std::filesystem::path& own_file) {
        teardownProject();
        project_.reset(); // the example goes, whatever was done to it
        if (!own_file.empty()) {
            try {
                adoptProject(Project::load(own_file));
                return;
            }
            catch (const std::exception&) {
                // Moved or broken meanwhile: a new project instead.
            }
        }
        adoptProject(std::make_unique<Project>(Tr::tr("Nuevo proyecto").toStdString(), app_defaults::load()));
    }

    bool MainWindow::refuseDuringTutorial() {
        if (!tutorial_running_) return false;
        statusBar()->showMessage(
            Tr::tr("Termina o salta el tutorial para cambiar de proyecto"), 4000);
        return true;
    }

    QRect MainWindow::tutorialTarget(tutorial::Target target) const {
        using T = tutorial::Target;
        auto area = [this](const QWidget* w) {
            if (!w || !w->isVisible()) return QRect();
            return QRect(w->mapTo(this, QPoint(0, 0)), w->size());
        };
        auto menuTitle = [this](const QString& title) {
            for (QAction* a : menuBar()->actions()) {
                if (a->text() != title) continue;
                const QRect r = menuBar()->actionGeometry(a);
                return QRect(menuBar()->mapTo(this, r.topLeft()), r.size());
            }
            return QRect();
        };
        // The strip's visible part only (it scrolls).
        auto inStrip = [&](const QWidget* w) { return area(w).intersected(area(tab_bar_->tabStrip())); };

        DiagramTabWidget* first = tab_bar_->tabAt(0);
        switch (target) {
        case T::Canvas:            return area(central_view_);
        case T::TabStrip:          return area(tab_bar_->tabStrip());
        case T::FirstTabMiniature: return first ? inStrip(first->miniature()) : QRect();
        case T::FirstTabName:      return first ? inStrip(first->nameArea()) : QRect();
        case T::AddTab:            return inStrip(tab_bar_->addButton());
        case T::JointTab:          return area(tab_bar_->jointTab());
        case T::MinimizePanel:     return area(minimize_panel_);
        case T::ZoomLabel:         return area(zoom_label_);
        case T::FileMenu:          return menuTitle(Tr::tr("Archivo"));
        case T::EditMenu:          return menuTitle(Tr::tr("Editar"));
        case T::ProjectMenu:       return menuTitle(Tr::tr("Proyecto"));
        case T::HelpMenu:          return menuTitle(Tr::tr("Ayuda"));
        }
        return QRect();
    }

    void MainWindow::setupCentralArea() {
        auto* central = new QWidget(this);
        auto* vbox = new QVBoxLayout(central);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);

        // Tab bar at the top.
        tab_bar_ = new DiagramTabBar(central);
        vbox->addWidget(tab_bar_);

        // The editing view fills the whole area; the "Minimizar cruces" controls
        // and the selection hint float over it.
        auto* view_wrapper = new QWidget(central);
        auto* view_layout = new QVBoxLayout(view_wrapper);
        view_layout->setContentsMargins(0, 0, 0, 0);

        central_view_ = new DiagramView(nullptr, view_wrapper);
        central_view_->setFrameShape(QFrame::NoFrame);
        view_layout->addWidget(central_view_, 1);
        connect(central_view_, &DiagramView::zoomChanged, this, &MainWindow::updateZoomLabel);

        setupMinimizePanel(view_wrapper);

        hint_banner_ = new HintBanner(view_wrapper);
        connect(hint_banner_, &HintBanner::cancelRequested, this, [this] {
            if (auto* scene = activeScene()) scene->cancelInteraction();
            central_view_->setFocus();
        });

        vbox->addWidget(view_wrapper, 1);
        setCentralWidget(central);

        // Connect tab bar signals.
        connect(tab_bar_, &DiagramTabBar::tabClicked,
            this, &MainWindow::onTabClicked);
        connect(tab_bar_, &DiagramTabBar::jointTabClicked,
            this, &MainWindow::onJointTabClicked);
        connect(tab_bar_, &DiagramTabBar::addTabRequested,
            this, &MainWindow::onAddTabRequested);
        connect(tab_bar_, &DiagramTabBar::tabRenamed,
            this, &MainWindow::onTabRenamed);
        connect(tab_bar_, &DiagramTabBar::removeTabRequested,
            this, &MainWindow::onRemoveDiagram);
        connect(tab_bar_, &DiagramTabBar::tabMenuRequested,
            this, &MainWindow::showTabMenu);
    }

    void MainWindow::setupStatusBar() {
        statusBar()->setStyleSheet(QStringLiteral(R"(
QStatusBar { background: #F5F6FA; border-top: 1px solid #E3E6EF; color: #4B5068; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #4B5068; padding: 1px 10px; }
)"));
        statusBar()->setSizeGripEnabled(false);
        stats_label_ = new QLabel(statusBar());
        zoom_label_ = new QLabel(statusBar());
        zoom_label_->setToolTip(Tr::tr("Zoom (rueda para cambiarlo, Ctrl+0 para ajustar)"));
        statusBar()->addWidget(stats_label_, 1);
        statusBar()->addPermanentWidget(zoom_label_);
    }

    void MainWindow::setupMinimizePanel(QWidget* host) {
        // Floating card, parked top-right at first, that the user can drag
        // anywhere over the diagram:  grip [bolt Minimizar cruces] [bolt|turtle] (?)
        minimize_panel_ = new FloatingPanel(host);
        minimize_panel_->setStyleSheet(QStringLiteral(R"(
QToolButton#minimizeButton {
    color: white; font-weight: 700; border: none; border-radius: 9px; padding: 6px 14px 6px 10px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #6366F1, stop:1 #8B5CF6);
}
QToolButton#minimizeButton:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #575AE8, stop:1 #7E4FEF);
}
QToolButton#minimizeButton:pressed { background: #4F46E5; }
QFrame#modeSwitch { background: #F1F2F7; border-radius: 9px; }
QToolButton#modeButton { border: 1px solid transparent; border-radius: 7px; background: transparent; }
QToolButton#modeButton:hover { background: #E6E8F0; }
QToolButton#modeButton:checked { background: white; border-color: #C7C9F9; }
QToolButton#helpButton {
    border: 1px solid #D6DAE4; border-radius: 11px; color: #6366F1; font-weight: 700; background: white;
}
QToolButton#helpButton:hover { background: #EEF0FF; border-color: #6366F1; }
)"));

        minimize_crossings_btn_ = new QToolButton(minimize_panel_);
        minimize_crossings_btn_->setObjectName("minimizeButton");
        minimize_crossings_btn_->setText(Tr::tr("Minimizar cruces"));
        minimize_crossings_btn_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        minimize_crossings_btn_->setIconSize(QSize(18, 18));
        minimize_crossings_btn_->setCursor(Qt::PointingHandCursor);
        connect(minimize_crossings_btn_, &QToolButton::clicked,
            this, &MainWindow::onMinimizeCrossings);
        minimize_panel_->contentLayout()->addWidget(minimize_crossings_btn_);

        // Segmented switch between the two modes.
        auto* mode_switch = new QFrame(minimize_panel_);
        mode_switch->setObjectName("modeSwitch");
        auto* switch_row = new QHBoxLayout(mode_switch);
        switch_row->setContentsMargins(3, 3, 3, 3);
        switch_row->setSpacing(2);
        auto makeModeButton = [&](style::Icon icon, const QString& tip) {
            auto* b = new QToolButton(mode_switch);
            b->setObjectName("modeButton");
            b->setIcon(style::icon(icon));
            b->setIconSize(QSize(20, 20));
            b->setFixedSize(30, 28);
            b->setCheckable(true);
            b->setToolTip(tip);
            b->setCursor(Qt::PointingHandCursor);
            switch_row->addWidget(b);
            return b;
        };
        mode_fast_btn_ = makeModeButton(style::Icon::Fast,
            Tr::tr("Rápido: el mejor dibujo en 5 segundos como máximo"));
        mode_slow_btn_ = makeModeButton(style::Icon::Slow,
            Tr::tr("Lento: sin límite de tiempo; se puede pausar"));
        auto* modes = new QButtonGroup(this);
        modes->setExclusive(true);
        modes->addButton(mode_fast_btn_);
        modes->addButton(mode_slow_btn_);
        connect(mode_fast_btn_, &QToolButton::toggled, this, [this](bool on) {
            if (on) setMinimizeMode(MinimizeMode::Fast);
        });
        connect(mode_slow_btn_, &QToolButton::toggled, this, [this](bool on) {
            if (on) setMinimizeMode(MinimizeMode::Slow);
        });
        minimize_panel_->contentLayout()->addWidget(mode_switch);

        minimize_help_btn_ = new HelpButton(minimize_panel_);
        minimize_help_btn_->setObjectName("helpButton");
        minimize_help_btn_->setText("?");
        minimize_help_btn_->setFixedSize(22, 22);
        minimize_help_btn_->setToolTip(Tr::tr("<div style=\"color:#1F2330; font-size:10.5pt;\"><b>Minimizar cruces</b></div>"
            "<div style=\"color:#4B5068; margin-top:4px;\">Reordena las cajas de cada nivel para que las "
            "conexiones se crucen lo menos posible. Hay dos modos:</div>"
            "<table cellspacing=\"0\" cellpadding=\"4\" style=\"margin-top:6px;\">"
            "<tr><td style=\"color:#D97706;\"><b>Rápido</b></td>"
            "<td style=\"color:#4B5068;\">Se queda con el mejor orden que encuentra en 5 segundos. "
            "Ideal mientras editas.</td></tr>"
            "<tr><td style=\"color:#2F9E6E;\"><b>Lento</b></td>"
            "<td style=\"color:#4B5068;\">Busca el mejor orden posible, sin límite de tiempo. Puedes pausarlo "
            "cuando quieras y quedarte con el mejor encontrado hasta entonces.</td></tr></table>"
            "<div style=\"color:#8A90A2; margin-top:6px;\">Consejo: usa el modo lento para dejar listo un "
            "esquema terminado.</div>"));
        minimize_panel_->contentLayout()->addWidget(minimize_help_btn_);

        mode_fast_btn_->setChecked(true);
        setMinimizeMode(MinimizeMode::Fast);
        minimize_panel_->placeInCorner(Qt::TopRightCorner);
        minimize_panel_->raise();
    }

    void MainWindow::setMinimizeMode(MinimizeMode mode) {
        minimize_mode_ = mode;
        const bool fast = mode == MinimizeMode::Fast;
        minimize_crossings_btn_->setIcon(style::icon(fast ? style::Icon::Fast : style::Icon::Slow));
        minimize_crossings_btn_->setToolTip(fast
            ? Tr::tr("Minimizar cruces en modo rápido (Ctrl+M)")
            : Tr::tr("Minimizar cruces en modo lento (Ctrl+M)"));
        if (action_menu_fast_) action_menu_fast_->setChecked(fast);
        if (action_menu_slow_) action_menu_slow_->setChecked(!fast);
    }

    void MainWindow::connectHint(DiagramScene* scene) {
        // Only the scene on screen gets to talk to the banner.
        connect(scene, &DiagramScene::interactionHintChanged, this, [this, scene](const QString& hint) {
            if (central_view_->scene() == scene) hint_banner_->setHint(hint);
        });
        connect(scene, &DiagramScene::notice, this, [this](const QString& message) {
            statusBar()->showMessage(message, 5000);
        });
    }

    // ============================================================================
    // Project lifetime
    // ============================================================================

    void MainWindow::buildFromProject() {
        scenes_.clear();
        zoom_levels_.clear();
        joint_zoom_ = 0.0;
        joint_scene_ = nullptr;

        // Create one scene per regular editor.
        for (int i = 0; i < project_->getDiagramCount(); ++i) {
            project_->getEditor(i).setOnMutated([this] { onEditorMutated(); });
            DiagramScene* scene = createSceneForEditor(i);
            scenes_.push_back(scene);
            zoom_levels_.push_back(0.0);
            tab_bar_->addTab(scene,
                QString::fromStdString(project_->getDiagramName(i)));
        }
        project_->getJointEditor().setOnMutated([this] { onEditorMutated(); });

        // Joint scene.
        joint_scene_ = new DiagramScene(&project_->getJointEditor(), this);
        connect(joint_scene_, &DiagramScene::graphChanged,
            this, &MainWindow::onGraphChanged);
        connectHint(joint_scene_);
        attachColourStore(joint_scene_);
        joint_scene_->setDiagramCatalog([this] {
            std::vector<DiagramScene::DiagramInfo> diagrams;
            for (int i = 0; i < project_->getDiagramCount(); ++i)
                diagrams.push_back({ project_->getEditor(i).getId(),
                                     QString::fromStdString(project_->getDiagramName(i)) });
            return diagrams;
        });
        connect(joint_scene_, &DiagramScene::addHypergraphRequested,
            this, [this](const QString& diagram_id, double click_x) {
                // The joint places the diagram by where the user clicked, among
                // the diagrams already there.
                for (int i = 0; i < project_->getDiagramCount(); ++i) {
                    if (project_->getEditor(i).getId() != diagram_id.toStdString()) continue;
                    GraphicalHypergraph& g = const_cast<GraphicalHypergraph&>(project_->getEditor(i).getGraph());
                    try {
                        // The others make room and the new diagram fades in.
                        const auto from = joint_scene_->nodeCenters();
                        project_->getJointEditor().addHypergraph(g, click_x);
                        joint_scene_->rebuildAnimated(from);
                        onGraphChanged();
                    }
                    catch (const std::exception& e) {
                        dialogs::showError(this, Tr::tr("No se ha podido añadir el esquema"),
                            QString::fromStdString(e.what()));
                    }
                    return;
                }
            });

        tab_bar_->setJointScene(joint_scene_);

        // Activate the project's last active diagram.
        switchToTab(project_->getActiveIndex());
    }

    void MainWindow::teardownProject() {
        if (auto* scene = activeScene()) scene->cancelInteraction();
        hint_banner_->setHint(QString());
        central_view_->setScene(nullptr);
        while (tab_bar_->tabCount() > 0) tab_bar_->removeTab(0);
        for (auto* s : scenes_) delete s;
        scenes_.clear();
        delete joint_scene_;
        joint_scene_ = nullptr;
    }

    void MainWindow::adoptProject(std::unique_ptr<Project> project) {
        project_ = std::move(project);
        buildFromProject();
        updateWindowTitle();
        updateStatusBar();
    }

    DiagramScene* MainWindow::createSceneForEditor(int index) {
        auto* scene = new DiagramScene(&project_->getEditor(index), this);
        connect(scene, &DiagramScene::graphChanged,
            this, &MainWindow::onGraphChanged);
        connectHint(scene);
        attachColourStore(scene);
        return scene;
    }

    void MainWindow::attachColourStore(DiagramScene* scene) {
        scene->setColourStore(
            [this] {
                QList<QColor> colours;
                for (const auto& c : project_->getRecentColours())
                    colours << node_visuals::toQColor(c);
                return colours;
            },
            [this](const QColor& colour) {
                project_->addRecentColour(node_visuals::fromQColor(colour));
            });
        scene->setProjectDefaultsStore(
            [this](const std::string& name) { return project_->newNodeAttributes(name); });
        scene->setProjectSettingsSource([this] { return &project_->getSettings(); });
    }

    // ============================================================================
    // Project settings
    // ============================================================================

    void MainWindow::onAjustesProyecto() {
        QList<QColor> recent;
        for (const auto& c : project_->getRecentColours()) recent << node_visuals::toQColor(c);
        ProjectSettingsDialog dlg(project_->getSettings(), app_defaults::load(), recent, this);
        if (dlg.exec() != QDialog::Accepted) return;

        const ProjectSettings settings = dlg.settings();
        if (!applyProjectSettings(settings)) return;
        if (dlg.saveAsAppDefaults()) app_defaults::save(settings);
        statusBar()->showMessage(dlg.saveAsAppDefaults()
            ? Tr::tr("Ajustes guardados en el proyecto y para los proyectos nuevos")
            : Tr::tr("Ajustes del proyecto actualizados"), 4000);
    }

    void MainWindow::onAplicarPredeterminados() {
        const ProjectSettings defaults = app_defaults::load();
        if (defaults == project_->getSettings()) {
            dialogs::showInfo(this, Tr::tr("Nada que cambiar"),
                Tr::tr("Este proyecto ya usa los ajustes predeterminados de la aplicación."));
            return;
        }
        if (applyProjectSettings(defaults))
            statusBar()->showMessage(Tr::tr("El proyecto usa ahora los ajustes predeterminados de la aplicación"), 4000);
    }

    bool MainWindow::applyProjectSettings(const ProjectSettings& settings) {
        const ProjectSettings next = settings.clamped();
        if (next == project_->getSettings()) return true;

        auto existing = Project::ExistingBoxes::KeepTheirSize;
        if (next.layout.box_sizes != project_->getSettings().layout.box_sizes
            && project_->usesDefaultSizesChangedBy(next.layout.box_sizes)) {
            const auto answer = askAboutExistingBoxes(this);
            if (!answer) return false;
            existing = *answer;
        }

        DiagramScene* active = activeScene();
        if (active) active->cancelInteraction();
        const auto from = active ? active->nodeCenters() : std::unordered_map<hypergraph_logic::Node*, QPointF>();
        project_->setSettings(next, existing);

        // Every diagram and the joint changed; the one on show glides into its new shape.
        for (DiagramScene* scene : scenes_)
            if (scene != active) scene->rebuild();
        if (joint_scene_ && joint_scene_ != active) joint_scene_->rebuild();
        if (active) active->rebuildAnimated(from);
        updateWindowTitle();
        updateStatusBar();
        return true;
    }

    // ============================================================================
    // Tab switching
    // ============================================================================

    DiagramScene* MainWindow::activeScene() const {
        return qobject_cast<DiagramScene*>(central_view_->scene());
    }

    void MainWindow::switchToTab(int index) {
        // A selection pending on the diagram being left does not carry over.
        if (auto* old_scene = activeScene())
            old_scene->cancelInteraction();
        hint_banner_->setHint(QString());

        // Save the current zoom before switching.
        if (central_view_->scene()) {
            int old_idx = project_->getActiveIndex();
            if (old_idx >= 0 && old_idx < static_cast<int>(zoom_levels_.size()))
                zoom_levels_[old_idx] = central_view_->currentZoom();
            else if (old_idx == -1)
                joint_zoom_ = central_view_->currentZoom();
        }

        project_->setActive(index);
        tab_bar_->setActiveTab(index);

        QGraphicsScene* scene_to_show =
            (index == -1) ? joint_scene_ : scenes_[index];
        central_view_->setScene(scene_to_show);

        // Restore the previously saved zoom for this tab, or fit with margin if
        // this tab has never been viewed before (zoom == 0).
        double saved_zoom = (index == -1) ? joint_zoom_
            : (index < static_cast<int>(zoom_levels_.size()) ? zoom_levels_[index] : 0.0);

        central_view_->resetZoom();
        if (saved_zoom > 0.0) {
            central_view_->applyZoomFactor(saved_zoom);
        }
        else if (!scene_to_show->sceneRect().isEmpty()) {
            // First time viewing this tab — fit with margin.
            central_view_->fitWithMargin(scene_to_show->sceneRect());
        }

        // The joint diagram cannot be renamed, duplicated, deleted nor pasted into.
        action_renombrar_->setEnabled(index >= 0);
        action_eliminar_esquema_->setEnabled(index >= 0);
        action_duplicar_esquema_->setEnabled(index >= 0);
        updatePasteAction();

        updateUndoRedoActions();
        updateStatusBar();
        updateZoomLabel();
    }

    void MainWindow::onTabClicked(int index) {
        switchToTab(index);
    }

    void MainWindow::onJointTabClicked() {
        switchToTab(-1);
    }

    void MainWindow::onAddTabRequested() {
        onNuevoDiagrama();
    }

    void MainWindow::onTabRenamed(int index, const QString& new_name) {
        // Undoable like any change in the diagram (and the title follows).
        project_->getEditor(index).setName(new_name.toStdString());
        project_->syncJointNames(); // the joint shows the new name too
        updateUndoRedoActions();
        updateWindowTitle();
    }

    void MainWindow::onEsquemaSiguiente() {
        // Regular diagrams in order, then the joint one, then round again.
        const int n = project_->getDiagramCount();
        const int pos = project_->getActiveIndex() == -1 ? n : project_->getActiveIndex();
        const int next = (pos + 1) % (n + 1);
        switchToTab(next == n ? -1 : next);
    }

    void MainWindow::onEsquemaAnterior() {
        const int n = project_->getDiagramCount();
        const int pos = project_->getActiveIndex() == -1 ? n : project_->getActiveIndex();
        const int prev = (pos + n) % (n + 1);
        switchToTab(prev == n ? -1 : prev);
    }

    // ============================================================================
    // State sync
    // ============================================================================

    void MainWindow::updateUndoRedoActions() {
        bool can_undo = false, can_redo = false;
        int idx = project_->getActiveIndex();
        if (idx == -1) {
            can_undo = project_->getJointEditor().canUndo();
            can_redo = project_->getJointEditor().canRedo();
        }
        else if (idx >= 0 && idx < project_->getDiagramCount()) {
            can_undo = project_->getEditor(idx).canUndo();
            can_redo = project_->getEditor(idx).canRedo();
        }
        action_deshacer_->setEnabled(can_undo);
        action_rehacer_->setEnabled(can_redo);
    }

    void MainWindow::onEditorMutated() {
        // Direct when already on the GUI thread; queued to it otherwise (the
        // minimisation worker). A queued call is dropped if the window is gone.
        QMetaObject::invokeMethod(this, [this] { updateWindowTitle(); }, Qt::AutoConnection);
    }

    void MainWindow::updateWindowTitle() {
        updateProjectHistoryActions(); // every change of the project ends up here
        QString title = QString::fromStdString(project_->getName());
        if (project_->hasUnsavedChanges()) title += QStringLiteral(" *");
        setWindowTitle(title + Tr::tr(" — Taller Matrix Harris"));
    }

    void MainWindow::updateStatusBar() {
        if (!stats_label_) return;
        const int idx = project_->getActiveIndex();
        const GraphicalHypergraph* g = nullptr;
        if (idx == -1) g = &project_->getJointEditor().getGraph();
        else if (idx >= 0 && idx < project_->getDiagramCount()) g = &project_->getEditor(idx).getGraph();
        if (!g) { stats_label_->clear(); return; }

        qsizetype boxes = 0, connections = 0;
        for (const auto& n : g->getAllNodes()) if (!n->isDummy()) ++boxes;
        for (const auto& e : g->getAllHyperedges()) if (!e->isSegment()) ++connections;
        stats_label_->setText(Tr::tr("%1   ·   %2   ·   %3").arg(
            count(boxes, Tr::tr("caja"), Tr::tr("cajas")),
            count(connections, Tr::tr("conexión"), Tr::tr("conexiones")),
            count(g->getLayerCount(), Tr::tr("nivel"), Tr::tr("niveles"))));
    }

    void MainWindow::updateZoomLabel() {
        if (!zoom_label_) return;
        zoom_label_->setText(QStringLiteral("%1 %").arg(qRound(central_view_->currentZoom() * 100.0)));
    }

    void MainWindow::onGraphChanged() {
        updateUndoRedoActions();
        updateWindowTitle();
        updateStatusBar();
        // Tab bar miniatures update automatically (shared scenes); names may
        // have changed through undo/redo of a rename.
        for (int i = 0; i < project_->getDiagramCount(); ++i)
            tab_bar_->setTabName(i, QString::fromStdString(project_->getDiagramName(i)));
    }

    // ============================================================================
    // Archivo
    // ============================================================================

    void MainWindow::onNuevoProyecto() {
        if (refuseDuringTutorial()) return;
        if (!mayContinue()) return;
        teardownProject();
        project_.reset(); // only one project (and joint graph) may exist at a time
        adoptProject(std::make_unique<Project>(Tr::tr("Nuevo proyecto").toStdString(), app_defaults::load()));
        statusBar()->showMessage(Tr::tr("Nuevo proyecto creado"), 3000);
    }

    void MainWindow::onNuevoDiagrama() {
        int new_index = project_->addDiagram();
        project_->getEditor(new_index).setOnMutated([this] { onEditorMutated(); });
        DiagramScene* scene = createSceneForEditor(new_index);
        scenes_.push_back(scene);
        zoom_levels_.push_back(0.0); // 0 means "fit on first view"
        tab_bar_->addTab(scene,
            QString::fromStdString(project_->getDiagramName(new_index)));
        switchToTab(new_index);
        updateWindowTitle();
        // A new diagram's first job is to get a name: type it straight away.
        tab_bar_->startRename(new_index);
    }

    void MainWindow::onAbrirProyecto() {
        if (refuseDuringTutorial()) return;
        const QString start = project_->getFilePath().empty() ? QString()
            : QFileInfo(QString::fromStdString(project_->getFilePath().string())).absolutePath();
        const QString path = QFileDialog::getOpenFileName(
            this, Tr::tr("Abrir proyecto"), start, projectFilter());
        if (!path.isEmpty()) openProject(path);
    }

    void MainWindow::openProject(const QString& path) {
        if (refuseDuringTutorial()) return;
        if (!QFileInfo::exists(path)) {
            dialogs::showError(this, Tr::tr("No se encuentra el proyecto"),
                Tr::tr("El archivo «%1» ya no existe o se ha movido.").arg(path));
            forgetRecentProject(path);
            return;
        }
        if (!mayContinue()) return;

        teardownProject();
        project_.reset(); // only one project (and joint graph) may exist at a time
        try {
            adoptProject(Project::load(path.toStdString()));
            rememberRecentProject(path);
            statusBar()->showMessage(Tr::tr("Proyecto abierto: %1").arg(QFileInfo(path).fileName()), 3000);
        }
        catch (const std::exception& e) {
            adoptProject(std::make_unique<Project>(Tr::tr("Nuevo proyecto").toStdString(), app_defaults::load()));
            dialogs::showError(this, Tr::tr("No se ha podido abrir el proyecto"),
                Tr::tr("«%1» no es un proyecto válido del Taller Matrix Harris.\n\n%2")
                    .arg(QFileInfo(path).fileName(), QString::fromStdString(e.what())));
        }
    }

    void MainWindow::onGuardarProyecto() {
        if (project_->getFilePath().empty()) saveWithPath();
        else                                 saveToKnownPath();
    }

    void MainWindow::onGuardarComo() {
        saveWithPath();
    }

    bool MainWindow::saveWithPath() {
        const QString suggested = project_->getFilePath().empty()
            ? QString::fromStdString(project_->getName()) + ".json"
            : QString::fromStdString(project_->getFilePath().string());
        const QString path = QFileDialog::getSaveFileName(
            this, Tr::tr("Guardar proyecto"), suggested, projectFilter());
        if (path.isEmpty()) return false;
        try {
            project_->setName(QFileInfo(path).completeBaseName().toStdString());
            project_->save(path.toStdString());
            rememberRecentProject(path);
            updateWindowTitle();
            statusBar()->showMessage(Tr::tr("Proyecto guardado en %1").arg(QFileInfo(path).fileName()), 3000);
            return true;
        }
        catch (const std::exception& e) {
            dialogs::showError(this, Tr::tr("No se ha podido guardar el proyecto"),
                QString::fromStdString(e.what()));
            return false;
        }
    }

    bool MainWindow::saveToKnownPath() {
        try {
            project_->save();
            updateWindowTitle();
            statusBar()->showMessage(Tr::tr("Proyecto guardado"), 2500);
            return true;
        }
        catch (const std::exception& e) {
            dialogs::showError(this, Tr::tr("No se ha podido guardar el proyecto"),
                QString::fromStdString(e.what()));
            return false;
        }
    }

    void MainWindow::onExportarImagen() {
        DiagramScene* scene = activeScene();
        if (!scene || scene->items().isEmpty()) {
            dialogs::showInfo(this, Tr::tr("No hay nada que exportar"),
                Tr::tr("Este esquema está vacío. Añade alguna caja y vuelve a intentarlo."));
            return;
        }

        const int idx = project_->getActiveIndex();
        const QString name = idx == -1 ? Tr::tr("Esquema conjunto")
                                       : QString::fromStdString(project_->getDiagramName(idx));
        QString selected;
        QString path = QFileDialog::getSaveFileName(this, Tr::tr("Exportar esquema"),
            QDir(defaultExportFolder()).filePath(exporting::safeFileName(name) + QStringLiteral(".png")),
            Tr::tr("Imagen PNG (*.png);;Imagen JPEG (*.jpg *.jpeg);;Documento PDF (*.pdf)"),
            &selected);
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty())
            path += selected.contains("pdf") ? ".pdf" : selected.contains("JPEG") ? ".jpg" : ".png";

        // Picture the diagram as it is, with no selection in progress.
        scene->cancelInteraction();
        const bool ok = QFileInfo(path).suffix().compare("pdf", Qt::CaseInsensitive) == 0
            ? exporting::writePdf(path, QString::fromStdString(project_->getName()), { { name, scene } })
            : exporting::writeImage(path, scene);

        if (ok) statusBar()->showMessage(Tr::tr("Esquema exportado a %1").arg(QFileInfo(path).fileName()), 4000);
        else    dialogs::showError(this, Tr::tr("No se ha podido exportar el esquema"),
                    Tr::tr("No se ha podido escribir el archivo «%1».").arg(path));
    }

    void MainWindow::onExportarProyecto() {
        // Every diagram in tab order, then the joint one.
        std::vector<exporting::Page> pages;
        for (int i = 0; i < project_->getDiagramCount(); ++i)
            pages.push_back({ QString::fromStdString(project_->getDiagramName(i)), scenes_[i] });
        pages.push_back({ Tr::tr("Esquema conjunto"), joint_scene_ });
        const QString project_name = QString::fromStdString(project_->getName());

        // ── Choose the format ────────────────────────────────────────────────
        StyledDialog dlg(StyledDialog::Badge::App, Tr::tr("Exportar proyecto"), this);
        dlg.setMessage(Tr::tr("Incluye %1 de «%2» y el esquema conjunto. ¿En qué formato quieres exportarlo?")
            .arg(count(project_->getDiagramCount(), Tr::tr("esquema"), Tr::tr("esquemas")),
                 project_name));

        auto* options = new QWidget;
        options->setStyleSheet(QStringLiteral(R"(
QToolButton#exportOption {
    border: 1px solid #E3E6EF; border-radius: 12px; background: white; padding: 10px 14px;
    color: #1F2330; text-align: left;
}
QToolButton#exportOption:hover { border-color: #A5A8F5; background: #FAFAFF; }
QToolButton#exportOption:checked { border: 2px solid #6366F1; background: #EEF0FF; }
)"));
        auto* column = new QVBoxLayout(options);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(8);
        auto option = [&](style::Icon icon, const QString& text) {
            auto* b = new QToolButton(options);
            b->setObjectName("exportOption");
            b->setCheckable(true);
            b->setIcon(style::icon(icon));
            b->setIconSize(QSize(34, 34));
            b->setText(text);
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setMinimumHeight(62);
            b->setCursor(Qt::PointingHandCursor);
            column->addWidget(b);
            return b;
        };
        QToolButton* as_pdf = option(style::Icon::PdfDocument,
            Tr::tr("Documento PDF\nUna página por esquema y el esquema conjunto al final"));
        QToolButton* as_png = option(style::Icon::ImageFolder,
            Tr::tr("Imágenes PNG\nUna carpeta «%1» con una imagen por esquema").arg(project_name));
        auto* group = new QButtonGroup(options);
        group->addButton(as_pdf);
        group->addButton(as_png);
        QSettings settings;
        (settings.value("exportFormat").toString() == "png" ? as_png : as_pdf)->setChecked(true);
        dlg.setBody(options);
        dlg.addButton(Tr::tr("Cancelar"), 0, StyledDialog::ButtonStyle::Secondary, false, true);
        dlg.addButton(Tr::tr("Exportar"), 1, StyledDialog::ButtonStyle::Primary, true);
        dlg.exec();
        if (dlg.choice() != 1) return;
        const bool pdf = as_pdf->isChecked();
        settings.setValue("exportFormat", pdf ? "pdf" : "png");

        // Picture every diagram as it is, with no selection in progress.
        for (auto* s : scenes_) s->cancelInteraction();
        joint_scene_->cancelInteraction();

        if (pdf) {
            QString path = QFileDialog::getSaveFileName(this, Tr::tr("Exportar proyecto como PDF"),
                QDir(defaultExportFolder()).filePath(exporting::safeFileName(project_name) + QStringLiteral(".pdf")),
                Tr::tr("Documento PDF (*.pdf)"));
            if (path.isEmpty()) return;
            if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".pdf");

            QApplication::setOverrideCursor(Qt::WaitCursor);
            const bool ok = exporting::writePdf(path, project_name, pages);
            QApplication::restoreOverrideCursor();
            if (!ok) {
                dialogs::showError(this, Tr::tr("No se ha podido exportar el proyecto"),
                    Tr::tr("No se ha podido escribir el archivo «%1».").arg(path));
                return;
            }
            showExportDone(Tr::tr("Proyecto exportado"),
                Tr::tr("«%1» tiene %2, una por esquema.")
                    .arg(QFileInfo(path).fileName(), count(static_cast<qsizetype>(pages.size()),
                        Tr::tr("página"), Tr::tr("páginas"))),
                Tr::tr("Abrir PDF"), path);
            return;
        }

        // ── PNG: a new folder named after the project ─────────────────────────
        const QString parent_dir = QFileDialog::getExistingDirectory(this,
            Tr::tr("Elige dónde crear la carpeta «%1»").arg(project_name), defaultExportFolder());
        if (parent_dir.isEmpty()) return;

        // Never mix with an earlier export: "Proyecto", "Proyecto (2)", ...
        const QString base = exporting::safeFileName(project_name);
        QString folder = QDir(parent_dir).filePath(base);
        for (int n = 2; QFileInfo::exists(folder); ++n)
            folder = QDir(parent_dir).filePath(QStringLiteral("%1 (%2)").arg(base).arg(n));
        if (!QDir().mkpath(folder)) {
            dialogs::showError(this, Tr::tr("No se ha podido exportar el proyecto"),
                Tr::tr("No se ha podido crear la carpeta «%1».").arg(folder));
            return;
        }

        QApplication::setOverrideCursor(Qt::WaitCursor);
        QStringList written, empty, failed;
        QSet<QString> used; // diagrams may share a name: "Esquema nuevo", "Esquema nuevo (2)"
        for (const auto& page : pages) {
            if (!page.scene || page.scene->items().isEmpty()) { empty << page.title; continue; }
            QString file = exporting::safeFileName(page.title);
            for (int n = 2; used.contains(file.toLower()); ++n)
                file = QStringLiteral("%1 (%2)").arg(exporting::safeFileName(page.title)).arg(n);
            used.insert(file.toLower());
            const QString path = QDir(folder).filePath(file + QStringLiteral(".png"));
            (exporting::writeImage(path, page.scene) ? written : failed) << file + QStringLiteral(".png");
        }
        QApplication::restoreOverrideCursor();

        if (!failed.isEmpty()) {
            dialogs::showError(this, Tr::tr("No se han podido exportar todas las imágenes"),
                Tr::tr("No se han podido escribir: %1.").arg(failed.join(QStringLiteral(", "))));
            return;
        }
        QString text = written.isEmpty()
            ? Tr::tr("Todos los esquemas están vacíos, así que la carpeta «%1» no contiene imágenes.")
                  .arg(QFileInfo(folder).fileName())
            : Tr::tr("La carpeta «%1» contiene %2.").arg(QFileInfo(folder).fileName(),
                  count(written.size(), Tr::tr("imagen"), Tr::tr("imágenes")));
        if (!empty.isEmpty() && !written.isEmpty())
            text += Tr::tr("\n\nSin imagen por estar vacíos: %1.").arg(empty.join(QStringLiteral(", ")));
        showExportDone(Tr::tr("Proyecto exportado"), text, Tr::tr("Abrir carpeta"), folder);
    }

    void MainWindow::showExportDone(const QString& title, const QString& text,
        const QString& open_text, const QString& target)
    {
        StyledDialog done(StyledDialog::Badge::Success, title, this);
        done.setMessage(text);
        done.addButton(Tr::tr("Cerrar"), 0, StyledDialog::ButtonStyle::Secondary, false, true);
        done.addButton(open_text, 1, StyledDialog::ButtonStyle::Primary, true);
        done.exec();
        if (done.choice() == 1) QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    }

    QString MainWindow::defaultExportFolder() const {
        if (!project_->getFilePath().empty())
            return QFileInfo(QString::fromStdString(project_->getFilePath().string())).absolutePath();
        return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }

    void MainWindow::onSalir() {
        close();
    }

    // ============================================================================
    // Recent projects
    // ============================================================================

    QStringList MainWindow::recentProjects() const {
        return QSettings().value(RECENT_KEY).toStringList();
    }

    void MainWindow::rememberRecentProject(const QString& path) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        QStringList list = recentProjects();
        list.removeAll(absolute);
        list.prepend(absolute);
        while (list.size() > MAX_RECENT_PROJECTS) list.removeLast();
        QSettings().setValue(RECENT_KEY, list);
    }

    void MainWindow::forgetRecentProject(const QString& path) {
        QStringList list = recentProjects();
        list.removeAll(QFileInfo(path).absoluteFilePath());
        QSettings().setValue(RECENT_KEY, list);
    }

    void MainWindow::rebuildRecentMenu() {
        recent_menu_->clear();
        const QStringList list = recentProjects();
        if (list.isEmpty()) {
            recent_menu_->addAction(Tr::tr("No hay proyectos recientes"))->setEnabled(false);
            return;
        }
        int n = 1;
        for (const QString& path : list) {
            QAction* a = recent_menu_->addAction(QStringLiteral("%1   %2").arg(n++).arg(QFileInfo(path).completeBaseName()));
            a->setToolTip(QDir::toNativeSeparators(path));
            connect(a, &QAction::triggered, this, [this, path] { openProject(path); });
        }
        recent_menu_->addSeparator();
        connect(recent_menu_->addAction(Tr::tr("Borrar la lista")), &QAction::triggered, this, [] {
            QSettings().remove(RECENT_KEY);
        });
    }

    // ============================================================================
    // Editar
    // ============================================================================

    void MainWindow::onDeshacer() {
        try {
            int idx = project_->getActiveIndex();
            if (idx == -1) project_->getJointEditor().undo();
            else           project_->getEditor(idx).undo();
            project_->syncJointNames(); // undoing a rename, or a joint step with old names

            if (idx == -1) joint_scene_->rebuild();
            else           scenes_[idx]->rebuild();

            onGraphChanged();
        }
        catch (const std::exception& e) {
            dialogs::showWarning(this, Tr::tr("No se puede deshacer"), QString::fromStdString(e.what()));
        }
    }

    void MainWindow::onCopiar() {
        DiagramScene* scene = activeScene();
        if (!scene) return;
        // The block under the mouse; with the mouse elsewhere, the whole diagram.
        QWidget* viewport = central_view_->viewport();
        const QPoint local = viewport->mapFromGlobal(QCursor::pos());
        const QString copied = viewport->rect().contains(local)
            ? scene->copyAt(central_view_->mapToScene(local))
            : scene->copyAt(scene->sceneRect().topLeft() - QPointF(1e6, 1e6));
        if (copied.isEmpty())
            statusBar()->showMessage(Tr::tr("No hay nada que copiar"), 3000);
    }

    void MainWindow::onPegar() {
        DiagramScene* scene = activeScene();
        if (!scene || !scene->canPaste()) return;
        // Where the mouse is, or in the middle of what is on screen.
        QWidget* viewport = central_view_->viewport();
        const QPoint local = viewport->mapFromGlobal(QCursor::pos());
        scene->pasteAt(central_view_->mapToScene(viewport->rect().contains(local) ? local : viewport->rect().center()));
    }

    void MainWindow::updatePasteAction() {
        if (!action_pegar_ || !project_) return;
        const bool regular = project_->getActiveIndex() >= 0;
        action_pegar_->setEnabled(regular && clipboard::hasPiece());
        action_pegar_->setToolTip(regular ? Tr::tr("Pega lo copiado donde está el ratón")
                                          : Tr::tr("No se puede pegar en el esquema conjunto"));
    }

    void MainWindow::onRehacer() {
        try {
            int idx = project_->getActiveIndex();
            if (idx == -1) project_->getJointEditor().redo();
            else           project_->getEditor(idx).redo();
            project_->syncJointNames();

            if (idx == -1) joint_scene_->rebuild();
            else           scenes_[idx]->rebuild();

            onGraphChanged();
        }
        catch (const std::exception& e) {
            dialogs::showWarning(this, Tr::tr("No se puede rehacer"), QString::fromStdString(e.what()));
        }
    }

    // ============================================================================
    // Ver
    // ============================================================================

    void MainWindow::onAcercar() {
        central_view_->zoomIn();
    }

    void MainWindow::onAlejar() {
        central_view_->zoomOut();
    }

    void MainWindow::onAjustarVentana() {
        if (auto* scene = activeScene(); scene && !scene->sceneRect().isEmpty())
            central_view_->fitWithMargin(scene->sceneRect());
    }

    void MainWindow::onTamanoReal() {
        central_view_->applyZoomFactor(1.0);
    }

    void MainWindow::onPantallaCompleta(bool on) {
        if (on) showFullScreen();
        else    showMaximized();
    }

    // ============================================================================
    // Esquema
    // ============================================================================

    void MainWindow::onRenombrarEsquema() {
        const int idx = project_->getActiveIndex();
        if (idx >= 0) tab_bar_->startRename(idx);
    }

    void MainWindow::onEliminarEsquemaActivo() {
        const int idx = project_->getActiveIndex();
        if (idx >= 0) onRemoveDiagram(idx);
    }

    void MainWindow::duplicateDiagram(int index) {
        if (index < 0 || index >= project_->getDiagramCount()) return;
        if (auto* scene = activeScene()) scene->cancelInteraction();

        const QString original = QString::fromStdString(project_->getDiagramName(index));
        const int new_index = project_->duplicateDiagram(index);
        project_->getEditor(new_index).setOnMutated([this] { onEditorMutated(); });
        DiagramScene* scene = createSceneForEditor(new_index);
        scenes_.push_back(scene);
        zoom_levels_.push_back(0.0);
        const QString name = QString::fromStdString(project_->getDiagramName(new_index));
        tab_bar_->addTab(scene, name);
        switchToTab(new_index);
        updateWindowTitle();
        statusBar()->showMessage(Tr::tr("«%1» duplicado como «%2»").arg(original, name), 4000);
    }

    void MainWindow::showTabMenu(int index, const QPoint& global_pos) {
        if (index < 0 || index >= project_->getDiagramCount()) return;
        QMenu* menu = style::createMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addAction(Tr::tr("Renombrar"), this, [this, index] { tab_bar_->startRename(index); });
        menu->addAction(style::icon(style::Icon::Duplicate), Tr::tr("Duplicar esquema"),
            this, [this, index] { duplicateDiagram(index); });
        menu->addSeparator();
        menu->addAction(style::icon(style::Icon::RemoveBox), Tr::tr("Eliminar esquema"),
            this, [this, index] { onRemoveDiagram(index); });
        help::addHelpButtons(menu, this);
        menu->popup(global_pos);
    }

    void MainWindow::onMinimizeCrossings() {
        int idx = project_->getActiveIndex();
        try {
            MinimizingProgressDialog::Options opts;

            bool nothing_to_minimize = true;
            for (const auto& [id, layer] : (idx == -1) ? project_->getJointEditor().getLayers() :
                                                         project_->getEditor(idx).getLayers()) {
                if (layer.outgoing_edges.size() > 1) {
                    nothing_to_minimize = false;
                    break;
                }
            }

            if (nothing_to_minimize) {
                dialogs::showInfo(this, Tr::tr("No hay cruces que minimizar"),
                    Tr::tr("En este esquema ninguna conexión puede cruzarse con otra, "
                                   "así que ya está dibujado de la mejor forma posible."));
                return;
            }

            if (minimize_mode_ == MinimizeMode::Fast) {
                // Fast mode: same modal dialog as slow mode, but with a
                // countdown from kILPTimeBudgetSeconds instead of open-ended
                // text, and no "Pausar" button -- the solve is already capped
                // to a few seconds internally, so there's nothing meaningful
                // to cancel. Routing fast mode through the dialog too (rather
                // than calling the editor directly) is what actually fixes
                // the "looks frozen, clicks queue up" problem: the dialog's
                // modality is what stops Qt from queuing further clicks on
                // the button while this runs, and its worker thread is what
                // keeps the dialog (and its spinner/countdown) responsive
                // while it does.
                opts.show_pausar = false;
                opts.countdown_seconds = static_cast<int>(kILPTimeBudgetSeconds);

                if (idx == -1) {
                    MinimizingProgressDialog::run(minimize_crossings_btn_,
                        [this](ILPCancellationToken&) {
                            return project_->getJointEditor().minimizeCrossings();
                        }, opts);
                }
                else {
                    MinimizingProgressDialog::run(minimize_crossings_btn_,
                        [this, idx](ILPCancellationToken&) {
                            return project_->getEditor(idx).minimizeCrossings();
                        }, opts);
                }
            }
            else {
                // Slow mode: same dialog, open-ended text + "Pausar" wired to
                // token.cancel(). Everything else in the app is blocked by
                // the dialog's modality while this runs; the dialog itself
                // stays responsive to its own "Pausar" button.
                opts.show_pausar = true;
                opts.countdown_seconds = 0;

                if (idx == -1) {
                    MinimizingProgressDialog::run(minimize_crossings_btn_,
                        [this](ILPCancellationToken& token) {
                            return project_->getJointEditor().minimizeCrossingsInterruptible(token);
                        }, opts);
                }
                else {
                    MinimizingProgressDialog::run(minimize_crossings_btn_,
                        [this, idx](ILPCancellationToken& token) {
                            return project_->getEditor(idx).minimizeCrossingsInterruptible(token);
                        }, opts);
                }
            }

            if (idx == -1) joint_scene_->rebuild();
            else           scenes_[idx]->rebuild();

            onGraphChanged();
        }
        catch (const std::exception& e) {
            dialogs::showError(this, Tr::tr("No se han podido minimizar los cruces"),
                QString::fromStdString(e.what()));
        }
    }

    void MainWindow::onRemoveDiagram(int index) {
        if (index < 0 || index >= static_cast<int>(scenes_.size())) return;

        // No need to ask: Proyecto › Deshacer brings it back.
        const QString name = QString::fromStdString(project_->getDiagramName(index));

        // Remove scene and zoom level.
        if (central_view_->scene() == scenes_[index]) central_view_->setScene(nullptr);
        delete scenes_[index];
        scenes_.erase(scenes_.begin() + index);
        if (index < static_cast<int>(zoom_levels_.size()))
            zoom_levels_.erase(zoom_levels_.begin() + index);

        tab_bar_->removeTab(index);
        project_->removeDiagram(index);

        // Switch to whatever is now active.
        int new_active = project_->getActiveIndex();
        if (new_active == -1 && !scenes_.empty())
            new_active = 0;
        if (!scenes_.empty() || new_active == -1) {
            switchToTab(new_active);
        }
        else {
            central_view_->setScene(nullptr);
            updateUndoRedoActions();
        }
        updateWindowTitle();
        statusBar()->showMessage(Tr::tr("Esquema «%1» eliminado (Ctrl+Alt+Z lo recupera)").arg(name), 5000);
    }

    // ============================================================================
    // The project's history
    // ============================================================================

    void MainWindow::updateProjectHistoryActions() {
        if (!action_deshacer_proyecto_ || !project_) return;
        const bool can_undo = project_->canUndoProjectChange();
        const bool can_redo = project_->canRedoProjectChange();
        action_deshacer_proyecto_->setEnabled(can_undo);
        action_rehacer_proyecto_->setEnabled(can_redo);
        action_deshacer_proyecto_->setText(can_undo
            ? Tr::tr("Deshacer: %1").arg(QString::fromStdString(project_->projectUndoLabel()))
            : Tr::tr("Deshacer"));
        action_rehacer_proyecto_->setText(can_redo
            ? Tr::tr("Rehacer: %1").arg(QString::fromStdString(project_->projectRedoLabel()))
            : Tr::tr("Rehacer"));
    }

    void MainWindow::onProjectHistory(bool undo) {
        if (undo ? !project_->canUndoProjectChange() : !project_->canRedoProjectChange()) return;
        const QString what = QString::fromStdString(undo ? project_->projectUndoLabel() : project_->projectRedoLabel());
        if (auto* scene = activeScene()) scene->cancelInteraction();

        const app_logic::ProjectSettings settings_before = project_->getSettings();
        if (undo) project_->undoProjectChange();
        else      project_->redoProjectChange();

        syncDiagramTabs();
        if (project_->getSettings() != settings_before) {
            // Every diagram and the joint are drawn again with the settings back.
            for (DiagramScene* scene : scenes_) scene->rebuild();
            if (joint_scene_) joint_scene_->rebuild();
        }
        updateWindowTitle();
        updateStatusBar();
        statusBar()->showMessage((undo ? Tr::tr("Deshecho: %1") : Tr::tr("Rehecho: %1")).arg(what), 5000);
    }

    void MainWindow::syncDiagramTabs() {
        // What each diagram on show had: its scene and its zoom.
        std::unordered_map<const app_logic::HypergraphEditor*, std::pair<DiagramScene*, double>> had;
        DiagramScene* shown = activeScene();
        for (size_t i = 0; i < scenes_.size(); ++i) {
            const double zoom = scenes_[i] == shown ? central_view_->currentZoom()
                : (i < zoom_levels_.size() ? zoom_levels_[i] : 0.0);
            had[scenes_[i]->regularEditor()] = { scenes_[i], zoom };
        }
        if (shown == joint_scene_ && shown) joint_zoom_ = central_view_->currentZoom();
        hint_banner_->setHint(QString());
        central_view_->setScene(nullptr);
        while (tab_bar_->tabCount() > 0) tab_bar_->removeTab(0);
        scenes_.clear();
        zoom_levels_.clear();

        for (int i = 0; i < project_->getDiagramCount(); ++i) {
            app_logic::HypergraphEditor* editor = &project_->getEditor(i);
            DiagramScene* scene = nullptr;
            double zoom = 0.0;
            if (auto it = had.find(editor); it != had.end()) {
                std::tie(scene, zoom) = it->second;
                had.erase(it);
            }
            else {
                // A diagram back in the project: a scene for it again.
                editor->setOnMutated([this] { onEditorMutated(); });
                scene = createSceneForEditor(i);
            }
            scenes_.push_back(scene);
            zoom_levels_.push_back(zoom);
            tab_bar_->addTab(scene, QString::fromStdString(project_->getDiagramName(i)));
        }
        // The scenes of the diagrams that left (the project keeps their editors).
        for (auto& [editor, scene_zoom] : had) delete scene_zoom.first;

        int active = project_->getActiveIndex();
        if (active >= project_->getDiagramCount()) active = project_->getDiagramCount() - 1;
        switchToTab(active);
    }

    // ============================================================================
    // Ayuda
    // ============================================================================

    void MainWindow::onAtajos() {
        StyledDialog dlg(StyledDialog::Badge::Info, Tr::tr("Atajos y controles"), this);
        dlg.setMessage(Tr::tr("Todo lo que puedes hacer con el teclado y el ratón."));

        auto* body = new QWidget;
        body->setStyleSheet(QStringLiteral(R"(
QLabel#group { color: #8A90A2; font-size: 7pt; font-weight: 700; letter-spacing: 1px; padding-top: 8px; }
QLabel#key {
    background: #F4F5F9; border: 1px solid #DDE0EA; border-bottom-width: 2px; border-radius: 5px;
    padding: 1px 7px; color: #3A4050; font-size: 8pt; font-weight: 600;
}
QLabel#what { color: #3A4050; }
)"));
        auto* columns = new QHBoxLayout(body);
        columns->setContentsMargins(0, 0, 0, 0);
        columns->setSpacing(28);

        using Rows = std::vector<std::pair<QString, QString>>;
        auto column = [&](const std::vector<std::pair<QString, Rows>>& groups) {
            auto* grid = new QGridLayout;
            grid->setHorizontalSpacing(12);
            grid->setVerticalSpacing(5);
            int row = 0;
            for (const auto& [title, rows] : groups) {
                auto* g = new QLabel(title.toUpper());
                g->setObjectName("group");
                grid->addWidget(g, row++, 0, 1, 2);
                for (const auto& [keys, what] : rows) {
                    auto* k = new QLabel(keys);
                    k->setObjectName("key");
                    auto* w = new QLabel(what);
                    w->setObjectName("what");
                    grid->addWidget(k, row, 0, Qt::AlignLeft);
                    grid->addWidget(w, row++, 1);
                }
            }
            grid->setRowStretch(row, 1);
            columns->addLayout(grid);
        };

        column({
            { Tr::tr("Proyecto"), {
                { QStringLiteral("Ctrl+N"), Tr::tr("Nuevo proyecto") },
                { QStringLiteral("Ctrl+O"), Tr::tr("Abrir proyecto") },
                { QStringLiteral("Ctrl+S"), Tr::tr("Guardar") },
                { Tr::tr("Ctrl+Mayús+S"), Tr::tr("Guardar como") },
                { QStringLiteral("Ctrl+E"), Tr::tr("Exportar el esquema") },
                { Tr::tr("Ctrl+Mayús+E"), Tr::tr("Exportar el proyecto") },
                { QStringLiteral("Ctrl+Alt+Z"), Tr::tr("Deshacer un cambio del proyecto") },
                { QStringLiteral("Ctrl+Alt+Y"), Tr::tr("Rehacerlo") },
                { QStringLiteral("Ctrl+Alt+S"), Tr::tr("Ajustes del proyecto") } } },
            { Tr::tr("Edición"), {
                { QStringLiteral("Ctrl+Z"), Tr::tr("Deshacer") },
                { QStringLiteral("Ctrl+Y"), Tr::tr("Rehacer") },
                { QStringLiteral("Ctrl+C"), Tr::tr("Copiar el bloque bajo el ratón (o el esquema)") },
                { QStringLiteral("Ctrl+V"), Tr::tr("Pegar donde está el ratón") },
                { QStringLiteral("Esc"), Tr::tr("Cancelar la selección en curso") } } },
            { Tr::tr("Esquemas"), {
                { QStringLiteral("Ctrl+T"), Tr::tr("Nuevo esquema") },
                { QStringLiteral("F2"), Tr::tr("Renombrar esquema") },
                { QStringLiteral("Ctrl+D"), Tr::tr("Duplicar esquema") },
                { QStringLiteral("Ctrl+Tab"), Tr::tr("Esquema siguiente") },
                { QStringLiteral("Ctrl+J"), Tr::tr("Esquema conjunto") },
                { QStringLiteral("Ctrl+M"), Tr::tr("Minimizar cruces") } } },
        });
        column({
            { Tr::tr("Vista"), {
                { Tr::tr("Rueda"), Tr::tr("Acercar / alejar") },
                { QStringLiteral("Ctrl+0"), Tr::tr("Ajustar a la ventana") },
                { QStringLiteral("Ctrl+1"), Tr::tr("Tamaño real") },
                { Tr::tr("Arrastrar fondo"), Tr::tr("Desplazar la vista") },
                { QStringLiteral("F11"), Tr::tr("Pantalla completa") } } },
            { Tr::tr("Cajas y conexiones"), {
                { Tr::tr("Clic derecho"), Tr::tr("Menú de la caja o del fondo") },
                { Tr::tr("Clic en conexión"), Tr::tr("Menú de la conexión") },
                { Tr::tr("Doble clic"), Tr::tr("Propiedades de la caja") },
                { Tr::tr("Arrastrar caja"), Tr::tr("Moverla de posición o nivel") },
                { Tr::tr("Rueda sobre caja"), Tr::tr("Desplazar un texto largo") } } },
            { Tr::tr("Pestañas"), {
                { Tr::tr("Doble clic"), Tr::tr("Renombrar (sobre el nombre)") },
                { Tr::tr("Clic derecho"), Tr::tr("Renombrar, duplicar o eliminar") },
                { Tr::tr("Rueda"), Tr::tr("Zoom en la miniatura") } } },
        });

        dlg.setBody(body);
        dlg.addButton(Tr::tr("Cerrar"), 0, StyledDialog::ButtonStyle::Primary, true, true);
        dlg.exec();
    }

    void MainWindow::onAcercaDe() {
        StyledDialog dlg(StyledDialog::Badge::App, Tr::tr("Taller Matrix Harris"), this);
        // The version is the project's (CMakeLists.txt), set by main().
        dlg.setMessage(Tr::tr("Versión %1\n\n"
            "Herramienta para crear, organizar y dibujar esquemas Matrix Harris, "
            "así como cualquier otro tipo de esquema jerárquico, "
            "con un dibujo automático que minimiza los cruces.\n\n"
            "Desarrollado por Javier San Andrés.\n").arg(QCoreApplication::applicationVersion()));
        dlg.addButton(Tr::tr("Cerrar"), 0, StyledDialog::ButtonStyle::Primary, true, true);
        dlg.exec();
    }

    // ============================================================================
    // Close, drag and drop
    // ============================================================================

    void MainWindow::closeEvent(QCloseEvent* event) {
        // During the tour the open project is the example: nothing to keep
        // (the user's own was saved or dropped when the tour began).
        if (tutorial_running_ || closing_for_update_ || replaced_) { // nothing to keep / already asked / handed over
            event->accept();
            return;
        }
        if (mayContinue()) event->accept();
        else event->ignore();
    }

    void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
        const auto urls = event->mimeData()->urls();
        if (urls.size() == 1 && urls.front().isLocalFile()
            && urls.front().toLocalFile().endsWith(".json", Qt::CaseInsensitive))
            event->acceptProposedAction();
    }

    void MainWindow::dropEvent(QDropEvent* event) {
        const QString path = event->mimeData()->urls().front().toLocalFile();
        event->acceptProposedAction();
        // Opened once the drop has finished, so no dialog runs inside it.
        QMetaObject::invokeMethod(this, [this, path] { openProject(path); }, Qt::QueuedConnection);
    }

    void MainWindow::switchLanguage(i18n::Language language) {
        switchLanguage(language, std::nullopt);
    }

    void MainWindow::switchLanguage(i18n::Language language, std::optional<Tour> tour) {
        if (language == i18n::language() || replaced_) return;
        if (tutorial_running_) return; // the tour hands the project back first (see runTutorial)
        QSettings settings;
        i18n::save(settings, language);

        // The names the app gave, before and after: the new project's, a new
        // diagram's and, while touring, the example's.
        auto names = [this] {
            std::vector<std::string> diagrams{ hypergraph_logic::text(LogicText::tr("Esquema nuevo")) };
            for (int i = 1; i <= project_->getDiagramCount(); ++i)
                diagrams.push_back(Tr::tr("Esquema %1").arg(i).toStdString());
            return std::pair{ std::vector<std::string>{ Tr::tr("Nuevo proyecto").toStdString(),
                                                        Tr::tr("Proyecto de ejemplo").toStdString() },
                              diagrams };
        };
        const auto before = names();
        i18n::apply(language);
        const auto after = names();
        std::vector<std::pair<std::string, std::string>> project_names, diagram_names;
        for (std::size_t i = 0; i < before.first.size(); ++i) project_names.emplace_back(before.first[i], after.first[i]);
        for (std::size_t i = 0; i < before.second.size(); ++i) diagram_names.emplace_back(before.second[i], after.second[i]);
        translateDefaultNames(project_names, diagram_names, tour.has_value());

        if (auto* scene = activeScene()) scene->cancelInteraction();
        const bool panel = action_panel_->isChecked();

        // The new window covers this one, already painted and without the
        // system's animations, before this one goes: the swap shows no flash.
        // This one keeps showing its diagram meanwhile (its scenes go with it;
        // they only touch the editors when the user acts on them).
        auto* next = new MainWindow(std::move(project_));
        next->setAttribute(Qt::WA_DeleteOnClose);
        next->setUpdateChecker(update_checker_);
        next->action_panel_->setChecked(panel);
        native::disableTransitions(this);
        native::disableTransitions(next);
        // A new window shows before it has painted: it starts invisible and
        // appears once painted, all at once.
        next->setWindowOpacity(0.0);
        if (isFullScreen()) next->action_pantalla_completa_->setChecked(true);
        else if (isMaximized()) next->showMaximized();
        else {
            next->setGeometry(geometry());
            next->show();
        }
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 500) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (next->windowHandle() && next->windowHandle()->isExposed()) break;
        }
        // Built before it had its size: the diagram is fitted to the view now.
        next->onAjustarVentana();
        // The tour, if it was on, shows with the window.
        if (tour) next->startTour(*tour);
        next->repaint();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        next->setWindowOpacity(1.0);
        next->raise();
        next->activateWindow();
        emit replaced(next);

        replaced_ = true;
        close();
    }

    void MainWindow::translateDefaultNames(const std::vector<std::pair<std::string, std::string>>& project_names,
        const std::vector<std::pair<std::string, std::string>>& diagram_names, bool touring)
    {
        // The project's, while it has no file (the user has not named it).
        if (project_->getFilePath().empty() || touring)
            for (const auto& [from, to] : project_names)
                if (project_->getName() == from) project_->setName(to);
        // A diagram's: "Esquema nuevo", whatever it holds; while touring, also
        // the example's "Esquema 1", "Esquema 2"…
        for (int i = 0; i < project_->getDiagramCount(); ++i) {
            auto& editor = project_->getEditor(i);
            for (std::size_t k = 0; k < diagram_names.size(); ++k) {
                const auto& [from, to] = diagram_names[k];
                const bool default_name = k == 0;
                if (editor.getName() != from || from == to) continue;
                if (default_name || touring) editor.setNameOutsideHistory(to);
                break;
            }
        }
        project_->syncJointNames();
    }

    void MainWindow::setUpdateChecker(std::function<void()> check) {
        update_checker_ = std::move(check);
        action_actualizaciones_->setVisible(static_cast<bool>(update_checker_));
    }

    bool MainWindow::prepareToCloseForUpdate() {
        // During the tour the open project is the example: nothing to keep.
        closing_for_update_ = tutorial_running_ || mayContinue();
        return closing_for_update_;
    }

    bool MainWindow::mayContinue() {
        if (!project_->hasUnsavedChanges()) return true;
        const auto choice = dialogs::askToSave(this,
            Tr::tr("«%1» tiene cambios sin guardar. Si continúas sin guardarlos, se perderán.")
                .arg(QString::fromStdString(project_->getName())));

        if (choice == dialogs::SaveChoice::Save)
            return project_->getFilePath().empty() ? saveWithPath() : saveToKnownPath();
        return choice == dialogs::SaveChoice::Discard;
    }

} // namespace ui
