#include "MainWindow.h"
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
#include <QVBoxLayout>
#include <QWidget>

using namespace app_logic;
using namespace hypergraph_logic;

namespace ui {

    namespace {
        constexpr int MAX_RECENT_PROJECTS = 8;
        const char* const RECENT_KEY = "recentProjects";
        const char* const PROJECT_FILTER = "Proyectos de Taller Matrix Harris (*.json)";

        // "1 caja" / "3 cajas".
        QString count(qsizetype n, const QString& one, const QString& many) {
            return QStringLiteral("%1 %2").arg(n).arg(n == 1 ? one : many);
        }
    }

    // ============================================================================
    // Construction
    // ============================================================================

    MainWindow::MainWindow(QWidget* parent)
        : MainWindow(Project("Nuevo proyecto"), parent)
    {
    }

    MainWindow::MainWindow(Project&& project, QWidget* parent)
        : QMainWindow(parent)
        , project_(std::make_unique<Project>(std::move(project)))
    {
        setAcceptDrops(true);
        setupMenuBar();
        setupCentralArea();
        setupStatusBar();
        buildFromProject();
        updateWindowTitle();
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
        QMenu* archivo = addMenu(QStringLiteral("Archivo"));
        add(archivo, QStringLiteral("Nuevo proyecto"), QKeySequence::New, this, &MainWindow::onNuevoProyecto);
        add(archivo, QStringLiteral("Abrir proyecto…"), QKeySequence::Open, this, &MainWindow::onAbrirProyecto);
        recent_menu_ = style::createMenu(archivo);
        recent_menu_->setTitle(QStringLiteral("Abrir reciente"));
        recent_menu_->setToolTipsVisible(true);
        archivo->addMenu(recent_menu_);
        connect(recent_menu_, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
        archivo->addSeparator();
        add(archivo, QStringLiteral("Guardar"), QKeySequence::Save, this, &MainWindow::onGuardarProyecto);
        add(archivo, QStringLiteral("Guardar como…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")),
            this, &MainWindow::onGuardarComo);
        archivo->addSeparator();
        add(archivo, QStringLiteral("Exportar esquema como imagen…"), QKeySequence(QStringLiteral("Ctrl+E")),
            this, &MainWindow::onExportarImagen);
        add(archivo, QStringLiteral("Exportar proyecto…"), QKeySequence(QStringLiteral("Ctrl+Shift+E")),
            this, &MainWindow::onExportarProyecto);
        archivo->addSeparator();
        add(archivo, QStringLiteral("Salir"), QKeySequence(QStringLiteral("Ctrl+Q")), this, &MainWindow::onSalir);

        // ── Editar ────────────────────────────────────────────────────────────────
        QMenu* editar = addMenu(QStringLiteral("Editar"));
        action_deshacer_ = add(editar, QStringLiteral("Deshacer"), QKeySequence::Undo, this, &MainWindow::onDeshacer);
        action_deshacer_->setEnabled(false);
        action_rehacer_ = add(editar, QStringLiteral("Rehacer"), QKeySequence(QStringLiteral("Ctrl+Y")),
            this, &MainWindow::onRehacer);
        action_rehacer_->setShortcuts({ QKeySequence(QStringLiteral("Ctrl+Y")), QKeySequence(QStringLiteral("Ctrl+Shift+Z")) });
        action_rehacer_->setEnabled(false);
        editar->addSeparator();
        QAction* copiar = add(editar, QStringLiteral("Copiar"), QKeySequence::Copy, this, &MainWindow::onCopiar);
        copiar->setIcon(style::icon(style::Icon::Copy));
        copiar->setToolTip(QStringLiteral("Copia el bloque que está bajo el ratón, o el esquema entero"));
        action_pegar_ = add(editar, QStringLiteral("Pegar"), QKeySequence::Paste, this, &MainWindow::onPegar);
        action_pegar_->setIcon(style::icon(style::Icon::Paste));
        action_pegar_->setToolTip(QStringLiteral("Pega lo copiado donde está el ratón"));
        action_pegar_->setEnabled(false);
        // Also when something is copied in another window or program.
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::updatePasteAction);

