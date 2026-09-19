#pragma once

#include "Project.h"
#include "DiagramScene.h"
#include "DiagramView.h"
#include "DiagramTabBar.h"
#include "HelpButton.h"

#include <QMainWindow>
#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QToolButton>
#include <memory>
#include <vector>

namespace ui {

    // ============================================================================
    // MainWindow
    //
    // Top-level application window. Layout:
    //
    //   ┌─────────────────────────────────────────────────────────┐
    //   │  Menú: Archivo | Editar                                 │
    //   ├─────────────────────────────────────────────────────────┤
    //   │  DiagramTabBar  [ tab0 | tab1 | ... | [+] ]  [joint]   │
    //   ├─────────────────────────────────────────────────────────┤
    //   │                        [Minimizar cruces (Rápido) ▾][?] │
    //   │              DiagramView (central)                      │
    //   │                                                         │
    //   └─────────────────────────────────────────────────────────┘
    //
    // The central DiagramView always shows the currently active diagram's scene.
    // The tab bar shows miniatures of all diagrams; the active tab shows only its
    // name. The fixed joint tab sits at the far right of the tab bar.
    //
    // One DiagramScene is created per diagram (and one for the joint). Scenes are
    // created once and reused. The central DiagramView simply swaps which scene
    // it displays when the active tab changes.
    //
    // Undo/redo is per-diagram — Ctrl+Z / Ctrl+Y are forwarded to the currently
    // active editor.
    // ============================================================================
    class MainWindow : public QMainWindow {
        Q_OBJECT

    public:
        // Constructs the window with a brand-new project.
        explicit MainWindow(QWidget* parent = nullptr);

        // Constructs the window from an existing loaded project.
        explicit MainWindow(app_logic::Project&& project, QWidget* parent = nullptr);

    protected:
        void closeEvent(QCloseEvent* event) override;

    private slots:
        // ── Archivo menu ──────────────────────────────────────────────────────────
        void onNuevoDiagrama();
        void onAbrirProyecto();
        void onGuardarProyecto();
        void onGuardarComo();
        void onSalir();

        // ── Editar menu ───────────────────────────────────────────────────────────
        void onDeshacer();
        void onRehacer();
        void onAcercar();
        void onAlejar();

        // ── Tab bar ───────────────────────────────────────────────────────────────
        void onTabClicked(int index);
        void onJointTabClicked();
        void onAddTabRequested();
        void onTabRenamed(int index, const QString& new_name);

        // ── Graph changed (scene signals) ─────────────────────────────────────────
        void onGraphChanged();

        // ── Minimize crossings button ─────────────────────────────────────────────
        void onMinimizeCrossings();
        void onMinimizeModeChanged(QAction* action);
        void onRemoveDiagram(int index);

    private:
        // ── Setup ─────────────────────────────────────────────────────────────────
        void setupMenuBar();
        void setupCentralArea();
        void setupTabBar();
        void buildFromProject();

        // ── Active diagram switching ───────────────────────────────────────────────
        // Switch the central view to show diagram at index (-1 = joint).
        void switchToTab(int index);

        // ── Scene management ──────────────────────────────────────────────────────
        // Create and register a new DiagramScene for the regular editor at index.
        DiagramScene* createSceneForEditor(int index);

        // ── Undo / redo state sync ────────────────────────────────────────────────
        void updateUndoRedoActions();

        // ── Unsaved changes guard ─────────────────────────────────────────────────
        // Returns true if it is safe to proceed (no unsaved changes, or user
        // chose to discard them).
        bool mayContinue();

        // ── Save helpers ──────────────────────────────────────────────────────────
        bool saveWithPath();   // prompts for path if not set
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

        // "Minimizar cruces" split button (top-right corner of central area).
        // Its label always reflects minimize_mode_ ("... (Rápido)" / "... (Lento)");
        // clicking its main body runs onMinimizeCrossings() in that mode, and its
        // side arrow opens a menu (action_mode_fast_ / action_mode_slow_, in
        // minimize_mode_group_ so exactly one is ever checked) to change it.
        QToolButton* minimize_crossings_btn_;
        QAction* action_mode_fast_;
        QAction* action_mode_slow_;
        QActionGroup* minimize_mode_group_;

        // "?" button to the right of minimize_crossings_btn_: no click action,
        // just a hover tooltip explaining the two modes (stays open for as
        // long as the mouse is over it -- see HelpButton).
        HelpButton* minimize_help_btn_;

        // Which mode onMinimizeCrossings() currently runs. Kept in sync with
        // minimize_mode_group_'s checked action by onMinimizeModeChanged().
        enum class MinimizeMode { Fast, Slow };
        MinimizeMode minimize_mode_ = MinimizeMode::Fast;

        // ── Per-tab zoom state ───────────────────────────────────────────────────────
        // zoom_levels_[i] stores the last zoom factor for regular diagram i.
        // A value of 0.0 means "never viewed — fit on first show".
        std::vector<double> zoom_levels_;
        double              joint_zoom_ = 0.0;

        // ── Menu actions ──────────────────────────────────────────────────────────
        QAction* action_deshacer_;
        QAction* action_rehacer_;
        QAction* action_acercar_;
        QAction* action_alejar_;
        QAction* action_nuevo_diagrama_;
        QAction* action_guardar_;
        QAction* action_guardar_como_;
    };

} // namespace ui