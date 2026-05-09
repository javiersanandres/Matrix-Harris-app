#include "MainWindow.h"
#include "AddHypergraphDialog.h"
#include "FuseNodesDialog.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QKeySequence>
#include <QSplitter>
#include <QStackedWidget>

using namespace app_logic;
using namespace hypergraph_logic;

namespace ui {

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
        setWindowTitle("Matrix-Harris");

        setupMenuBar();
        setupCentralArea();
        setupTabBar();
        buildFromProject();
    }

    // ============================================================================
    // Setup
    // ============================================================================

    void MainWindow::setupMenuBar() {
        // ── Archivo ──────────────────────────────────────────────────────────────
        QMenu* archivo = menuBar()->addMenu("Archivo");

        action_nuevo_diagrama_ = archivo->addAction("Nuevo diagrama");
        connect(action_nuevo_diagrama_, &QAction::triggered,
            this, &MainWindow::onNuevoDiagrama);

        archivo->addSeparator();
        QAction* abrir = archivo->addAction("Abrir proyecto...");
        connect(abrir, &QAction::triggered, this, &MainWindow::onAbrirProyecto);

        action_guardar_ = archivo->addAction("Guardar proyecto");
        action_guardar_->setShortcut(QKeySequence::Save);
        connect(action_guardar_, &QAction::triggered,
            this, &MainWindow::onGuardarProyecto);

        action_guardar_como_ = archivo->addAction("Guardar como...");
        action_guardar_como_->setShortcut(QKeySequence::SaveAs);
        connect(action_guardar_como_, &QAction::triggered,
            this, &MainWindow::onGuardarComo);

        archivo->addSeparator();
        QAction* salir = archivo->addAction("Salir");
        connect(salir, &QAction::triggered, this, &MainWindow::onSalir);

        // ── Editar ────────────────────────────────────────────────────────────────
        QMenu* editar = menuBar()->addMenu("Editar");

        action_deshacer_ = editar->addAction("Deshacer");
        action_deshacer_->setShortcut(QKeySequence::Undo);
        action_deshacer_->setEnabled(false);
        connect(action_deshacer_, &QAction::triggered, this, &MainWindow::onDeshacer);

        action_rehacer_ = editar->addAction("Rehacer");
        action_rehacer_->setShortcut(QKeySequence::Redo);
        action_rehacer_->setEnabled(false);
        connect(action_rehacer_, &QAction::triggered, this, &MainWindow::onRehacer);

        editar->addSeparator();

        action_acercar_ = editar->addAction("Acercar");
        action_acercar_->setShortcut(QKeySequence::ZoomIn);
        connect(action_acercar_, &QAction::triggered, this, &MainWindow::onAcercar);

        action_alejar_ = editar->addAction("Alejar");
        action_alejar_->setShortcut(QKeySequence::ZoomOut);
        connect(action_alejar_, &QAction::triggered, this, &MainWindow::onAlejar);
    }

    void MainWindow::setupCentralArea() {
        auto* central = new QWidget(this);
        auto* vbox = new QVBoxLayout(central);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);

        // Tab bar at the top.
        tab_bar_ = new DiagramTabBar(central);
        vbox->addWidget(tab_bar_);

        // Wrapper for the editing view + the "Minimizar cruces" button.
        auto* view_wrapper = new QWidget(central);
        auto* view_layout = new QHBoxLayout(view_wrapper);
        view_layout->setContentsMargins(0, 0, 0, 0);

        central_view_ = new DiagramView(nullptr, view_wrapper);
        view_layout->addWidget(central_view_, 1);

        // "Minimizar cruces" button anchored to the top-right.
        minimize_crossings_btn_ = new QPushButton("Minimizar cruces", view_wrapper);
        minimize_crossings_btn_->setFixedWidth(140);
        connect(minimize_crossings_btn_, &QPushButton::clicked,
            this, &MainWindow::onMinimizeCrossings);

        auto* btn_layout = new QVBoxLayout;
        btn_layout->addWidget(minimize_crossings_btn_);
        btn_layout->addStretch();
        view_layout->addLayout(btn_layout);

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
    }

    void MainWindow::setupTabBar() {
        // Tab bar is created in setupCentralArea; joint scene attached in buildFromProject.
    }

    void MainWindow::buildFromProject() {
        scenes_.clear();
        zoom_levels_.clear();
        joint_zoom_ = 0.0;
        joint_scene_ = nullptr;

        // Create one scene per regular editor.
        for (int i = 0; i < project_->getDiagramCount(); ++i) {
            project_->getEditor(i).setOnMutated([this] { project_->markUnsaved(); });
            DiagramScene* scene = createSceneForEditor(i);
            scenes_.push_back(scene);
            zoom_levels_.push_back(0.0);
            tab_bar_->addTab(scene,
                QString::fromStdString(project_->getDiagramName(i)));
        }
        project_->getJointEditor().setOnMutated([this] { project_->markUnsaved(); });


        // Joint scene.
        joint_scene_ = new DiagramScene(&project_->getJointEditor(), this);
        connect(joint_scene_, &DiagramScene::graphChanged,
            this, &MainWindow::onGraphChanged);
        connect(joint_scene_, &DiagramScene::addHypergraphRequested,
            this, [this](double click_x) {
                // Build entry list.
                const auto& ids = project_->getJointEditor().getGraph()
                    .getIncorporatedIds();
                std::vector<AddHypergraphDialog::DiagramEntry> entries;
                for (int i = 0; i < project_->getDiagramCount(); ++i) {
                    const auto& g = project_->getEditor(i).getGraph();
                    entries.push_back({
                        project_->getDiagramName(i),
                        g.getId(),
                        ids.count(g.getId()) > 0
                        });
                }

                AddHypergraphDialog dlg(entries, this);
                if (dlg.exec() != QDialog::Accepted) return;
                std::string selected_id = dlg.selectedId();
                if (selected_id.empty()) return;

                // Find the corresponding editor and determine left/right.
                for (int i = 0; i < project_->getDiagramCount(); ++i) {
                    if (project_->getEditor(i).getId() == selected_id) {
                        // Determine left: click_x < leftmost node x in layer 0.
                        bool left = false;
                        const auto& jg = project_->getJointEditor().getGraph();
                        if (!jg.getLayers().empty()) {
                            auto nodes0 = jg.getNodesAt(0);
                            if (!nodes0.empty()) {
                                double lx = jg.getNodeLayout()
                                    .at(nodes0.front().get()).x;
                                left = (click_x < lx);
                            }
                        }
                        GraphicalHypergraph& g = const_cast<GraphicalHypergraph&>(
                            project_->getEditor(i).getGraph());
                        try {
                            project_->getJointEditor().addHypergraph(g, left);
                            joint_scene_->rebuild();
                            onGraphChanged();
                        }
                        catch (const std::exception& e) {
                            QMessageBox::warning(this, "Error",
                                QString::fromStdString(e.what()));
                        }
                        return;
                    }
                }
            });

        tab_bar_->setJointScene(joint_scene_);

        // Activate the project's last active diagram.
        switchToTab(project_->getActiveIndex());
    }

    DiagramScene* MainWindow::createSceneForEditor(int index) {
        auto* scene = new DiagramScene(&project_->getEditor(index), this);
        connect(scene, &DiagramScene::graphChanged,
            this, &MainWindow::onGraphChanged);
        return scene;
    }

    // ============================================================================
    // Tab switching
    // ============================================================================

    void MainWindow::switchToTab(int index) {
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
            // Re-apply the saved zoom factor.
            double factor = saved_zoom; // resetZoom set us to 1.0
            central_view_->applyZoomFactor(factor);
        }
        else {
            // First time viewing this tab — fit with margin.
            if (!scene_to_show->sceneRect().isEmpty())
                central_view_->fitWithMargin(scene_to_show->sceneRect());
        }

        updateUndoRedoActions();
    }

    // ============================================================================
    // Undo / Redo
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

    // ============================================================================
    // Slot implementations
    // ============================================================================

    void MainWindow::onNuevoDiagrama() {
        int new_index = project_->addDiagram();
        project_->getEditor(new_index).setOnMutated([this] { project_->markUnsaved(); });
        DiagramScene* scene = createSceneForEditor(new_index);
        scenes_.push_back(scene);
        zoom_levels_.push_back(0.0); // 0 means "fit on first view"
        tab_bar_->addTab(scene,
            QString::fromStdString(project_->getDiagramName(new_index)));
        switchToTab(new_index);
    }

    void MainWindow::onAbrirProyecto() {
        if (!mayContinue()) return;
        QString path = QFileDialog::getOpenFileName(
            this, "Abrir proyecto", QString(), "Proyectos (*.json)");
        if (path.isEmpty()) return;

        try {
            if (project_->hasUnsavedChanges()) {
                auto btn = QMessageBox::question(this,
                    "Cambios no guardados",
                    "El proyecto actual tiene cambios no guardados. ¿Desea descartarlos y abrir otro proyecto?",
                    QMessageBox::Yes | QMessageBox::Cancel);
                if (btn != QMessageBox::Yes) return;
			}
            project_.reset();
            project_ = Project::load(path.toStdString());
            // Rebuild all scenes from the new project.
            for (auto* s : scenes_) delete s;
            scenes_.clear();
            if (joint_scene_) { delete joint_scene_; joint_scene_ = nullptr; }
            // Rebuild tab bar.
            while (tab_bar_->tabCount() > 0) tab_bar_->removeTab(0);
            setWindowTitle(QString::fromStdString(project_->getName()) + " — Matrix-Harris");
            buildFromProject();
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Error al abrir",
                QString::fromStdString(e.what()));

            for (auto* s : scenes_) delete s;
            scenes_.clear();
            if (joint_scene_) { delete joint_scene_; joint_scene_ = nullptr; }
            while (tab_bar_->tabCount() > 0) tab_bar_->removeTab(0);

            project_ = std::make_unique<Project>("Nuevo proyecto");
            buildFromProject();
        }
    }

    void MainWindow::onGuardarProyecto() {
        if (project_->getFilePath().empty()) {
            saveWithPath();
            setWindowTitle(QString::fromStdString(project_->getName()) + " — Matrix-Harris");
        }
        else {
            saveToKnownPath();
        }
    }

    void MainWindow::onGuardarComo() {
        saveWithPath();
    }

    bool MainWindow::saveWithPath() {
        QString suggested = QString::fromStdString(project_->getName()) + ".json";
        QString path = QFileDialog::getSaveFileName(
            this, "Guardar proyecto", suggested, "Proyectos (*.json)");
        if (path.isEmpty()) return false;
        try {
            QFileInfo info(path);
            project_->setName(info.completeBaseName().toStdString());
            project_->save(path.toStdString());
            setWindowTitle(QString::fromStdString(project_->getName()) + " — Matrix-Harris");
            return true;
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Error al guardar",
                QString::fromStdString(e.what()));
            return false;
        }
    }

    bool MainWindow::saveToKnownPath() {
        try {
            project_->save();
            setWindowTitle(QString::fromStdString(project_->getName()) + " — Matrix-Harris");
            return true;
        }
        catch (const std::exception& e) {
            QMessageBox::critical(this, "Error al guardar",
                QString::fromStdString(e.what()));
            return false;
        }
    }

    void MainWindow::onSalir() {
        close();
    }

    void MainWindow::onDeshacer() {
        try {
            int idx = project_->getActiveIndex();
            if (idx == -1) project_->getJointEditor().undo();
            else           project_->getEditor(idx).undo();

            if (idx == -1) joint_scene_->rebuild();
            else           scenes_[idx]->rebuild();

            onGraphChanged();
        }
        catch (const std::exception& e) {
            QMessageBox::warning(this, "Deshacer", QString::fromStdString(e.what()));
        }
    }

    void MainWindow::onRehacer() {
        try {
            int idx = project_->getActiveIndex();
            if (idx == -1) project_->getJointEditor().redo();
            else           project_->getEditor(idx).redo();

            if (idx == -1) joint_scene_->rebuild();
            else           scenes_[idx]->rebuild();

            onGraphChanged();
        }
        catch (const std::exception& e) {
            QMessageBox::warning(this, "Rehacer", QString::fromStdString(e.what()));
        }
    }

    void MainWindow::onAcercar() {
        central_view_->zoomIn();
    }

    void MainWindow::onAlejar() {
        central_view_->zoomOut();
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
        project_->getEditor(index).setName(new_name.toStdString());
    }

    void MainWindow::onGraphChanged() {
        updateUndoRedoActions();
        // Refresh the title bar to show unsaved state.
        QString title = QString::fromStdString(project_->getName());
        if (project_->hasUnsavedChanges()) title += " *";
        setWindowTitle(title + " — Matrix-Harris");
        // Tab bar miniatures update automatically (shared scenes).
    }

    void MainWindow::onMinimizeCrossings() {
        int idx = project_->getActiveIndex();
        try {
            if (idx == -1) project_->getJointEditor().minimizeCrossings();
            else           project_->getEditor(idx).minimizeCrossings();

            if (idx == -1) joint_scene_->rebuild();
            else           scenes_[idx]->rebuild();

            onGraphChanged();
        }
        catch (const std::exception& e) {
            QMessageBox::warning(this, "Error", QString::fromStdString(e.what()));
        }
    }

    // ============================================================================
    // Remove diagram
    // ============================================================================

    void MainWindow::onRemoveDiagram(int index) {
        if (index < 0 || index >= static_cast<int>(scenes_.size())) return;

        auto btn = QMessageBox::question(this,
            "Eliminar diagrama",
            QString("¿Eliminar el diagrama %1? Esta acción no se puede deshacer.")
            .arg(QString::fromStdString(project_->getDiagramName(index))),
            QMessageBox::Yes | QMessageBox::Cancel);
        if (btn != QMessageBox::Yes) return;

        // Remove scene and zoom level.
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
    }

    // ============================================================================
    // Close event
    // ============================================================================

    void MainWindow::closeEvent(QCloseEvent* event) {
        if (mayContinue()) event->accept();
        else event->ignore();
    }

    bool MainWindow::mayContinue() {
        if (!project_->hasUnsavedChanges()) return true;
        auto btn = QMessageBox::question(
            this,
            "Cambios sin guardar",
            "Hay cambios sin guardar. ¿Deseas guardar antes de continuar?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (btn == QMessageBox::Save)    return saveWithPath();
        if (btn == QMessageBox::Discard) return true;
        return false; // Cancel
    }

} // namespace ui