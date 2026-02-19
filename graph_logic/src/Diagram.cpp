#include "Diagram.h"
#include <stdexcept>
#include <unordered_set>
#include <algorithm>
#include <limits>

namespace graph_logic {
	// static helper functions declaration
	static void rewireConnection(const NodePtr& parent, const NodePtr& oldChild, const NodePtr& newChild);
	static size_t findInsertPosition(const Diagram* diagram, const std::vector<NodePtr>& nodes, const NodePtr& newNode);
	static bool parentIsInAncestors(const NodePtr& node, const NodePtr& parent);
	static std::vector<NodePtr> checkCyclesUtil(Node* node, std::unordered_set<Node*>& visited, std::vector<NodePtr>& path);

	// Constructor
	Diagram::Diagram(std::string name)
		: name_(std::move(name)) {
	}

	// Comparison implementation
	bool Diagram::compare(const NodePtr& a, const NodePtr& b) const {
		if (!a || !b) return a < b;
		if (a == b) return false;

		// Find lowest common ancestor
		NodePtr lca = findLowestCommonAncestor(a, b);

		if (lca) {
			// They share a common ancestor
			// Find which children of LCA lead to a and b
			NodePtr childToA = findChildLeadingTo(lca, a);
			NodePtr childToB = findChildLeadingTo(lca, b);

			if (!childToA || !childToB) {
				// One is descendant of the other (shouldn't happen if LCA is correct, but safe)
				return a->getDepth() < b->getDepth();
			}

			// Compare positions in LCA's children vector
			int posA = findPositionInChildren(lca, childToA);
			int posB = findPositionInChildren(lca, childToB);

			return posA < posB;
		}
		else {
			// No common ancestor, compare roots
			int posA = findPositionInRoots(findRoot(a));
			int posB = findPositionInRoots(findRoot(b));

			return posA < posB;
		}
	}


	// Getters and setters
	const std::string& Diagram::getName() const noexcept {
		return name_;
	}
	void Diagram::setName(const std::string& name) noexcept {
		name_ = name;
	}

	std::vector<NodePtr> Diagram::getNodes() const {
		return nodes_;
	}
	std::vector<NodePtr> Diagram::getRoots() const {
		return roots_;
	}


	static void rewireConnection(const NodePtr& parent, const NodePtr& oldChild, const NodePtr& newChild) {
		parent->replaceChild(oldChild, newChild); // this deletes the connection from parent to oldChild and creates a new one to newChild
		oldChild->replaceParent(parent, newChild); // this deletes the connection from oldChild to parent and creates a new one to newChild
		newChild->addParent(parent);
		newChild->addChild(oldChild);
	}
	
	void Diagram::addNode(std::string& name, const NodePtr& parent, const NodePtr& child) {
		// This method creates a new node and inserts it into the diagram.
		// Cycle formation is not possible since the new node is a leaf, a root or an extension of an existing 
		// non-cycle connection (this will be forced in the client side), so we don't need to check for it here.

		PrimaryNodePtr node = std::make_shared<PrimaryNode>(name);

		nodes_.push_back(node);

		if (!parent) {
			roots_.push_back(node);
		}
		else {
			// We will control in the client side that the parent is unique or a hub, 
			// so we don't need to worry about multiple parents here
			if (child) { // Rewire existing connection
				rewireConnection(parent, child, node);
			}
			else {
				parent->addChild(node);
				node->addParent(parent);
			}

			node->recomputeDepth();
		}
	}

	void Diagram::removeNode(const NodePtr& node) {
		if (!node) return;

		auto parents = node->getParents();
		auto children = node->getChildren();

		if (node->isHub()) {
			removeHubNode(node, parents, children);
		}
		else if (parents.empty()) {
			removeRootNode(node, children);
		}
		else {
			removeNonRootPrimaryNode(node, parents[0], children);
		}

		// Remove from diagram
		nodes_.erase(std::remove(nodes_.begin(), nodes_.end(), node), nodes_.end());
		roots_.erase(std::remove(roots_.begin(), roots_.end(), node), roots_.end());
	}

