#include "Node.h"
#include "LayoutTypes.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hypergraph_logic {

	// ============================================================================
	// Colour hex helpers
	// ============================================================================
	std::string colorToHex(const Color& c) {
		static const char* hex = "0123456789ABCDEF";
		std::string out = "#";
		for (uint8_t component : { c.r, c.g, c.b, c.a }) {
			out += hex[component >> 4];
			out += hex[component & 0x0F];
		}
		return out;
	}

	Color colorFromHex(const std::string& s) {
		if ((s.size() != 7 && s.size() != 9) || s[0] != '#')
			throw std::invalid_argument("invalid colour: " + s);
		auto nibble = [&](char ch) -> uint8_t {
			if (ch >= '0' && ch <= '9') return static_cast<uint8_t>(ch - '0');
			if (ch >= 'A' && ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
			if (ch >= 'a' && ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
			throw std::invalid_argument("invalid colour: " + s);
		};
		auto component = [&](size_t pos) -> uint8_t {
			return static_cast<uint8_t>((nibble(s[pos]) << 4) | nibble(s[pos + 1]));
		};
		Color c;
		c.r = component(1);
		c.g = component(3);
		c.b = component(5);
		c.a = s.size() == 9 ? component(7) : 255;
		return c;
	}

	// ============================================================================
	// Construction
	// ============================================================================
	Node::Node(NodeAttributes attributes)
		: attributes_(std::move(attributes))
		, layer_(0)
		, desired_layer_(-1) {
	}

	Node::Node(std::string name)
		: Node(NodeAttributes(std::move(name))) {
	}

	Node::Node(const char* name)
		: Node(NodeAttributes(name)) {
	}

	Node::Node()
		: attributes_(std::nullopt)
		, layer_(0)
		, desired_layer_(-1) {
	}

	bool Node::isDummy() const noexcept {
		return !attributes_.has_value();
	}

	// ============================================================================
	// Attributes
	// ============================================================================
	const NodeAttributes& Node::getAttributes() const noexcept {
		static const NodeAttributes dummy_attributes{};
		return attributes_ ? *attributes_ : dummy_attributes;
	}

	void Node::setAttributes(NodeAttributes attributes) {
		if (isDummy()) {
			throw std::logic_error("Cannot set attributes on a dummy node.");
		}
		attributes_ = std::move(attributes);
	}

	const std::string& Node::getName() const noexcept {
		return getAttributes().name;
	}

	void Node::setName(const std::string& name) {
		if (isDummy()) {
			throw std::logic_error("Cannot rename a dummy node.");
		}
		attributes_->name = name;
	}

	NodeShape Node::getShape() const noexcept {
		return getAttributes().shape;
	}

	const Color& Node::getColour() const noexcept {
		return getAttributes().colour;
	}

	const Color& Node::getFontColour() const noexcept {
		return getAttributes().font_colour;
	}

	int Node::getFontSize() const noexcept {
		return getAttributes().font_size;
	}

	FireState Node::getFireState() const noexcept {
		return getAttributes().fire;
	}

	bool Node::isFire() const noexcept {
		return getAttributes().isFire();
	}

	bool Node::hasAshes() const noexcept {
		return getAttributes().hasAshes();
	}

	// ============================================================================
	// Geometry
	// ============================================================================
	double Node::getWidth() const noexcept {
		if (isDummy()) return DUMMY_NODE_WIDTH;
		switch (attributes_->shape) {
		case NodeShape::Circle:  return CIRCLE_NODE_WIDTH;
		case NodeShape::Rhombus: return RHOMBUS_NODE_WIDTH;
		default:                 return NODE_WIDTH;
		}
	}

	double Node::getHeight() const noexcept {
		if (isDummy()) return DUMMY_NODE_HEIGHT;
		switch (attributes_->shape) {
		case NodeShape::Circle:  return CIRCLE_NODE_HEIGHT;
		case NodeShape::Rhombus: return RHOMBUS_NODE_HEIGHT;
		default:                 return NODE_HEIGHT;
		}
	}

	double Node::getBoundaryHalfHeight(double dx) const noexcept {
		if (isDummy()) return 0.0;

		const double half_w = getWidth() / 2.0;
		const double half_h = getHeight() / 2.0;
		const double adx = std::min(std::abs(dx), half_w);

		switch (attributes_->shape) {
		case NodeShape::Circle:
			// Ellipse equation, which reduces to the circle for the square bounding box.
			return half_h * std::sqrt(std::max(0.0, 1.0 - (adx / half_w) * (adx / half_w)));
		case NodeShape::Rhombus:
			// Vertices at (±half_w, 0) and (0, ±half_h): the boundary is linear in |dx|.
			return half_h * (1.0 - adx / half_w);
		default:
			return half_h;
		}
	}

	int Node::getLayer() const noexcept {
		return layer_;
	}

	void Node::setLayer(int layer) noexcept {
		layer_ = layer;
	}

	int Node::getDesiredLayer() const noexcept {
		return desired_layer_;
	}

	void Node::setDesiredLayer(int desired_layer) noexcept {
		desired_layer_ = desired_layer;
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
		replaceChild(oldChild, std::vector<NodePtr>{newChild});
	}

	void Node::replaceChild(const NodePtr& oldChild, const std::vector<NodePtr>& newChildren) {
		std::vector<NodePtr> validNewChildren;
		for (const auto& newChild : newChildren) {
			if (!newChild) continue;
			// Check if newChild is already in children_ (avoid duplicates)
			auto it = std::find_if(children_.begin(), children_.end(),
				[&newChild](const WeakNodePtr& weak) {
					auto ptr = weak.lock();
					return ptr && ptr == newChild;
				});
			if (it != children_.end()) continue;

			validNewChildren.push_back(newChild);
		}

		replaceComponent(oldChild, validNewChildren, children_);
	}

	void Node::replaceParent(const NodePtr& oldParent, const NodePtr& newParent) {
		if (!newParent) return;
		// Check if newParent is already in parents_ (avoid duplicates)
		auto it = std::find_if(parents_.begin(), parents_.end(),
			[&newParent](const WeakNodePtr& weak) {
				auto ptr = weak.lock();
				return ptr && ptr == newParent;
			});
		if (it != parents_.end()) return; // Already exists, don't replace

		replaceComponent(oldParent, std::vector<NodePtr>{newParent}, parents_);
	}
}