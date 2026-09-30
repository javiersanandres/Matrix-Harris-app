#include "Project.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <algorithm>

using json = nlohmann::json;

namespace app_logic {

	using namespace hypergraph_logic;

	// ============================================================================
	// Construction
	// ============================================================================

	Project::Project(const std::string& name)
		: name_(name)
		, active_index_(0)
		, unsaved_changes_(false)
	{
		// Create the joint editor first (it owns the singleton).
		joint_editor_ = std::make_unique<JointHypergraphEditor>(
			JointGraphicalHypergraph::create(name + "_joint"));

		// Create one initial empty diagram.
		addDiagram();

		// addDiagram() sets unsaved_changes_ = true via markUnsaved(), but a
		// brand-new project with one empty diagram is considered clean.
		unsaved_changes_ = false;
	}

	// ============================================================================
	// Diagram management
	// ============================================================================

	int Project::addDiagram() {
		std::string diagram_name("Esquema nuevo");
		// unique_ptr: push_back never invalidates existing pointed-to objects,
		// only the pointers stored inside the vector itself — which we never
		// hold across a push_back.
		editors_.push_back(
			std::make_unique<HypergraphEditor>(GraphicalHypergraph(diagram_name)));
		active_index_ = static_cast<int>(editors_.size()) - 1;
		markUnsaved();
		return active_index_;
	}

	void Project::removeDiagram(int index) {
		if (index < 0 || index >= static_cast<int>(editors_.size()))
			throw std::out_of_range("Project::removeDiagram: index out of bounds.");

		editors_.erase(editors_.begin() + index);

		// Adjust active index.
		if (editors_.empty()) {
			active_index_ = -1;  // only the joint remains
		}
		else if (active_index_ >= static_cast<int>(editors_.size())) {
			active_index_ = static_cast<int>(editors_.size()) - 1;
		}
		else if (active_index_ > index) {
			--active_index_;
		}

		markUnsaved();
	}

	int Project::getDiagramCount() const {
		return static_cast<int>(editors_.size());
	}

	const std::string& Project::getDiagramName(int index) const {
		if (index < 0 || index >= static_cast<int>(editors_.size()))
			throw std::out_of_range("Project::getDiagramName: index out of bounds.");
		return editors_[index]->getName();
	}

	// ============================================================================
	// Active diagram management
	// ============================================================================

	void Project::setActive(int index) {
		if (index != -1 &&
			(index < 0 || index >= static_cast<int>(editors_.size())))
			throw std::out_of_range("Project::setActive: index out of bounds.");
		active_index_ = index;
	}

	int Project::getActiveIndex() const {
		return active_index_;
	}

	bool Project::isJointActive() const {
		return active_index_ == -1;
	}

	// ============================================================================
	// Editor access
	// ============================================================================

	HypergraphEditor& Project::getActiveEditor() {
		if (active_index_ == -1)
			throw std::logic_error(
				"Project::getActiveEditor: joint is active, call getJointEditor().");
		return *editors_[active_index_];
	}

	const HypergraphEditor& Project::getActiveEditor() const {
		if (active_index_ == -1)
			throw std::logic_error(
				"Project::getActiveEditor: joint is active, call getJointEditor().");
		return *editors_[active_index_];
	}

	HypergraphEditor& Project::getEditor(int index) {
		if (index < 0 || index >= static_cast<int>(editors_.size()))
			throw std::out_of_range("Project::getEditor: index out of bounds.");
		return *editors_[index];
	}

	const HypergraphEditor& Project::getEditor(int index) const {
		if (index < 0 || index >= static_cast<int>(editors_.size()))
			throw std::out_of_range("Project::getEditor: index out of bounds.");
		return *editors_[index];
	}

	JointHypergraphEditor& Project::getJointEditor() {
		return *joint_editor_;
	}

	const JointHypergraphEditor& Project::getJointEditor() const {
		return *joint_editor_;
	}

	// ============================================================================
	// Project metadata
	// ============================================================================

