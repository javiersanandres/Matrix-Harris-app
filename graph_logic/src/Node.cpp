#include "Node.h"
#include <algorithm>
#include <stdexcept>

namespace graph_logic {

	// ============================================================================
	// Base Node Implementation
	// ============================================================================

	Node::Node(float depth)
		: depth_(depth) {
	}

	float Node::getDepth() const noexcept {
		return depth_;
	}

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

	std::vector<NodePtr> Node::getSiblings() const {
		std::vector<NodePtr> result;
		for (const auto& parentWeak : parents_) {
			if (auto parent = parentWeak.lock()) {
				for (const auto& sibling : parent->getChildren()) {
					if (sibling.get() != this) {
						result.push_back(sibling);						
					}
				}
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

	// Add child
	void Node::addChild(const NodePtr& child) {
		if (!child) return;

		for (const auto& it : children_) {
			auto desc = it.lock();
			if (desc && desc == child) return;
		}

		children_.push_back(child);		
	}

	void Node::insertChildAt(size_t position, const NodePtr& child) {
		if (!child) return;

		// Check for duplicates
		for (const auto& it : children_) {
			auto c = it.lock();
			if (c && c == child) return;
		}

		if (position >= children_.size()) {
			children_.push_back(child);
		}
		else {
			children_.insert(children_.begin() + position, child);
		}
	}

	// Remove child
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

	// Remove parent
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

	static void replaceComponent(const NodePtr& oldNode, const std::vector<NodePtr>& newNodes, std::vector<WeakNodePtr>& connections) {
		if (!oldNode || newNodes.empty()) return;

		auto it = std::find_if(connections.begin(), connections.end(),
			[&oldNode](const WeakNodePtr& weak) {
				auto ptr = weak.lock();
				return ptr && ptr == oldNode;
			});

		if (it == connections.end()) return;

		connections.erase(it); // Remove the old node (and any expired weak pointers)

		size_t position = std::distance(connections.begin(), it);

		for (const auto& newNode : newNodes) {
			if (newNode) {
				connections.insert(connections.begin() + position, newNode);
				position++;
			}
		}
	}

	// Note: For rewiring purposes, it is actually pretty important that none of 
	// these methods remove the bidirectional connection from the other side, since
	// the diagram will take care of that when necessary, and it is easier to manage 
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

	// Recompute depth from parents or right/left equivalences and propagate
	void Node::recomputeDepth() {
		float newDepth = 0.0f;

		if (this->isPrimary()) { // it can only have one parent, so we can directly compute the depth from it
			if (auto parent = parents_.empty() ? nullptr : parents_[0].lock()) {
				// If parent is Primary: depth + 1.0, if Hub: depth + 0.5
				newDepth = parent->getDepth() + (parent->isPrimary() ? 1.0f : 0.5f);
			}

			// Consider left and right equivalences for primary nodes
			if (auto l = getLeft()) {
				newDepth = std::max(newDepth, l->depth_);
			}

			if (auto r = getRight()) {
				newDepth = std::max(newDepth, r->depth_);
			}
		}
		else {
			for (const auto& it : parents_) { // compute depth from all parents
				if (auto parent = it.lock()) {
					newDepth = std::max(newDepth, parent->getDepth() + 0.5f);
				}
			}
		}

		if (newDepth == depth_) return;
		depth_ = newDepth;

		if (this->isPrimary()) {
			if (auto l = getLeft()) {
				l->recomputeDepth();
			}

			if (auto r = getRight()) {
				r->recomputeDepth();
			}
		}

		// Propagate to children
		for (const auto& it : children_) {
			if (auto child = it.lock()) {
				child->recomputeDepth();
			}
		}
	}



	// ============================================================================
	// PrimaryNode Implementation
	// ============================================================================

	PrimaryNode::PrimaryNode(std::string name)
		: Node(0), name_(std::move(name)) {
	}

	const std::string& PrimaryNode::getName() const noexcept {
		return name_;
	}

	void PrimaryNode::setName(const std::string& name) noexcept {
		name_ = name;
	}

	void PrimaryNode::addParent(const NodePtr& parent) {
		if (!parent) return;

		// Primary nodes can only have one parent.
		if (!parents_.empty()) {
			if (auto existingParent = parents_[0].lock()) {
				if (existingParent == parent) return;
				else throw std::logic_error("Primary nodes cannot have more than one parent."); // TODO: this will disappear when tested
			}
		}

		parents_.push_back(parent);
	}


	NodePtr PrimaryNode::getLeft() const noexcept {
		return left_.lock();
	}

	NodePtr PrimaryNode::getRight() const noexcept {
		return right_.lock();
	}

	// Add left equivalence (on both nodes)
	void PrimaryNode::addLeft(const NodePtr& left) {
		if (!left || left_.lock() || left->getRight()) return;

		this->left_ = left;
		auto leftPrimary = std::dynamic_pointer_cast<PrimaryNode>(left);
		if (leftPrimary) {
			leftPrimary->right_ = this->shared_from_this();
		}
	}

	// Add right equivalence (on both nodes)
	void PrimaryNode::addRight(const NodePtr& right) {
		if (!right || right_.lock() || right->getLeft()) return;

		this->right_ = right;
		auto rightPrimary = std::dynamic_pointer_cast<PrimaryNode>(right);
		if (rightPrimary) {
			rightPrimary->left_ = this->shared_from_this();
		}
	}


	// Remove left equivalence (on both nodes)
	bool PrimaryNode::removeLeft() {
		if (auto l = left_.lock()) {
			left_.reset();
			auto leftPrimary = std::dynamic_pointer_cast<PrimaryNode>(l);
			if (leftPrimary) {
				leftPrimary->right_.reset();
			}
			return true;
		}

		return false;
	}

	// Remove right equivalence (on both nodes)
	bool PrimaryNode::removeRight() {
		if (auto r = right_.lock()) {
			return r->removeLeft();
		}

		return false;
	}

	// ============================================================================
	// HubNode Implementation
	// ============================================================================
	HubNode::HubNode()
		: Node(0) {
	}

	void HubNode::addParent(const NodePtr& parent) {
		if (!parent) return;

		// Check for duplicates
		for (const auto& it : parents_) {
			auto anc = it.lock();
			if (anc && anc == parent) return;
		}

		parents_.push_back(parent);
	}


	void HubNode::insertParentAt(size_t position, const NodePtr& parent) {
		if (!parent) return;

		// Check for duplicates
		for (const auto& it : parents_) {
			auto p = it.lock();
			if (p && p == parent) return;
		}

		if (position >= parents_.size()) {
			parents_.push_back(parent);
		}
		else {
			parents_.insert(parents_.begin() + position, parent);
		}
	}


}
