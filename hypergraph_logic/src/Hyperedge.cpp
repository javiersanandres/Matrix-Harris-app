#include "Hyperedge.h"
#include <algorithm>
#include <stdexcept>

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

	const Hyperedge* Hyperedge::styleOwner() const noexcept {
		if (!is_segment_) return this;
		return origin_.lock().get(); // the graph keeps the origin alive while its segments exist
	}

	bool Hyperedge::isContinuous() const noexcept {
		const Hyperedge* owner = styleOwner();
		return owner ? owner->uncertain_sources_.empty() && owner->uncertain_targets_.empty() : true;
	}

	bool Hyperedge::allEndsUncertain() const noexcept {
		const Hyperedge* owner = styleOwner();
		if (!owner) return false;
		for (const auto& s : owner->sources_)
			if (!owner->uncertain_sources_.count(s.lock().get())) return false;
		for (const auto& t : owner->targets_)
			if (!owner->uncertain_targets_.count(t.lock().get())) return false;
		return true;
	}

	void Hyperedge::setContinuous(bool continuous) {
		if (is_segment_) {
			throw std::logic_error("El estilo de línea solo se puede cambiar en la conexión completa.");
		}
		// A decision for the whole connection overrides the per-end ones.
		if (continuous) {
			uncertain_sources_.clear();
			uncertain_targets_.clear();
		}
		else {
			for (const auto& src : sources_) uncertain_sources_.insert(src.lock().get());
			for (const auto& tgt : targets_) uncertain_targets_.insert(tgt.lock().get());
		}
	}

	bool Hyperedge::isSourceUncertain(const Node* node) const noexcept {
		const Hyperedge* owner = styleOwner();
		return owner && owner->uncertain_sources_.count(node) > 0;
	}

	bool Hyperedge::isTargetUncertain(const Node* node) const noexcept {
		const Hyperedge* owner = styleOwner();
		return owner && owner->uncertain_targets_.count(node) > 0;
	}

	void Hyperedge::setSourceUncertain(const NodePtr& node, bool uncertain) {
		if (is_segment_) {
			throw std::logic_error("Solo se puede marcar como dudosa la conexión completa.");
		}
		if (!containsSource(node)) {
			throw std::invalid_argument("La caja no está por encima en esta conexión.");
		}
		if (sources_.size() == 1) return setContinuous(!uncertain);
		if (uncertain) uncertain_sources_.insert(node.get());
		else           uncertain_sources_.erase(node.get());
	}

	void Hyperedge::setTargetUncertain(const NodePtr& node, bool uncertain) {
		if (is_segment_) {
			throw std::logic_error("Solo se puede marcar como dudosa la conexión completa.");
		}
		if (!containsTarget(node)) {
			throw std::invalid_argument("La caja no está por debajo en esta conexión.");
		}
		if (targets_.size() == 1) return setContinuous(!uncertain);
		if (uncertain) uncertain_targets_.insert(node.get());
		else           uncertain_targets_.erase(node.get());
	}

	void Hyperedge::setUncertainEnds(const std::vector<NodePtr>& sources, const std::vector<NodePtr>& targets) {
		if (is_segment_) {
			throw std::logic_error("Solo se puede marcar como dudosa la conexión completa.");
		}
		std::set<const Node*> new_sources, new_targets;
		for (const auto& s : sources) {
			if (!containsSource(s)) throw std::invalid_argument("La caja no está por encima en esta conexión.");
			new_sources.insert(s.get());
		}
		for (const auto& t : targets) {
			if (!containsTarget(t)) throw std::invalid_argument("La caja no está por debajo en esta conexión.");
			new_targets.insert(t.get());
		}
		uncertain_sources_ = std::move(new_sources);
		uncertain_targets_ = std::move(new_targets);
	}

	bool Hyperedge::hasUncertainEnds() const noexcept {
		const Hyperedge* owner = styleOwner();
		return owner && (!owner->uncertain_sources_.empty() || !owner->uncertain_targets_.empty());
	}

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
		uncertain_sources_.erase(node.get());
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
		uncertain_targets_.erase(node.get());
		return true;
	}

	void Hyperedge::replaceSource(const NodePtr& oldNode, const NodePtr& newNode) {
		replaceSource(oldNode, std::vector<NodePtr>{ newNode });
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
		const bool was_uncertain = uncertain_sources_.erase(oldNode.get()) > 0;

		bool replace = false;
		for (const auto& newNode : newNodes) {
			if (!newNode) continue;
			auto it2 = std::find_if(sources_.begin(), sources_.end(),
				[&newNode](const WeakNodePtr& weak) {
					auto s = weak.lock();
					return s && s == newNode;
				});

			if (it2 != sources_.end()) continue; // Avoid adding duplicates
			replace = true;
			sources_.push_back(newNode);
			if (was_uncertain) uncertain_sources_.insert(newNode.get());
		}
		if (!replace) {
			sources_.push_back(oldNode);
			if (was_uncertain) uncertain_sources_.insert(oldNode.get());
		}
	}

	void Hyperedge::replaceTarget(const NodePtr& oldNode, const NodePtr& newNode) {
		replaceTarget(oldNode, std::vector<NodePtr>{ newNode });
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
		const bool was_uncertain = uncertain_targets_.erase(oldNode.get()) > 0;

		bool replace = false;
		for (const auto& newNode : newNodes) {
			if (!newNode) continue;
			auto it2 = std::find_if(targets_.begin(), targets_.end(),
				[&newNode](const WeakNodePtr& weak) {
					auto t = weak.lock();
					return t && t == newNode;
				});
			if (it2 != targets_.end()) continue; // Avoid adding duplicates
			replace = true;
			targets_.push_back(newNode);
			if (was_uncertain) uncertain_targets_.insert(newNode.get());
		}
		if (!replace) {
			targets_.push_back(oldNode);
			if (was_uncertain) uncertain_targets_.insert(oldNode.get());
		}
	}
}