        // ── Ver ───────────────────────────────────────────────────────────────────
        QMenu* ver = addMenu(QStringLiteral("Ver"));
        QAction* acercar = add(ver, QStringLiteral("Acercar"), QKeySequence::ZoomIn, this, &MainWindow::onAcercar);
        acercar->setShortcuts({ QKeySequence(QStringLiteral("Ctrl++")), QKeySequence(QStringLiteral("Ctrl+=")) });
        add(ver, QStringLiteral("Alejar"), QKeySequence::ZoomOut, this, &MainWindow::onAlejar);
        add(ver, QStringLiteral("Ajustar a la ventana"), QKeySequence(QStringLiteral("Ctrl+0")),
            this, &MainWindow::onAjustarVentana);
        add(ver, QStringLiteral("Tamaño real (100 %)"), QKeySequence(QStringLiteral("Ctrl+1")),
            this, &MainWindow::onTamanoReal);
        ver->addSeparator();
        action_panel_ = ver->addAction(QStringLiteral("Panel «Minimizar cruces»"));
        action_panel_->setCheckable(true);
        action_panel_->setChecked(true);
        connect(action_panel_, &QAction::toggled, this, [this](bool on) { minimize_panel_->setVisible(on); });
        QAction* barra = ver->addAction(QStringLiteral("Barra de estado"));
        barra->setCheckable(true);
        barra->setChecked(true);
        connect(barra, &QAction::toggled, this, [this](bool on) { statusBar()->setVisible(on); });
        ver->addSeparator();
        action_pantalla_completa_ = ver->addAction(QStringLiteral("Pantalla completa"));
        action_pantalla_completa_->setShortcut(QKeySequence(QStringLiteral("F11")));
        action_pantalla_completa_->setCheckable(true);
        connect(action_pantalla_completa_, &QAction::toggled, this, &MainWindow::onPantallaCompleta);

        // ── Esquema ───────────────────────────────────────────────────────────────
        QMenu* esquema = addMenu(QStringLiteral("Esquema"));
        QAction* nuevo = add(esquema, QStringLiteral("Nuevo esquema"), QKeySequence(QStringLiteral("Ctrl+T")),
            this, &MainWindow::onNuevoDiagrama);
        nuevo->setIcon(style::icon(style::Icon::NewBox));
        action_renombrar_ = add(esquema, QStringLiteral("Renombrar esquema"), QKeySequence(QStringLiteral("F2")),
            this, &MainWindow::onRenombrarEsquema);
        action_eliminar_esquema_ = add(esquema, QStringLiteral("Eliminar esquema…"), QKeySequence(),
            this, &MainWindow::onEliminarEsquemaActivo);
        action_eliminar_esquema_->setIcon(style::icon(style::Icon::RemoveBox));
        action_duplicar_esquema_ = add(esquema, QStringLiteral("Duplicar esquema"), QKeySequence(QStringLiteral("Ctrl+D")),
            this, [this] { duplicateDiagram(project_->getActiveIndex()); });
        action_duplicar_esquema_->setIcon(style::icon(style::Icon::Duplicate));
        esquema->addSeparator();
        add(esquema, QStringLiteral("Esquema siguiente"), QKeySequence(QStringLiteral("Ctrl+Tab")),
            this, &MainWindow::onEsquemaSiguiente);
        add(esquema, QStringLiteral("Esquema anterior"), QKeySequence(QStringLiteral("Ctrl+Shift+Tab")),
            this, &MainWindow::onEsquemaAnterior);
        QAction* conjunto = add(esquema, QStringLiteral("Ir al esquema conjunto"), QKeySequence(QStringLiteral("Ctrl+J")),
            this, &MainWindow::onJointTabClicked);
        conjunto->setIcon(style::icon(style::Icon::Joint));
        esquema->addSeparator();
        add(esquema, QStringLiteral("Minimizar cruces"), QKeySequence(QStringLiteral("Ctrl+M")),
            this, &MainWindow::onMinimizeCrossings);
        auto* modes = new QActionGroup(this);
        // No icons here: the check mark is what shows the current mode.
        action_menu_fast_ = esquema->addAction(QStringLiteral("Modo rápido"));
        action_menu_slow_ = esquema->addAction(QStringLiteral("Modo lento"));
        for (QAction* a : { action_menu_fast_, action_menu_slow_ }) {
            a->setCheckable(true);
            modes->addAction(a);
        }
        action_menu_fast_->setChecked(true);
        connect(action_menu_fast_, &QAction::triggered, this, [this] { mode_fast_btn_->setChecked(true); });
        connect(action_menu_slow_, &QAction::triggered, this, [this] { mode_slow_btn_->setChecked(true); });

