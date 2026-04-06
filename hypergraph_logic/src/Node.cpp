#include "Node.h"
#include <algorithm>

namespace hypergraph_logic {

	// ============================================================================
	// Construction
	// ============================================================================
	Node::Node(std::string name)
		: is_dummy_(false)
		, name_(std::move(name))
		, layer_(0) {
	}

	Node::Node()
		: is_dummy_(true)
		, name_("")
		, layer_(0) {
	}

	bool Node::isDummy() const noexcept {
		return is_dummy_;
	}

	const std::string& Node::getName() const noexcept {
		return name_;
	}

	int Node::getLayer() const noexcept {
		return layer_;
	}

	void Node::setLayer(int layer) noexcept {
		layer_ = layer;
	}

	// ============================================================================
	// Adjacency queries
	// ============================================================================

	std::vector<NodePtr> Node::getChildren() const {
		std::vector<NodePtr> result;
		result.reserve(children_.size());

		for (const auto& it : children_) {
			if (auto desc = it.lock()) {
				result.push_back(desc);
			}
		}

		return result;
	}

	std::vector<NodePtr> Node::getParents() const {
		std::vector<NodePtr> result;
		result.reserve(parents_.size());

		for (const auto& it : parents_) {
			if (auto asc = it.lock()) {
				result.push_back(asc);
			}
		}

		return result;
	}

	std::unordered_set<Node*> Node::getAllAncestors() const {
		std::unordered_set<Node*> result = {};
		for (const auto& parentWeak : parents_) {
			if (auto parent = parentWeak.lock()) {
				if (result.count(parent.get()) == 0) { // first insertion
					result.insert(parent.get());
					auto parentAncestors = parent->getAllAncestors();
					result.insert(parentAncestors.begin(), parentAncestors.end());
				}
			}
		}
		return result;
	}

	std::unordered_set<Node*> Node::getAllDescendants() const {
		std::unordered_set<Node*> result = {};

		for (const auto& childWeak : children_) {
			if (auto child = childWeak.lock()) {
				if (result.count(child.get()) == 0) { // first insertion
					result.insert(child.get());
					auto childDescendants = child->getAllDescendants();
					result.insert(childDescendants.begin(), childDescendants.end());
				}
			}
		}

		return result;
	}

	// ============================================================================
	// Mutation
	// ============================================================================

	void Node::addParent(const NodePtr& parent) {
		if (!parent) return;

		for (const auto& it : parents_) {
			auto asc = it.lock();
			if (asc && asc == parent) return;
		}

		parents_.push_back(parent);
	}

	void Node::addChild(const NodePtr& child) {
		if (!child) return;

		for (const auto& it : children_) {
			auto desc = it.lock();
			if (desc && desc == child) return;
		}

		children_.push_back(child);
	}

	bool Node::removeParent(const NodePtr& parent) {
		if (!parent) return false;

		auto it = std::remove_if(parents_.begin(), parents_.end(),
			[&parent](const WeakNodePtr& weak) {
				auto ptr = weak.lock();
				return !ptr || ptr == parent;
			});

		if (it == parents_.end()) return false;

		parents_.erase(it, parents_.end()); // Actually remove the parent and any expired weak pointers
		return true;
	}

	bool Node::removeChild(const NodePtr& child) {
		if (!child) return false;

		auto it = std::remove_if(children_.begin(), children_.end(),
			[&child](const WeakNodePtr& weak) {
				auto ptr = weak.lock();
				return !ptr || ptr == child;
			});

		if (it == children_.end()) return false;

		children_.erase(it, children_.end()); // Actually remove the child and any expired weak pointers
		return true;
	}

	// ============================================================================
	// Replace helper
	// ============================================================================
	static void replaceComponent(const NodePtr& oldNode, const std::vector<NodePtr>& newNodes, std::vector<WeakNodePtr>& connections) {
		if (!oldNode || newNodes.empty()) return;

		auto it = std::find_if(connections.begin(), connections.end(),
			[&oldNode](const WeakNodePtr& weak) {
				auto ptr = weak.lock();
				return ptr && ptr == oldNode;
			});

		if (it == connections.end()) return;

		size_t position = std::distance(connections.begin(), it);

		connections.erase(it); // Remove the old node (and any expired weak pointers)

		for (const auto& newNode : newNodes) {
			if (newNode) {
				connections.insert(connections.begin() + position, newNode);
				position++;
			}
		}
	}

	// Note: For rewiring purposes, it is actually pretty important that none of 
	// these methods remove the bidirectional connection from the other side, since
	// the graph will take care of that when necessary, and it is easier to manage 
	// the connections if they are not removed in the middle of the rewiring process.
	void Node::replaceChild(const NodePtr& oldChild, const NodePtr& newChild) {
		replaceComponent(oldChild, std::vector<NodePtr>{newChild}, children_);
	}

	void Node::replaceChild(const NodePtr& oldChild, const std::vector<NodePtr>& newChildren) {
		replaceComponent(oldChild, newChildren, children_);
	}

	void Node::replaceParent(const NodePtr& oldParent, const NodePtr& newParent) {
		replaceComponent(oldParent, std::vector<NodePtr>{newParent}, parents_);
	}
}