	void Diagram::removeRootNode(const NodePtr& node, const std::vector<NodePtr>& children) {
		if (children.empty()) return; // Leaf node, nothing to be done

		for (const auto& child: children) {
			if (child->isHub()) {
				child->removeParent(node);

				// In order for the hub to exist, it must be the case that it has 2 or more parents.
				// If it has only 2 parents, since the node to remove is already one, the hub must disappear;
				// otherwise, the hub stays and we just need to remove the connection with the parent
				if (child->getParents().size() <= 1) {
					removeNode(child);
				}
				// It is not necessary to recompute the depth of the hub because the node to remove is a root, 
				// so the depth of the child is not changing (since its a maximum of its parents' depths)
			}
			else {
				child->removeParent(node);
				child->recomputeDepth();

				roots_.push_back(child); // The children are roots since they do not have any other parent
			}
		}
	}

	void Diagram::removeHubNode(const NodePtr& hub, const std::vector<NodePtr>& parents, const std::vector<NodePtr>& children) {
		// Hub removal and creation is an internal feature, completely invisible to any outside class.
		// There are two cases for a hub to be removed:
		//	1) It has only 1 parent
		// 	2) It has 2 or more parents, but has no children

		if (parents.size() == 1) {
			// Case 1: the hub is just an extension of its single parent.
			auto& parent = parents[0];

			parent->replaceChild(hub, children); // Rewire connections from parent to children
			for (const auto& child : children) {
				child->replaceParent(hub, parent);
				child->recomputeDepth();
			}
		}
		else if (children.empty()) {
			// Case 2: the hub is just a connector between its parents.
			for (const auto& parent : parents) {
				parent->removeChild(hub);
			}
		}
		else {
			throw std::logic_error("Cannot remove a hub node with multiple parents and children."); // TODO: this will disappear when tested.
		}
	}

	void Diagram::removeNonRootPrimaryNode(const NodePtr& node, const NodePtr& parent, const std::vector<NodePtr>& children) {
		// We need to rewire the connections from the parent to the children.
		// There is a special case to consider: if the parent is a Hub, the node to remove is a leaf
		// and also the last child of the hub, so we will have to remmove the hub as well.

		if (parent->isHub() && children.empty()) {
			parent->removeChild(node);

			if (parent->getChildren().empty()) {
				return removeNode(parent);
			}
		}
		else {
			parent->replaceChild(node, children); // Rewire connections from parent to children

			for (const auto& child : children) {
				child->replaceParent(node, parent);
				child->recomputeDepth();
			}
		}
	}

	
	void Diagram::fuseHubChain(const NodePtr& node) {
		if (!node || !node->isHub()) return;

		auto hub = std::dynamic_pointer_cast<HubNode>(node);
		auto children = hub->getChildren();

		// Check if this hub has exactly one child and it's also a hub
		while (children.size() == 1 && children[0]->isHub()) {
			auto childHub = std::dynamic_pointer_cast<HubNode>(children[0]);
			auto grandchildren = childHub->getChildren();

			// Move all grandchildren to the current hub (order is preserved since 
			// we are inserting them in the same position as the child hub)
			hub->replaceChild(childHub, grandchildren);
			for (const auto& grandchild : grandchildren) {
				grandchild->replaceParent(childHub, hub);
			}

			// We also need to rewire the parents of the child hub to point to the current hub instead
			childHub->removeParent(hub);
			auto childHubParents = childHub->getParents();
			auto hubParents = hub->getParents();

			for (const auto& parent : childHubParents) {
				hub->insertParentAt(findInsertPosition(this, hubParents, parent), parent);
				parent->replaceChild(childHub, hub);
			}

			nodes_.erase(std::remove(nodes_.begin(), nodes_.end(), childHub), nodes_.end());
			
			children = grandchildren; // Propagate down the chain in case there are more hubs to fuse
		}

		// Recompute depth for the current hub and all its possible new children
		hub->recomputeDepth();
	}

	
	
