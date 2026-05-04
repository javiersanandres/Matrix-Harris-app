#pragma once

#include "HypergraphEditor.h"
#include "JointHypergraphEditor.h"

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
	// New diagrams are named "Diagrama nuevo", "Diagrama nuevo (1)",
	// "Diagrama nuevo (2)", etc., with the suffix incremented to avoid
	// duplicates within the current project.
	// ============================================================================
	class Project {
	public:

		// ── Construction ──────────────────────────────────────────────────────────

		// ── Project ───────────────────────────────────────────────────────────────
		//
		// Creates a new empty project with the given name, one empty diagram, and
		// an empty joint diagram. The file path is not set until save() is called.
		//
		explicit Project(const std::string& name);

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

		// ── removeDiagram ─────────────────────────────────────────────────────────
		//
		// Removes the diagram at the given index. If the removed diagram was active,
		// the active index is adjusted to the preceding diagram (or to 0 if none
		// precede it, or to -1 if the list becomes empty).
		// The joint diagram is not modified. This operation cannot be undone.
		// Throws std::out_of_range if index is out of bounds.
		//
		void removeDiagram(int index);

		// ── renameDiagram ─────────────────────────────────────────────────────────
		//
		// Renames the diagram at the given index.
		// Throws std::out_of_range if index is out of bounds.
		//
		void renameDiagram(int index, const std::string& new_name);

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
		// Returns true if any diagram has been mutated since the last save.
		// Used by the UI to show a "save before closing?" prompt.
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
		//     "joint": <JointGraphicalHypergraph JSON>
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

		// ── load ──────────────────────────────────────────────────────────────────
		//
		// Static factory that reconstructs a Project from a JSON file previously
		// written by save(). Returns a fully initialised Project with all editors
		// in the state they were in when save() was called.
		// Throws std::runtime_error if the file cannot be read or is malformed.
		//
		static Project load(const std::filesystem::path& path);

	private:
		std::string name_;
		std::filesystem::path file_path_;
		int active_index_ = 0;
		bool unsaved_changes_ = false;

		std::vector<HypergraphEditor> editors_;
		std::unique_ptr<JointHypergraphEditor> joint_editor_;

		void markUnsaved() { unsaved_changes_ = true; }
	};

} // namespace app_logic