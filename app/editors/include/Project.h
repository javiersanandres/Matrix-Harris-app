#pragma once

#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"
#include "ProjectSettings.h"

#include <deque>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

namespace app_logic {

	using namespace hypergraph_logic;

	// ============================================================================
	// Project
	//
	// Owns all HypergraphEditors and the single JointHypergraphEditor for one
	// project. The application creates exactly one Project at a time.
	//
	// A project consists of:
	//   - A name (shown in the title bar).
	//   - A file path (empty until the project has been saved at least once).
	//   - A list of regular diagram editors (HypergraphEditor), each wrapping a
	//     GraphicalHypergraph.
	//   - One joint diagram editor (JointHypergraphEditor), wrapping the
	//     JointGraphicalHypergraph singleton for this project.
	//   - An active index: which editor is currently displayed in the central
	//     editing area. -1 means the joint editor is active.
	//
	// New diagrams are named "Esquema nuevo", "Esquema nuevo (1)",
	// "Esquema nuevo (2)", etc., with the suffix incremented to avoid
	// duplicates within the current project.
	// ============================================================================
	class Project {
	public:

		// ── Construction ──────────────────────────────────────────────────────────

		// ── Project ───────────────────────────────────────────────────────────────
		//
		// Creates a new empty project with the given name, one empty diagram, and
		// an empty joint diagram, with the given settings (e.g. the application's
		// defaults for new projects). The file path is not set until save() is
		// called.
		//
		explicit Project(const std::string& name, const ProjectSettings& settings = {});

		// Non-copyable — a Project owns the JointGraphicalHypergraph singleton and
		// cannot be duplicated.
		Project(const Project&) = delete;
		Project& operator=(const Project&) = delete;

		// Movable — ownership can be transferred (e.g. returned from load()).
		Project(Project&&) = default;
		Project& operator=(Project&&) = default;

		// ── Diagram management ────────────────────────────────────────────────────

		// ── addDiagram ────────────────────────────────────────────────────────────
		//
		// Creates a new empty GraphicalHypergraph with a default unique name,
		// wraps it in a HypergraphEditor, appends it to the editor list, and
		// sets it as the active diagram. Returns the index of the new diagram.
		//
		int addDiagram();

		// ── duplicateDiagram ──────────────────────────────────────────────────────
		//
		// Appends an exact copy of the diagram at the given index (boxes,
		// connections, layout), as a new diagram the joint does not take for the
		// original, named "<name> (copia)" ("(copia 2)", ... when taken), and sets
		// it as the active diagram. Its history starts empty. Returns its index.
		// Throws std::out_of_range if index is out of bounds.
		//
		int duplicateDiagram(int index);

		// ── removeDiagram ─────────────────────────────────────────────────────────
		//
		// Removes the diagram at the given index. If the removed diagram was active,
		// the active index is adjusted to the preceding diagram (or to 0 if none
		// precede it, or to -1 if the list becomes empty).
		// The joint diagram is not modified. It is a step of the project's history
		// (see undoProjectChange), as adding and duplicating a diagram are.
		// Throws std::out_of_range if index is out of bounds.
		//
		void removeDiagram(int index);

		// ── syncJointNames ────────────────────────────────────────────────────────
		//
		// Gives every diagram that is in the joint its current name there, so a
		// rename (or undoing one) shows in the joint too. The joint's history
		// holds the names it had at each step, so this is also needed after an
		// undo or redo in the joint. Called by load(); the UI calls it after
		// every rename, undo and redo.
		//
		void syncJointNames();

		// ── getDiagramCount ───────────────────────────────────────────────────────
		//
		// Returns the number of regular diagrams in the project (excluding the joint).
		//
		int getDiagramCount() const;

		// ── getDiagramName ────────────────────────────────────────────────────────
		//
		// Returns the name of the diagram at the given index.
		// Throws std::out_of_range if index is out of bounds.
		//
		const std::string& getDiagramName(int index) const;

		// ── Active diagram management ─────────────────────────────────────────────

		// ── setActive ─────────────────────────────────────────────────────────────
		//
		// Sets the currently active diagram. Pass -1 to activate the joint.
		// Throws std::out_of_range if index is out of bounds (and not -1).
		//
		void setActive(int index);

		// ── getActiveIndex ────────────────────────────────────────────────────────
		//
		// Returns the index of the currently active diagram, or -1 if the joint
		// is active.
		//
		int getActiveIndex() const;

		// ── isJointActive ─────────────────────────────────────────────────────────
		//
		// Returns true if the joint diagram is currently active.
		//
		bool isJointActive() const;

		// ── Editor access ─────────────────────────────────────────────────────────

		// ── getActiveEditor ───────────────────────────────────────────────────────
		//
		// Returns a reference to the currently active HypergraphEditor.
		// Throws std::logic_error if the joint is currently active (call
		// getJointEditor() instead).
		//
		HypergraphEditor& getActiveEditor();
		const HypergraphEditor& getActiveEditor() const;

