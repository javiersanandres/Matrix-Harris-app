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
		std::string diagram_name("Diagrama nuevo");
		editors_.emplace_back(GraphicalHypergraph(diagram_name));
		editors_.back().setName(diagram_name);
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

	void Project::renameDiagram(int index, const std::string& new_name) {
		if (index < 0 || index >= static_cast<int>(editors_.size()))
			throw std::out_of_range("Project::renameDiagram: index out of bounds.");
		editors_[index].setName(new_name);
		markUnsaved();
	}

	int Project::getDiagramCount() const {
		return static_cast<int>(editors_.size());
	}

	const std::string& Project::getDiagramName(int index) const {
		if (index < 0 || index >= static_cast<int>(editors_.size()))
			throw std::out_of_range("Project::getDiagramName: index out of bounds.");
		return editors_[index].getName();
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
		return editors_[active_index_];
	}

	const HypergraphEditor& Project::getActiveEditor() const {
		if (active_index_ == -1)
			throw std::logic_error(
				"Project::getActiveEditor: joint is active, call getJointEditor().");
		return editors_[active_index_];
	}

	HypergraphEditor& Project::getEditor(int index) {
		if (index < 0 || index >= static_cast<int>(editors_.size()))
			throw std::out_of_range("Project::getEditor: index out of bounds.");
		return editors_[index];
	}

	const HypergraphEditor& Project::getEditor(int index) const {
		if (index < 0 || index >= static_cast<int>(editors_.size()))
			throw std::out_of_range("Project::getEditor: index out of bounds.");
		return editors_[index];
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
		markUnsaved();
	}

	const std::filesystem::path& Project::getFilePath() const {
		return file_path_;
	}

	bool Project::hasUnsavedChanges() const {
		return unsaved_changes_;
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
			editor.toJSON(dj);
			diagrams.push_back(std::move(dj));
		}
		j["diagrams"] = std::move(diagrams);

		// Serialize the joint directly, including its incorporated_ids_.
		json jj;
		joint_editor_->toJSON(jj);
		j["joint"] = std::move(jj);

		std::ofstream out(path);
		if (!out.is_open())
			throw std::runtime_error(
				"Project::save: cannot open file: " + path.string());
		out << j.dump(2);

		file_path_ = path;
		unsaved_changes_ = false;
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

	Project Project::load(const std::filesystem::path& path) {
		std::ifstream in(path);
		if (!in.is_open())
			throw std::runtime_error(
				"Project::load: cannot open file: " + path.string());

		json j;
		try { in >> j; }
		catch (const json::parse_error& e) {
			throw std::runtime_error(
				std::string("Project::load: JSON parse error: ") + e.what());
		}

		Project p(j.at("name").get<std::string>());
		p.editors_.clear();  // discard the initial diagram the constructor created

		// Restore each diagram directly from its embedded JSON object.
		for (const auto& dj : j.at("diagrams"))
			p.editors_.emplace_back(GraphicalHypergraph::fromJSON(dj));

		// Release the joint created by the constructor so the singleton slot is
		// free before fromJSON claims it.
		p.joint_editor_.reset();
		p.joint_editor_ = std::make_unique<JointHypergraphEditor>(
			JointGraphicalHypergraph::fromJSON(j.at("joint")));

		p.active_index_ = j.at("active_index").get<int>();
		p.file_path_ = path;
		p.unsaved_changes_ = false;

		return p;
	}
} // namespace app_logic