        // ── Ayuda ─────────────────────────────────────────────────────────────────
        QMenu* ayuda = addMenu(QStringLiteral("Ayuda"));
        QAction* tutorial = add(ayuda, QStringLiteral("Tutorial de introducción"), QKeySequence(), this,
            [this] { runTutorial(false); });
        add(ayuda, QStringLiteral("Atajos y controles"), QKeySequence(QStringLiteral("F1")), this, &MainWindow::onAtajos);
        add(ayuda, QStringLiteral("Ayuda de las opciones"), QKeySequence(), this,
            [this] { help::showOptionsHelp(this); });
        ayuda->addSeparator();
        QAction* install = add(ayuda, QStringLiteral("Cómo instalar Gurobi"), QKeySequence(), this,
            [this] { gurobi::showGuide(gurobi::Guide::Install, this); });
        QAction* license = add(ayuda, QStringLiteral("Cómo obtener una licencia de Gurobi"), QKeySequence(), this,
            [this] { gurobi::showGuide(gurobi::Guide::License, this); });
        ayuda->addSeparator();
        // Shown once there is something to check with (setUpdateChecker).
        action_actualizaciones_ = add(ayuda, QStringLiteral("Buscar actualizaciones…"), QKeySequence(), this,
            [this] { if (update_checker_) update_checker_(); });
        action_actualizaciones_->setVisible(false);
        add(ayuda, QStringLiteral("Acerca de Taller Matrix Harris"), QKeySequence(), this, &MainWindow::onAcercaDe);