	void Diagram::addConnection(const NodePtr& child, const NodePtr& parent) {
		if (!child || !parent) return;
		if (child == parent) {
			throw std::invalid_argument("A node cannot be connected to itself.");
		}

		//  Check for redundancy connections (the child already has this parent in its ancestry)
		if (parentIsInAncestors(parent, child)) {
			throw std::logic_error("This connection already exists in the diagram.");
		}

		// Temporarily add the connection (taking the order into consideration) to check for cycles
		if (child->isPrimary()) {
			if (child->getParents().empty()) {
				child->addParent(parent);
				parent->insertChildAt(findInsertPosition(this, parent->getChildren(), child), child);
			}
			else {
				NodePtr existingParent = child->getParents()[0];
				HubNodePtr hub;

				if (existingParent->isHub()) { // There is already a hub, so no need to create another
					hub = std::dynamic_pointer_cast<HubNode>(existingParent);
				}
				else { // No hub already, so we should create it
					hub = std::make_shared<HubNode>();
					nodes_.push_back(hub);

					// Rewire existing connection from parent to child to parent to hub and hub to child
					rewireConnection(existingParent, child, hub);
				}

				// Connect new parent to hub
				hub->insertParentAt(findInsertPosition(this, hub->getParents(), parent), parent);
				parent->insertChildAt(findInsertPosition(this, parent->getChildren(), hub), hub);
			}
		}
		else {
			HubNodePtr hub = std::dynamic_pointer_cast<HubNode>(child);
			hub->insertParentAt(findInsertPosition(this, hub->getParents(), parent), parent);
			parent->insertChildAt(findInsertPosition(this, parent->getChildren(), hub), hub);
		}
		
		// Check for cycles
		if (!checkCycles(child).empty()) {
			// Rollback
			if (child->isPrimary()) {
				if (child->getParents()[0] == parent) { // Just remove the connection
					child->removeParent(parent);
					parent->removeChild(child);
				}
				else {
					HubNodePtr hub = std::dynamic_pointer_cast<HubNode>(child->getParents()[0]);
					hub->removeParent(parent);
					parent->removeChild(hub);

					if (hub->getParents().size() <= 1) {
						removeNode(hub);
					}
				}
			}
			else {
				HubNodePtr hub = std::dynamic_pointer_cast<HubNode>(child);
				hub->removeParent(parent);
				parent->removeChild(hub);
			}
			
			throw std::logic_error("Adding this connection would create a cycle in the diagram.");
		}

		// Now, after checking that there were not any cycles, it is safe to recompute dephts.
		// The safer method to deal with possible hub creation in the middle of this process
		// is to recompute the depth from the new parent, as this call will force any child to
		// also recompute its own.
		parent->recomputeDepth();

		// Fuse hub chains if created. This is not necessary
		if (parent->isHub()) {
			fuseHubChain(parent);
		}

		// If this was a root node, remove it from roots_ (it now has a parent)
		if (child->getParents().size() == 1) {
			roots_.erase(std::remove(roots_.begin(), roots_.end(), child), roots_.end());
		}

		// Remove possible pre-existing connections from parent to some child's descendant
		auto descendants = child->getAllDescendants();
		for (const auto& parent_child : parent->getChildren()) {
			if (descendants.count(parent_child.get()) > 0) {
				parent->removeChild(parent_child);
				parent_child->removeParent(parent);

				if (parent_child->isHub() && parent_child->getParents().size() <= 1) { // Only parent of the hub, the hub disappears
					removeNode(parent_child);
				}
			}
		}
	}

	static size_t findInsertPosition(const Diagram* diagram, const std::vector<NodePtr>& nodes, const NodePtr& newNode) {
		size_t insertPosition = 0;
		for (const auto& existingNode : nodes) {
			if (diagram->compare(newNode, existingNode)) {
				break;
			}
			++insertPosition;
		}
		return insertPosition;
	}

