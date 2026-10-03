#pragma once

#include "Project.h"
#include "DiagramScene.h"
#include "DiagramView.h"
#include "DiagramTabBar.h"
#include "HelpButton.h"
#include "ViewOverlays.h"

#include <QMainWindow>
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QToolButton>
#include <memory>
#include <vector>

class QLabel;

namespace ui::tutorial {
    enum class Target;
}

namespace ui {

    class HelpNotifier;

    // ============================================================================
    // MainWindow
    //
    // Top-level application window. Layout:
    //
    //   ┌─────────────────────────────────────────────────────────┐
    //   │  Archivo | Editar | Ver | Esquema | Ayuda               │
    //   ├─────────────────────────────────────────────────────────┤
    //   │  DiagramTabBar  [ tab0 | tab1 | ... | [+] ]  [joint]    │
    //   ├─────────────────────────────────────────────────────────┤
    //   │   (selection hint)   ⋮⋮ [Minimizar cruces][fast|slow](?)│
    //   │              DiagramView (central, full area)           │
    //   ├─────────────────────────────────────────────────────────┤
    //   │  6 cajas · 7 conexiones · 3 niveles              100 %  │
    //   └─────────────────────────────────────────────────────────┘
    //
    // The central DiagramView always shows the currently active diagram's scene.
    // The tab bar shows live miniatures of all diagrams. The fixed joint tab sits
    // at the far right of the tab bar.
    //
    // One DiagramScene is created per diagram (and one for the joint). Scenes are
    // created once and reused. The central DiagramView simply swaps which scene
    // it displays when the active tab changes.
    //
    // Undo/redo is per-diagram — Ctrl+Z / Ctrl+Y are forwarded to the currently
    // active editor. Projects can also be opened from the recent list, by
    // dropping a .json file on the window, or from the command line.
    // ============================================================================
    class MainWindow : public QMainWindow {
        Q_OBJECT

    public:
        // Constructs the window with a brand-new project.
        explicit MainWindow(QWidget* parent = nullptr);

        // Constructs the window from an existing loaded project.
        explicit MainWindow(app_logic::Project&& project, QWidget* parent = nullptr);

        // Opens the project at path, asking first about unsaved changes.
        void openProject(const QString& path);

        // ── Pointing the user to Help ─────────────────────────────────────────
        // Entries of the Ayuda menu the user can be pointed to after dismissing
        // something that also lives there: a dot on the menu, then on the entry
        // (see HelpNotifier).
        enum class HelpEntry { Tutorial, GurobiInstall, GurobiLicense };
        void pointToHelpEntry(HelpEntry entry);

        // ── Introductory tour ─────────────────────────────────────────────────
        // Starts it on the first run, i.e. until it has been finished or
        // skipped once (always, with force: the --tutorial option). Skipping it
        // that first time points to its entry in Ayuda.
        void startTutorialIfFirstRun(bool force = false);

    protected:
        void closeEvent(QCloseEvent* event) override;
        void dragEnterEvent(QDragEnterEvent* event) override;
        void dropEvent(QDropEvent* event) override;

    private slots:
        // ── Archivo ───────────────────────────────────────────────────────────────
        void onNuevoProyecto();
        void onNuevoDiagrama();
        void onAbrirProyecto();
        void onGuardarProyecto();
        void onGuardarComo();
        void onExportarImagen();
        void onExportarProyecto();
        void onSalir();

        // ── Editar ────────────────────────────────────────────────────────────────
        void onDeshacer();
        void onRehacer();
        // Ctrl+C: the block under the mouse, or the whole diagram when the mouse
        // is over no box or connection (or not over the diagram). Ctrl+V: pastes
        // where the mouse is, or in the middle of the view (see DiagramScene).
        void onCopiar();
        void onPegar();
        void updatePasteAction();

        // ── Ver ───────────────────────────────────────────────────────────────────
        void onAcercar();
        void onAlejar();
        void onAjustarVentana();
        void onTamanoReal();
        void onPantallaCompleta(bool on);

        // ── Esquema ───────────────────────────────────────────────────────────────
        void onRenombrarEsquema();
        void onEliminarEsquemaActivo();
        // A copy of the diagram as a new one at the end of the tabs, shown
        // straight away (see Project::duplicateDiagram).
        void duplicateDiagram(int index);
        // Right click on a tab: rename, duplicate, delete.
        void showTabMenu(int index, const QPoint& global_pos);
        void onEsquemaSiguiente();
        void onEsquemaAnterior();

        // ── Ayuda ─────────────────────────────────────────────────────────────────
        void onAtajos();
        void onAcercaDe();

        // ── Tab bar ───────────────────────────────────────────────────────────────
        void onTabClicked(int index);
        void onJointTabClicked();
        void onAddTabRequested();
        void onTabRenamed(int index, const QString& new_name);

        // ── Graph changed (scene signals) ─────────────────────────────────────────
        void onGraphChanged();

        // ── Minimize crossings button ─────────────────────────────────────────────
        void onMinimizeCrossings();
        void onRemoveDiagram(int index);

    private:
        // ── Setup ─────────────────────────────────────────────────────────────────
        void setupMenuBar();
        void setupCentralArea();
        void setupStatusBar();
        void setupMinimizePanel(QWidget* host);
        void buildFromProject();

