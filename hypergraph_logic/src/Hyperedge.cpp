#include "Hyperedge.h"
#include <algorithm>

namespace hypergraph_logic {

	// ============================================================================
	// Construction
	// ============================================================================

	Hyperedge::Hyperedge(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets)
		: is_segment_(false)
		, layer_(-1) {
		for (const auto& source : sources) {
			addSource(source);
		}
		for (const auto& target : targets) {
			addTarget(target);
		}
	}

	Hyperedge::Hyperedge(const WeakHyperedgePtr& origin, const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets)
		: is_segment_(true)
		, origin_(origin)
		, layer_(-1) {
		for (const auto& source : sources) {
			addSource(source);
		}
		for (const auto& target : targets) {
			addTarget(target);
		}
	}

	// ============================================================================
	// Identity
	// ============================================================================

	bool Hyperedge::isSegment() const noexcept {
		return is_segment_;
	}

	WeakHyperedgePtr Hyperedge::getOrigin() const noexcept {
		return origin_;
	}

	int Hyperedge::getLayer() const noexcept {
		return layer_;
	}

	void Hyperedge::setLayer(int layer) noexcept {
		layer_ = layer;
	}

	// ============================================================================
	// Adjacency
	// ============================================================================

	std::vector<NodePtr> Hyperedge::getSources() const {
		std::vector<NodePtr> result;
		result.reserve(sources_.size());

		for (const auto& weak : sources_) {
			if (auto node = weak.lock()) {
				result.push_back(node);
			}
		}

		return result;
	}

	std::vector<NodePtr> Hyperedge::getTargets() const {
		std::vector<NodePtr> result;
		result.reserve(targets_.size());

		for (const auto& weak : targets_) {
			if (auto node = weak.lock()) {
				result.push_back(node);
			}
		}

		return result;
	}


	bool Hyperedge::containsSource(const NodePtr& node) const {
		if (!node) return false;

		for (const auto& weak : sources_) {
			if (auto source = weak.lock()) {
				if (source == node) {
					return true;
				}
			}
		}
		return false;
	}


	bool Hyperedge::containsTarget(const NodePtr& node) const {
		if (!node) return false;

		for (const auto& weak : targets_) {
			if (auto target = weak.lock()) {
				if (target == node) {
					return true;
				}
			}
		}
		return false;
	}


	// ============================================================================
	// Mutation
	// ============================================================================

	void Hyperedge::addSource(const NodePtr& node) {
		if (!node) return;

		for (const auto& it : sources_) {
			auto source = it.lock();
			if (source && source == node) return;
		}

		sources_.push_back(node);
	}

	void Hyperedge::addTarget(const NodePtr& node) {
		if (!node) return;

		for (const auto& it : targets_) {
			auto target = it.lock();
			if (target && target == node) return;
		}

		targets_.push_back(node);
	}

	bool Hyperedge::removeSource(const NodePtr& node) {
		if (!node) return false;

		auto it = std::remove_if(sources_.begin(), sources_.end(),
			[&node](const WeakNodePtr& weak) {
				auto s = weak.lock();
				return !s || s == node;
			});

		if (it == sources_.end()) return false;

		sources_.erase(it, sources_.end());
		return true;
	}

	bool Hyperedge::removeTarget(const NodePtr& node) {
		if (!node) return false;

		auto it = std::remove_if(targets_.begin(), targets_.end(),
			[&node](const WeakNodePtr& weak) {
				auto t = weak.lock();
				return !t || t == node;
			});

		if (it == targets_.end()) return false;

		targets_.erase(it, targets_.end());
		return true;
	}

	void Hyperedge::replaceSource(const NodePtr& oldNode, const NodePtr& newNode) {
		if (!oldNode || !newNode) return;

		for (auto& weak : sources_) {
			if (auto s = weak.lock(); s && s == oldNode) {
				weak = newNode;
				return;
			}
		}
	}

	void Hyperedge::replaceSource(const NodePtr& oldNode, const std::vector<NodePtr>& newNodes) {
		if (!oldNode || newNodes.empty()) return;

		auto it = std::find_if(sources_.begin(), sources_.end(),
			[&oldNode](const WeakNodePtr& weak) {
				auto s = weak.lock();
				return s && s == oldNode;
			});

		if (it == sources_.end()) return;

		sources_.erase(it);

		for (const auto& newNode : newNodes) {
			if (newNode) {
				sources_.push_back(newNode);
			}
		}
	}

	void Hyperedge::replaceTarget(const NodePtr& oldNode, const NodePtr& newNode) {
		if (!oldNode || !newNode) return;

		for (auto& weak : targets_) {
			if (auto t = weak.lock(); t && t == oldNode) {
				weak = newNode;
				return;
			}
		}
	}

	void Hyperedge::replaceTarget(const NodePtr& oldNode, const std::vector<NodePtr>& newNodes) {
		if (!oldNode || newNodes.empty()) return;

		auto it = std::find_if(targets_.begin(), targets_.end(),
			[&oldNode](const WeakNodePtr& weak) {
				auto t = weak.lock();
				return t && t == oldNode;
			});

		if (it == targets_.end()) return;
		targets_.erase(it);

		for (const auto& newNode : newNodes) {
			if (newNode) {
				targets_.push_back(newNode);
			}
		}
	}
}