	const std::string& Project::getName() const {
		return name_;
	}

	void Project::setName(const std::string& name) {
		name_ = name;
	}

	const std::filesystem::path& Project::getFilePath() const {
		return file_path_;
	}

	bool Project::hasUnsavedChanges() const {
		if (unsaved_changes_) return true;
		for (const auto& editor : editors_)
			if (!editor->isAtSavedState()) return true;
		return !joint_editor_->isAtSavedState();
	}

	void Project::addRecentColour(const Color& colour) {
		if (!recent_colours_.empty() && recent_colours_.front() == colour) return;

		recent_colours_.erase(
			std::remove(recent_colours_.begin(), recent_colours_.end(), colour),
			recent_colours_.end());
		recent_colours_.insert(recent_colours_.begin(), colour);
		if (static_cast<int>(recent_colours_.size()) > MAX_RECENT_COLOURS)
			recent_colours_.resize(MAX_RECENT_COLOURS);
	}

	// ============================================================================
	// save
	// ============================================================================

	void Project::save(const std::filesystem::path& path) {
		json j;
		j["name"] = name_;
		j["active_index"] = active_index_;

		// Serialize each diagram directly into the project JSON — no temp files.
		json diagrams = json::array();
		for (const auto& editor : editors_) {
			json dj;
			editor->toJSON(dj);
			diagrams.push_back(std::move(dj));
		}
		j["diagrams"] = std::move(diagrams);

		// Serialize the joint directly, including its incorporated_ids_.
		json jj;
		joint_editor_->toJSON(jj);
		j["joint"] = std::move(jj);

		json colours = json::array();
		for (const auto& c : recent_colours_)
			colours.push_back(colorToHex(c));
		j["recent_colours"] = std::move(colours);

		std::ofstream out(path);
		if (!out.is_open())
			throw std::runtime_error(
				"No se puede abrir el archivo: " + path.string());
		out << j.dump(2);

		file_path_ = path;
		unsaved_changes_ = false;
		for (const auto& editor : editors_) editor->markSaved();
		joint_editor_->markSaved();
	}

	void Project::save() {
		if (file_path_.empty())
			throw std::logic_error(
				"Project::save: no file path set. Use save(path) first.");
		save(file_path_);
	}

	// ============================================================================
	// load
	// ============================================================================

	std::unique_ptr<Project> Project::load(const std::filesystem::path& path) {
		std::ifstream in(path);
		if (!in.is_open())
			throw std::runtime_error("No se puede abrir el archivo: " + path.string());

		json j;
		try { in >> j; }
		catch (const json::parse_error& e) {
			throw std::runtime_error(std::string("El archivo del proyecto está dañado: ") + e.what());
		}

		auto p = std::make_unique<Project>(j.at("name").get<std::string>());
		p->editors_.clear();

		// The file carries the full layout of every diagram: open them as they
		// were saved, without laying them out again.
		for (const auto& dj : j.at("diagrams")) {
			auto gh = GraphicalHypergraph::fromJSON(dj);
			p->editors_.emplace_back(std::make_unique<HypergraphEditor>(
				std::move(gh), InitialLayout::Keep));
		}

		p->joint_editor_.reset();
		p->joint_editor_ = std::make_unique<JointHypergraphEditor>(
			JointGraphicalHypergraph::fromJSON(j.at("joint")), InitialLayout::Keep);

		// Projects saved before recent colours existed simply start with none.
		if (j.contains("recent_colours")) {
			for (const auto& c : j.at("recent_colours")) {
				try {
					p->recent_colours_.push_back(colorFromHex(c.get<std::string>()));
				}
				catch (const std::invalid_argument& e) {
					throw std::runtime_error(std::string("El archivo del proyecto está dañado: ") + e.what());
				}
			}
		}

		p->active_index_ = j.at("active_index").get<int>();
		p->file_path_ = path;
		p->unsaved_changes_ = false;

		return p;
	}

} // namespace app_logic