	static bool parentIsInAncestors(const NodePtr& node, const NodePtr& parent) {
		// Helper function to check if parent is in the ancestors of node.
		// It makes sense to have a sepparate method instead of implementing a function in Node which
		// returns all ancestors, since there is no need to store all ancestors, just to check for one.
		// This way, we can do it faster with less memory overhead.

		for (const auto& ancestor : node->getParents()) {
			if (ancestor == parent) {
				return true;
			}
			if (parentIsInAncestors(ancestor, parent)) {
				return true;
			}
		}
		return false;
	}

	void Diagram::removeConnection(const NodePtr& child, const NodePtr& parent) {
		if (!child || !parent) return;
		
		auto childParents = child->getParents();
		if (std::find(childParents.begin(), childParents.end(), parent) == childParents.end()) return;
		
		parent->removeChild(child);
		child->removeParent(parent);

		if (child->isPrimary()) {
			roots_.push_back(child);

			if (parent->isHub() && parent->getChildren().empty()) { // Only child of hub, the hub disappears
				removeNode(parent);
			}
		}
		else {
			if (childParents.size() <= 2) { // The hub had two parents initially and now one, so it should disappear
				removeNode(child);
			}
		}
	}












	std::vector<NodePtr> Diagram::checkCycles(const NodePtr& node) {
		// Wrapper function for cycle detection
		std::unordered_set<Node*> visited;
		std::vector<NodePtr> path;

		return checkCyclesUtil(node.get(), visited, path);
	}

	static std::vector<NodePtr> checkCyclesUtil(Node* node, std::unordered_set<Node*>& visited, std::vector<NodePtr>& path) {
		// The implemented algorithm for cycle detection uses DFS and keeps track of visited nodes and the current path.
		// For a more detailed explanation: https://takeuforward.org/data-structure/detect-cycle-in-a-directed-graph-using-dfs-g-19

		visited.insert(node);
		path.push_back(node->shared_from_this());

		auto adjacentNodes = node->getChildren();
		adjacentNodes.push_back(node->getRight());
		adjacentNodes.push_back(node->getLeft());

		for (const auto& adjNodePtr : adjacentNodes) {
			if (!adjNodePtr) continue;
			Node* adjNode = adjNodePtr.get();

			if (visited.count(adjNode) == 0) { // the adjacent node has not been visited yet
				auto result = checkCyclesUtil(adjNode, visited, path);
				if (!result.empty()) {
					return result;
				}
			}
			else {
				for (const auto& pathNode : path) {
					if (pathNode.get() == adjNode) {
						// Cycle detected, extract the cycle path
						auto it = std::find(path.begin(), path.end(), pathNode);
						return std::vector<NodePtr>(it, path.end());
					}
				}
			}
		}

		path.pop_back();
		return {};
	}

	NodePtr Diagram::findLowestCommonAncestor(const NodePtr& a, const NodePtr& b) const {
		if (!a || !b) return nullptr;

		// Get all common ancestors for both a and b
		auto ancestorsA = a->getAllAncestors();
		auto ancestorsB = b->getAllAncestors();

		std::vector<NodePtr> commonAncestors;
		for (const auto& ancestor : ancestorsB) {
			if (ancestorsA.count(ancestor) > 0) {
				commonAncestors.push_back(ancestor->shared_from_this());
			}
		}
		if (commonAncestors.empty()) return nullptr;


		// Sort common ancestors by depth (highest depth first - lowest in tree)
		std::sort(commonAncestors.begin(), commonAncestors.end(),
			[](const NodePtr& a, const NodePtr& b) {
				return a->getDepth() > b->getDepth();
			});

		// Get the ones with maximum depth
		int maxDepth = commonAncestors[0]->getDepth();
		commonAncestors.erase(
			std::remove_if(commonAncestors.begin(), commonAncestors.end(),
				[maxDepth](const NodePtr& ancestor) {
					return ancestor->getDepth() < maxDepth;
				}),
			commonAncestors.end()
		);

		if (commonAncestors.size() == 1) {
			return commonAncestors[0];
		}

		// If multiple, get the one which is more to the left (compare their positions in their parents)
		NodePtr lca = commonAncestors[0];
		for (const auto& ancestor : commonAncestors) {




			return lca;
		}


	}
};