		// ── getEditor ─────────────────────────────────────────────────────────────
		//
		// Returns a reference to the HypergraphEditor at the given index.
		// Throws std::out_of_range if index is out of bounds.
		//
		HypergraphEditor& getEditor(int index);
		const HypergraphEditor& getEditor(int index) const;

		// ── getJointEditor ────────────────────────────────────────────────────────
		//
		// Returns a reference to the JointHypergraphEditor.
		//
		JointHypergraphEditor& getJointEditor();
		const JointHypergraphEditor& getJointEditor() const;

		// ── Project metadata ──────────────────────────────────────────────────────

		// ── getName ───────────────────────────────────────────────────────────────
		const std::string& getName() const;

		// ── setName ───────────────────────────────────────────────────────────────
		void setName(const std::string& name);

		// ── getFilePath ───────────────────────────────────────────────────────────
		//
		// Returns the path to the file this project was last saved to, or an empty
		// path if it has never been saved.
		//
		const std::filesystem::path& getFilePath() const;

		// ── hasUnsavedChanges ─────────────────────────────────────────────────────
		//
		// Returns true if the project differs from what was last saved (or, if
		// never saved, from how it started). Used by the UI for the "*" in the
		// title and the "save before closing?" prompt.
		//
		// Changes made inside a diagram are judged by its editor's history: if
		// every diagram is back at the state it was saved in (for example after
		// undoing every change made since), there is nothing unsaved. Changes the
		// history cannot take back (adding or removing a diagram, markUnsaved())
		// count until the next save.
		//
		bool hasUnsavedChanges() const;

		// ── Persistence ───────────────────────────────────────────────────────────

		// ── save ──────────────────────────────────────────────────────────────────
		//
		// Serialises the entire project to a single JSON file at the given path.
		// The file contains:
		//   {
		//     "name": <string>,
		//     "active_index": <int>,
		//     "diagram_names": [<string>, ...],
		//     "diagrams": [ <GraphicalHypergraph JSON>, ... ],
		//     "joint": <JointGraphicalHypergraph JSON>,
		//     "recent_colours": [ "#RRGGBBAA", ... ],  (optional when loading)
		//     "defaults": { ... }, "layout": { ... }   (the settings, see
		//                                              ProjectSettings::toJSON;
		//                                              optional when loading, as
		//                                              is each field)
		//   }
		// Updates file_path_ and resets the unsaved-changes flag.
		// Throws std::runtime_error if the file cannot be written.
		//
		void save(const std::filesystem::path& path);

		// ── save (to current path) ────────────────────────────────────────────────
		//
		// Saves to file_path_. Throws std::logic_error if the project has never
		// been saved (no path set).
		//
		void save();

		// ── detachFromFile ────────────────────────────────────────────────────────
		//
		// Forgets the file the project was loaded from, so the next save asks
		// where to write (a project started from a copy, like the tour's
		// example). Nothing else changes.
		//
		void detachFromFile();

		// ── load ──────────────────────────────────────────────────────────────────
		//
		// Static factory that reconstructs a Project from a JSON file previously
		// written by save(). Returns a fully initialised Project with all editors
		// in the state they were in when save() was called.
		// Throws std::runtime_error if the file cannot be read or is malformed.
		//
		//static Project load(const std::filesystem::path& path);
		static std::unique_ptr<Project> load(const std::filesystem::path& path);

		// Records a change outside every history, so it counts as unsaved until
		// the next save.
		void markUnsaved() { unsaved_changes_ = true; }

		// ── Project history ───────────────────────────────────────────────────────
		//
		// The changes to the project itself, outside every diagram's history:
		// adding, duplicating and removing a diagram, and changing the settings.
		// They are undone and redone here, apart from the diagrams' own Ctrl+Z,
		// up to MAX_HISTORY steps back as a diagram's history.
		//
		//   - Undoing an addition (or a duplication) takes the diagram out as it
		//     is now, with its own history; redoing it puts it back as it was.
		//   - Undoing a removal puts the diagram back where it was, with its
		//     history; redoing it removes it again.
		//   - Undoing a change of settings brings back the previous ones. When
		//     existing boxes kept their size as their own (KeepTheirSize), the
		//     sizes it gave them are taken away again: those boxes follow the
		//     default once more (a size typed later that happens to equal it is
		//     taken for one of those).
		//
		// A new step forgets the ones undone. Undoing or redoing makes the
		// diagram concerned the active one (for a settings change, the active
		// one stays). The labels describe the step ("eliminar «Esquema 2»"), for
		// the menu.
		//
		bool canUndoProjectChange() const { return !past_.empty(); }
		bool canRedoProjectChange() const { return !future_.empty(); }
		std::string projectUndoLabel() const;
		std::string projectRedoLabel() const;
		void undoProjectChange();
		void redoProjectChange();