        help_notifier_ = new HelpNotifier(menuBar(), ayuda, this);
        help_notifier_->addEntry(QStringLiteral("tutorial"), tutorial);
        help_notifier_->addEntry(QStringLiteral("gurobiInstall"), install);
        help_notifier_->addEntry(QStringLiteral("gurobiLicense"), license);
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
        // A moment for the window to settle on screen first.
        QTimer::singleShot(300, this, [this] { runTutorial(true); });
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
            adoptProject(std::make_unique<Project>("Nuevo proyecto"));
        }
        tutorial_running_ = true;

        auto* tour = new tutorial::TutorialOverlay(this, tutorial::introSteps(),
            [this](tutorial::Target target) { return tutorialTarget(target); },
            [this](tutorial::View view) {
                if (view == tutorial::View::Joint) switchToTab(-1);
                else if (view == tutorial::View::FirstDiagram && project_->getDiagramCount() > 0) switchToTab(0);
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
        adoptProject(std::make_unique<Project>("Nuevo proyecto"));
    }

    bool MainWindow::refuseDuringTutorial() {
        if (!tutorial_running_) return false;
        statusBar()->showMessage(
            QStringLiteral("Termina o salta el tutorial para cambiar de proyecto"), 4000);
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
        case T::FileMenu:          return menuTitle(QStringLiteral("Archivo"));
        case T::EditMenu:          return menuTitle(QStringLiteral("Editar"));
        case T::HelpMenu:          return menuTitle(QStringLiteral("Ayuda"));
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
        zoom_label_->setToolTip(QStringLiteral("Zoom (Ctrl+rueda para cambiarlo, Ctrl+0 para ajustar)"));
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
        minimize_crossings_btn_->setText(QStringLiteral("Minimizar cruces"));
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
            QStringLiteral("Rápido: el mejor dibujo en 5 segundos como máximo"));
        mode_slow_btn_ = makeModeButton(style::Icon::Slow,
            QStringLiteral("Lento: sin límite de tiempo; se puede pausar"));
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
        minimize_help_btn_->setToolTip(QStringLiteral(
            "<div style=\"color:#1F2330; font-size:10.5pt;\"><b>Minimizar cruces</b></div>"
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
            ? QStringLiteral("Minimizar cruces en modo rápido (Ctrl+M)")
            : QStringLiteral("Minimizar cruces en modo lento (Ctrl+M)"));
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
                        dialogs::showError(this, QStringLiteral("No se ha podido añadir el esquema"),
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
        QString title = QString::fromStdString(project_->getName());
        if (project_->hasUnsavedChanges()) title += QStringLiteral(" *");
        setWindowTitle(title + QStringLiteral(" — Taller Matrix Harris"));
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
        stats_label_->setText(QStringLiteral("%1   ·   %2   ·   %3").arg(
            count(boxes, QStringLiteral("caja"), QStringLiteral("cajas")),
            count(connections, QStringLiteral("conexión"), QStringLiteral("conexiones")),
            count(g->getLayerCount(), QStringLiteral("nivel"), QStringLiteral("niveles"))));
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
        adoptProject(std::make_unique<Project>("Nuevo proyecto"));
        statusBar()->showMessage(QStringLiteral("Nuevo proyecto creado"), 3000);
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
            this, QStringLiteral("Abrir proyecto"), start, QString::fromUtf8(PROJECT_FILTER));
        if (!path.isEmpty()) openProject(path);
    }

    void MainWindow::openProject(const QString& path) {
        if (refuseDuringTutorial()) return;
        if (!QFileInfo::exists(path)) {
            dialogs::showError(this, QStringLiteral("No se encuentra el proyecto"),
                QStringLiteral("El archivo «%1» ya no existe o se ha movido.").arg(path));
            forgetRecentProject(path);
            return;
        }
        if (!mayContinue()) return;

        teardownProject();
        project_.reset(); // only one project (and joint graph) may exist at a time
        try {
            adoptProject(Project::load(path.toStdString()));
            rememberRecentProject(path);
            statusBar()->showMessage(QStringLiteral("Proyecto abierto: %1").arg(QFileInfo(path).fileName()), 3000);
        }
        catch (const std::exception& e) {
            adoptProject(std::make_unique<Project>("Nuevo proyecto"));
            dialogs::showError(this, QStringLiteral("No se ha podido abrir el proyecto"),
                QStringLiteral("«%1» no es un proyecto válido del Taller Matrix Harris.\n\n%2")
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
            this, QStringLiteral("Guardar proyecto"), suggested, QString::fromUtf8(PROJECT_FILTER));
        if (path.isEmpty()) return false;
        try {
            project_->setName(QFileInfo(path).completeBaseName().toStdString());
            project_->save(path.toStdString());
            rememberRecentProject(path);
            updateWindowTitle();
            statusBar()->showMessage(QStringLiteral("Proyecto guardado en %1").arg(QFileInfo(path).fileName()), 3000);
            return true;
        }
        catch (const std::exception& e) {
            dialogs::showError(this, QStringLiteral("No se ha podido guardar el proyecto"),
                QString::fromStdString(e.what()));
            return false;
        }
    }

    bool MainWindow::saveToKnownPath() {
        try {
            project_->save();
            updateWindowTitle();
            statusBar()->showMessage(QStringLiteral("Proyecto guardado"), 2500);
            return true;
        }
        catch (const std::exception& e) {
            dialogs::showError(this, QStringLiteral("No se ha podido guardar el proyecto"),
                QString::fromStdString(e.what()));
            return false;
        }
    }

    void MainWindow::onExportarImagen() {
        DiagramScene* scene = activeScene();
        if (!scene || scene->items().isEmpty()) {
            dialogs::showInfo(this, QStringLiteral("No hay nada que exportar"),
                QStringLiteral("Este esquema está vacío. Añade alguna caja y vuelve a intentarlo."));
            return;
        }

        const int idx = project_->getActiveIndex();
        const QString name = idx == -1 ? QStringLiteral("Esquema conjunto")
                                       : QString::fromStdString(project_->getDiagramName(idx));
        QString selected;
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar esquema"),
            QDir(defaultExportFolder()).filePath(exporting::safeFileName(name) + QStringLiteral(".png")),
            QStringLiteral("Imagen PNG (*.png);;Imagen JPEG (*.jpg *.jpeg);;Documento PDF (*.pdf)"),
            &selected);
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty())
            path += selected.contains("pdf") ? ".pdf" : selected.contains("JPEG") ? ".jpg" : ".png";

        // Picture the diagram as it is, with no selection in progress.
        scene->cancelInteraction();
        const bool ok = QFileInfo(path).suffix().compare("pdf", Qt::CaseInsensitive) == 0
            ? exporting::writePdf(path, QString::fromStdString(project_->getName()), { { name, scene } })
            : exporting::writeImage(path, scene);

        if (ok) statusBar()->showMessage(QStringLiteral("Esquema exportado a %1").arg(QFileInfo(path).fileName()), 4000);
        else    dialogs::showError(this, QStringLiteral("No se ha podido exportar el esquema"),
                    QStringLiteral("No se ha podido escribir el archivo «%1».").arg(path));
    }

    void MainWindow::onExportarProyecto() {
        // Every diagram in tab order, then the joint one.
        std::vector<exporting::Page> pages;
        for (int i = 0; i < project_->getDiagramCount(); ++i)
            pages.push_back({ QString::fromStdString(project_->getDiagramName(i)), scenes_[i] });
        pages.push_back({ QStringLiteral("Esquema conjunto"), joint_scene_ });
        const QString project_name = QString::fromStdString(project_->getName());

        // ── Choose the format ────────────────────────────────────────────────
        StyledDialog dlg(StyledDialog::Badge::App, QStringLiteral("Exportar proyecto"), this);
        dlg.setMessage(QStringLiteral("Incluye %1 de «%2» y el esquema conjunto. ¿En qué formato quieres exportarlo?")
            .arg(count(project_->getDiagramCount(), QStringLiteral("esquema"), QStringLiteral("esquemas")),
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
            QStringLiteral("Documento PDF\nUna página por esquema y el esquema conjunto al final"));
        QToolButton* as_png = option(style::Icon::ImageFolder,
            QStringLiteral("Imágenes PNG\nUna carpeta «%1» con una imagen por esquema").arg(project_name));
        auto* group = new QButtonGroup(options);
        group->addButton(as_pdf);
        group->addButton(as_png);
        QSettings settings;
        (settings.value("exportFormat").toString() == "png" ? as_png : as_pdf)->setChecked(true);
        dlg.setBody(options);
        dlg.addButton(QStringLiteral("Cancelar"), 0, StyledDialog::ButtonStyle::Secondary, false, true);
        dlg.addButton(QStringLiteral("Exportar"), 1, StyledDialog::ButtonStyle::Primary, true);
        dlg.exec();
        if (dlg.choice() != 1) return;
        const bool pdf = as_pdf->isChecked();
        settings.setValue("exportFormat", pdf ? "pdf" : "png");

        // Picture every diagram as it is, with no selection in progress.
        for (auto* s : scenes_) s->cancelInteraction();
        joint_scene_->cancelInteraction();

        if (pdf) {
            QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar proyecto como PDF"),
                QDir(defaultExportFolder()).filePath(exporting::safeFileName(project_name) + QStringLiteral(".pdf")),
                QStringLiteral("Documento PDF (*.pdf)"));
            if (path.isEmpty()) return;
            if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".pdf");

            QApplication::setOverrideCursor(Qt::WaitCursor);
            const bool ok = exporting::writePdf(path, project_name, pages);
            QApplication::restoreOverrideCursor();
            if (!ok) {
                dialogs::showError(this, QStringLiteral("No se ha podido exportar el proyecto"),
                    QStringLiteral("No se ha podido escribir el archivo «%1».").arg(path));
                return;
            }
            showExportDone(QStringLiteral("Proyecto exportado"),
                QStringLiteral("«%1» tiene %2, una por esquema.")
                    .arg(QFileInfo(path).fileName(), count(static_cast<qsizetype>(pages.size()),
                        QStringLiteral("página"), QStringLiteral("páginas"))),
                QStringLiteral("Abrir PDF"), path);
            return;
        }

        // ── PNG: a new folder named after the project ─────────────────────────
        const QString parent_dir = QFileDialog::getExistingDirectory(this,
            QStringLiteral("Elige dónde crear la carpeta «%1»").arg(project_name), defaultExportFolder());
        if (parent_dir.isEmpty()) return;

        // Never mix with an earlier export: "Proyecto", "Proyecto (2)", ...
        const QString base = exporting::safeFileName(project_name);
        QString folder = QDir(parent_dir).filePath(base);
        for (int n = 2; QFileInfo::exists(folder); ++n)
            folder = QDir(parent_dir).filePath(QStringLiteral("%1 (%2)").arg(base).arg(n));
        if (!QDir().mkpath(folder)) {
            dialogs::showError(this, QStringLiteral("No se ha podido exportar el proyecto"),
                QStringLiteral("No se ha podido crear la carpeta «%1».").arg(folder));
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
            dialogs::showError(this, QStringLiteral("No se han podido exportar todas las imágenes"),
                QStringLiteral("No se han podido escribir: %1.").arg(failed.join(QStringLiteral(", "))));
            return;
        }
        QString text = written.isEmpty()
            ? QStringLiteral("Todos los esquemas están vacíos, así que la carpeta «%1» no contiene imágenes.")
                  .arg(QFileInfo(folder).fileName())
            : QStringLiteral("La carpeta «%1» contiene %2.").arg(QFileInfo(folder).fileName(),
                  count(written.size(), QStringLiteral("imagen"), QStringLiteral("imágenes")));
        if (!empty.isEmpty() && !written.isEmpty())
            text += QStringLiteral("\n\nSin imagen por estar vacíos: %1.").arg(empty.join(QStringLiteral(", ")));
        showExportDone(QStringLiteral("Proyecto exportado"), text, QStringLiteral("Abrir carpeta"), folder);
    }

    void MainWindow::showExportDone(const QString& title, const QString& text,
        const QString& open_text, const QString& target)
    {
        StyledDialog done(StyledDialog::Badge::Success, title, this);
        done.setMessage(text);
        done.addButton(QStringLiteral("Cerrar"), 0, StyledDialog::ButtonStyle::Secondary, false, true);
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
            recent_menu_->addAction(QStringLiteral("No hay proyectos recientes"))->setEnabled(false);
            return;
        }
        int n = 1;
        for (const QString& path : list) {
            QAction* a = recent_menu_->addAction(QStringLiteral("%1   %2").arg(n++).arg(QFileInfo(path).completeBaseName()));
            a->setToolTip(QDir::toNativeSeparators(path));
            connect(a, &QAction::triggered, this, [this, path] { openProject(path); });
        }
        recent_menu_->addSeparator();
        connect(recent_menu_->addAction(QStringLiteral("Borrar la lista")), &QAction::triggered, this, [] {
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
            dialogs::showWarning(this, QStringLiteral("No se puede deshacer"), QString::fromStdString(e.what()));
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
            statusBar()->showMessage(QStringLiteral("No hay nada que copiar"), 3000);
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
        action_pegar_->setToolTip(regular ? QStringLiteral("Pega lo copiado donde está el ratón")
                                          : QStringLiteral("No se puede pegar en el esquema conjunto"));
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
            dialogs::showWarning(this, QStringLiteral("No se puede rehacer"), QString::fromStdString(e.what()));
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
        statusBar()->showMessage(QStringLiteral("«%1» duplicado como «%2»").arg(original, name), 4000);
    }

    void MainWindow::showTabMenu(int index, const QPoint& global_pos) {
        if (index < 0 || index >= project_->getDiagramCount()) return;
        QMenu* menu = style::createMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->addAction(QStringLiteral("Renombrar"), this, [this, index] { tab_bar_->startRename(index); });
        menu->addAction(style::icon(style::Icon::Duplicate), QStringLiteral("Duplicar esquema"),
            this, [this, index] { duplicateDiagram(index); });
        menu->addSeparator();
        menu->addAction(style::icon(style::Icon::RemoveBox), QStringLiteral("Eliminar esquema…"),
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
                dialogs::showInfo(this, QStringLiteral("No hay cruces que minimizar"),
                    QStringLiteral("En este esquema ninguna conexión puede cruzarse con otra, "
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
            dialogs::showError(this, QStringLiteral("No se han podido minimizar los cruces"),
                QString::fromStdString(e.what()));
        }
    }

    void MainWindow::onRemoveDiagram(int index) {
        if (index < 0 || index >= static_cast<int>(scenes_.size())) return;

        const QString name = QString::fromStdString(project_->getDiagramName(index));
        if (!dialogs::confirm(this, QStringLiteral("¿Eliminar «%1»?").arg(name),
                QStringLiteral("Se eliminará el esquema con todas sus cajas y conexiones. "
                               "Esta acción no se puede deshacer."),
                QStringLiteral("Eliminar"), true))
            return;

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
        statusBar()->showMessage(QStringLiteral("Esquema «%1» eliminado").arg(name), 3000);
    }

    // ============================================================================
    // Ayuda
    // ============================================================================

    void MainWindow::onAtajos() {
        StyledDialog dlg(StyledDialog::Badge::Info, QStringLiteral("Atajos y controles"), this);
        dlg.setMessage(QStringLiteral("Todo lo que puedes hacer con el teclado y el ratón."));

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
            { QStringLiteral("Proyecto"), {
                { QStringLiteral("Ctrl+N"), QStringLiteral("Nuevo proyecto") },
                { QStringLiteral("Ctrl+O"), QStringLiteral("Abrir proyecto") },
                { QStringLiteral("Ctrl+S"), QStringLiteral("Guardar") },
                { QStringLiteral("Ctrl+Mayús+S"), QStringLiteral("Guardar como") },
                { QStringLiteral("Ctrl+E"), QStringLiteral("Exportar el esquema") },
                { QStringLiteral("Ctrl+Mayús+E"), QStringLiteral("Exportar el proyecto") } } },
            { QStringLiteral("Edición"), {
                { QStringLiteral("Ctrl+Z"), QStringLiteral("Deshacer") },
                { QStringLiteral("Ctrl+Y"), QStringLiteral("Rehacer") },
                { QStringLiteral("Ctrl+C"), QStringLiteral("Copiar el bloque bajo el ratón (o el esquema)") },
                { QStringLiteral("Ctrl+V"), QStringLiteral("Pegar donde está el ratón") },
                { QStringLiteral("Esc"), QStringLiteral("Cancelar la selección en curso") } } },
            { QStringLiteral("Esquemas"), {
                { QStringLiteral("Ctrl+T"), QStringLiteral("Nuevo esquema") },
                { QStringLiteral("F2"), QStringLiteral("Renombrar esquema") },
                { QStringLiteral("Ctrl+D"), QStringLiteral("Duplicar esquema") },
                { QStringLiteral("Ctrl+Tab"), QStringLiteral("Esquema siguiente") },
                { QStringLiteral("Ctrl+J"), QStringLiteral("Esquema conjunto") },
                { QStringLiteral("Ctrl+M"), QStringLiteral("Minimizar cruces") } } },
        });
        column({
            { QStringLiteral("Vista"), {
                { QStringLiteral("Ctrl+rueda"), QStringLiteral("Acercar / alejar") },
                { QStringLiteral("Ctrl+0"), QStringLiteral("Ajustar a la ventana") },
                { QStringLiteral("Ctrl+1"), QStringLiteral("Tamaño real") },
                { QStringLiteral("Arrastrar fondo"), QStringLiteral("Desplazar la vista") },
                { QStringLiteral("F11"), QStringLiteral("Pantalla completa") } } },
            { QStringLiteral("Cajas y conexiones"), {
                { QStringLiteral("Clic derecho"), QStringLiteral("Menú de la caja o del fondo") },
                { QStringLiteral("Clic en conexión"), QStringLiteral("Menú de la conexión") },
                { QStringLiteral("Doble clic"), QStringLiteral("Propiedades de la caja") },
                { QStringLiteral("Arrastrar caja"), QStringLiteral("Moverla de posición o nivel") },
                { QStringLiteral("Rueda sobre caja"), QStringLiteral("Desplazar un texto largo") } } },
            { QStringLiteral("Pestañas"), {
                { QStringLiteral("Doble clic"), QStringLiteral("Renombrar (sobre el nombre)") },
                { QStringLiteral("Clic derecho"), QStringLiteral("Renombrar, duplicar o eliminar") },
                { QStringLiteral("Ctrl+rueda"), QStringLiteral("Zoom en la miniatura") } } },
        });

        dlg.setBody(body);
        dlg.addButton(QStringLiteral("Cerrar"), 0, StyledDialog::ButtonStyle::Primary, true, true);
        dlg.exec();
    }

    void MainWindow::onAcercaDe() {
        StyledDialog dlg(StyledDialog::Badge::App, QStringLiteral("Taller Matrix Harris"), this);
        // The version is the project's (CMakeLists.txt), set by main().
        dlg.setMessage(QStringLiteral(
            "Versión %1\n\n"
            "Herramienta para crear, organizar y dibujar esquemas Matrix Harris, "
            "así como cualquier otro tipo de esquema jerárquico, "
            "con un dibujo automático que minimiza los cruces.\n\n"
            "Desarrollado por Javier San Andrés.\n").arg(QCoreApplication::applicationVersion()));
        dlg.addButton(QStringLiteral("Cerrar"), 0, StyledDialog::ButtonStyle::Primary, true, true);
        dlg.exec();
    }

    // ============================================================================
    // Close, drag and drop
    // ============================================================================

    void MainWindow::closeEvent(QCloseEvent* event) {
        // During the tour the open project is the example: nothing to keep
        // (the user's own was saved or dropped when the tour began).
        if (tutorial_running_ || closing_for_update_) { // nothing to keep / already asked
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
            QStringLiteral("«%1» tiene cambios sin guardar. Si continúas sin guardarlos, se perderán.")
                .arg(QString::fromStdString(project_->getName())));

        if (choice == dialogs::SaveChoice::Save)
            return project_->getFilePath().empty() ? saveWithPath() : saveToKnownPath();
        return choice == dialogs::SaveChoice::Discard;
    }

} // namespace ui