        // Removes every scene and regular tab (before replacing the project).
        void teardownProject();

        // Replaces the current project (already torn down) and rebuilds the UI.
        void adoptProject(std::unique_ptr<app_logic::Project> project);

        // ── Active diagram switching ───────────────────────────────────────────────
        // Switch the central view to show diagram at index (-1 = joint).
        void switchToTab(int index);

        // The scene on screen (regular or joint), or nullptr.
        DiagramScene* activeScene() const;

        // ── Scene management ──────────────────────────────────────────────────────
        // Create and register a new DiagramScene for the regular editor at index.
        DiagramScene* createSceneForEditor(int index);

        // Lets the scene's node dialogs read and extend the project's list of
        // recently used colours (always the current project_).
        void attachColourStore(DiagramScene* scene);

        // Forwards the scene's selection hints to the banner while it is shown.
        void connectHint(DiagramScene* scene);

        // ── State sync ────────────────────────────────────────────────────────────
        void updateUndoRedoActions();
        void updateWindowTitle();
        // The editors' onMutated callback: refreshes the "*" in the title. An
        // editor can change on a worker thread (crossing minimisation), and
        // widgets may only be touched from the GUI thread, so the refresh is
        // always carried out there.
        void onEditorMutated();
        void updateStatusBar();
        void updateZoomLabel();

        // Tells the user an export finished, offering to open what was written
        // (a PDF or a folder) with the system's default application.
        void showExportDone(const QString& title, const QString& text,
            const QString& open_text, const QString& target);

        // Folder to propose in file dialogs: the project's own, else Documents.
        QString defaultExportFolder() const;

        // ── Recent projects (kept in QSettings) ──────────────────────────────────
        QStringList recentProjects() const;
        void rememberRecentProject(const QString& path);
        void forgetRecentProject(const QString& path);
        void rebuildRecentMenu();

        // ── Unsaved changes guard ─────────────────────────────────────────────────
        // Returns true if it is safe to proceed (no unsaved changes, or user
        // chose to save or discard them).
        bool mayContinue();

        // ── Introductory tour ─────────────────────────────────────────────────────
        void runTutorial(bool first_run);
        // Puts the user's project back once the tour is over: from its file,
        // or a new one if it had none.
        void restoreAfterTutorial(const std::filesystem::path& own_file);
        // Project-replacing commands wait for the tour to end (it shows the
        // example project); true when one was refused for that.
        bool refuseDuringTutorial();
        // Where a part the tour points at is, in this window's coordinates.
        QRect tutorialTarget(tutorial::Target target) const;
        bool tutorial_running_ = false;

        HelpNotifier* help_notifier_ = nullptr;

        // ── Save helpers ──────────────────────────────────────────────────────────
        bool saveWithPath();   // prompts for a path
        bool saveToKnownPath();

        // ── Data ──────────────────────────────────────────────────────────────────
        std::unique_ptr<app_logic::Project> project_;

        // One DiagramScene per regular diagram, plus one for the joint.
        // Indexed in parallel with project_.editors_.
        std::vector<DiagramScene*> scenes_;
        DiagramScene* joint_scene_ = nullptr;

        // Central editing view — swaps scene on tab switch.
        DiagramView* central_view_;

        // Tab bar
        DiagramTabBar* tab_bar_;

        // Floating, draggable panel over the view holding the "Minimizar cruces"
        // button (its icon shows the current mode: lightning or turtle), the
        // fast/slow switch and a "?" button whose hover popup explains both
        // modes (see HelpButton).
        FloatingPanel* minimize_panel_;
        QToolButton* minimize_crossings_btn_;
        QToolButton* mode_fast_btn_;
        QToolButton* mode_slow_btn_;
        HelpButton* minimize_help_btn_;

        // Which mode onMinimizeCrossings() currently runs; the panel's switch and
        // the Esquema menu's mode entries stay in sync with it.
        enum class MinimizeMode { Fast, Slow };
        MinimizeMode minimize_mode_ = MinimizeMode::Fast;
        void setMinimizeMode(MinimizeMode mode);

        // "Elige la caja..." pill shown over the view while a two-click operation waits.
        HintBanner* hint_banner_;

        // Status bar: diagram statistics on the left, zoom on the right.
        QLabel* stats_label_ = nullptr;
        QLabel* zoom_label_ = nullptr;

        // ── Per-tab zoom state ───────────────────────────────────────────────────────
        // zoom_levels_[i] stores the last zoom factor for regular diagram i.
        // A value of 0.0 means "never viewed — fit on first show".
        std::vector<double> zoom_levels_;
        double              joint_zoom_ = 0.0;

        // ── Menu actions ──────────────────────────────────────────────────────────
        QMenu* recent_menu_ = nullptr;
        QAction* action_deshacer_;
        QAction* action_rehacer_;
        QAction* action_renombrar_;
        QAction* action_eliminar_esquema_;
        QAction* action_duplicar_esquema_ = nullptr;
        QAction* action_pegar_ = nullptr;
        QAction* action_menu_fast_ = nullptr;
        QAction* action_menu_slow_ = nullptr;
        QAction* action_panel_;
        QAction* action_pantalla_completa_;
    };

} // namespace ui