		// ── Recent colours ────────────────────────────────────────────────────────
		//
		// Colours the user has picked for nodes in this project, most recent first,
		// so the colour pickers can offer them again. Saved with the project.
		//
		static constexpr int MAX_RECENT_COLOURS = 10;

		const std::vector<Color>& getRecentColours() const { return recent_colours_; }

		// ── addRecentColour ───────────────────────────────────────────────────────
		//
		// Moves the colour to the front of the list (adding it if new) and drops
		// the oldest entries beyond MAX_RECENT_COLOURS. The list is a convenience
		// kept with the next save; it does not by itself mark the project unsaved
		// (the colour change it comes with does, until it is undone).
		//
		void addRecentColour(const Color& colour);

		// ── Settings ──────────────────────────────────────────────────────────────
		//
		// The project's settings (see ProjectSettings). setSettings brings them
		// inside their ranges and, if they change, applies them to every diagram
		// and the joint at once: everything is drawn again with the new distances
		// and default sizes. The defaults for new boxes and equivalences only
		// apply to those created afterwards. That is not a step of any diagram's
		// history, but of the project's.
		//
		// A new default size of a shape reaches the boxes that use its default
		// (no size of their own) only with ExistingBoxes::TakeNewSize; with
		// KeepTheirSize they keep the one they have, which becomes their own,
		// also in what undo and redo bring back. The same goes for a shape's
		// default ashes and the boxes that are a Hogar without their own.
		//
		// Every diagram keeps matching the settings, also after an undo or redo
		// (see HypergraphEditorBase::setConformer). A change of settings is a
		// step of the project's history (see undoProjectChange).
		//
		enum class ExistingBoxes { KeepTheirSize, TakeNewSize };
		const ProjectSettings& getSettings() const { return *settings_; }
		void setSettings(const ProjectSettings& settings, ExistingBoxes existing = ExistingBoxes::KeepTheirSize);

		// Whether some box of a diagram or the joint uses a default size (or,
		// being a Hogar, default ashes) that `sizes` would change, i.e. whether
		// ExistingBoxes makes any difference.
		bool usesDefaultSizesChangedBy(const BoxSizes& sizes) const;

		// Shortcuts to single defaults for new boxes (setting one is setSettings with it changed).
		const Color& getDefaultColour() const { return settings_->colour; }
		const Color& getDefaultFontColour() const { return settings_->font_colour; }
		int getDefaultFontSize() const { return settings_->font_size; }
		void setDefaultColour(const Color& colour);
		void setDefaultFontColour(const Color& colour);
		void setDefaultFontSize(int size);

		// A new box named `name` with the project's defaults.
		NodeAttributes newNodeAttributes(const std::string& name) const;

		// Makes graph match the settings: its distances and default sizes.
		static void conform(GraphicalHypergraph& graph, const ProjectSettings& settings);

	private:
		// Has editor keep matching the settings, and makes it match them now.
		template <typename Editor>
		void follow(Editor& editor);
		void conformAll();

		// A step of the project's history. The diagram it adds or removes is
		// held by the step while it is out of the project.
		struct ProjectStep {
			enum class Kind { AddDiagram, RemoveDiagram, Settings } kind;
			std::string verb;                          // "añadir", "duplicar", "eliminar"
			int index = -1;                            // where the diagram is (or was)
			std::unique_ptr<HypergraphEditor> held;
			ProjectSettings before, after;
			ExistingBoxes existing = ExistingBoxes::KeepTheirSize;
			int id = 0;                                // tells saved states apart
		};
		void record(ProjectStep step);
		void undo(ProjectStep& step);
		void redo(ProjectStep& step);
		std::string label(const ProjectStep& step, bool undone) const;
		void forgetHistory();
		int currentStep() const { return past_.empty() ? floor_step_ : past_.back().id; }

		void insertEditor(int index, std::unique_ptr<HypergraphEditor> editor);
		std::unique_ptr<HypergraphEditor> takeEditor(int index);
		void applySettings(const ProjectSettings& settings, ExistingBoxes existing);

		std::string name_;
		std::filesystem::path file_path_;
		int active_index_ = 0;
		bool unsaved_changes_ = false; // changes outside every history

		std::deque<ProjectStep> past_, future_;
		int next_step_ = 1;
		int floor_step_ = 0;  // the step below the oldest one kept (0: none)
		int saved_step_ = 0;  // currentStep() when last saved

		std::vector<std::unique_ptr<HypergraphEditor>> editors_;
		std::unique_ptr<JointHypergraphEditor> joint_editor_;
		std::vector<Color> recent_colours_;
		// Shared with the editors' conformers, so it outlives a move of the project.
		std::shared_ptr<ProjectSettings> settings_ = std::make_shared<ProjectSettings>();
	};

} // namespace app_logic