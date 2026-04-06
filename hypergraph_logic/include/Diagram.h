#pragma once
#include "Node.h"
#include <string>


namespace hypergraph_logic {
	class Diagram {
	public:
		explicit Diagram(std::string name);

		// Basic getters and setters
		const std::string& getName() const noexcept;
		void setName(const std::string& name) noexcept;

		std::vector<NodePtr> getNodes() const;
		std::vector<NodePtr> getRoots() const;

		void addNode(std::string& name, const NodePtr& parent, const NodePtr& child);
		void removeNode(const NodePtr& node);

		void addConnection(const NodePtr& child, const NodePtr& parent);
		void removeConnection(const NodePtr& child, const NodePtr& parent);

		// the equivalence logic will be added when i've figured everything out
		//void addEquivalence(const NodePtr& left, const NodePtr& right);
		//void removeEquivalence(const NodePtr& left, const NodePtr& right);

		void mergeNodes(const NodePtr& target, const NodePtr& source);


		// Compare two nodes using diagram's left-to-right ordering
		bool compare(const NodePtr& a, const NodePtr& b) const;

	private:
		std::string name_;
		std::vector<NodePtr> nodes_;
		std::vector<NodePtr> roots_;

		// Diagram-specific node comparator
		struct NodeComparator {
			const Diagram* diagram;

			explicit NodeComparator(const Diagram* diag) : diagram(diag) {}

			bool operator()(const NodePtr& a, const NodePtr& b) const {
				return diagram->compare(a, b);
			}
		};

		// Helper methods for node removal
		void removeRootNode(const NodePtr& node, const std::vector<NodePtr>& children);
		void removeHubNode(const NodePtr& hub, const std::vector<NodePtr>& parents, const std::vector<NodePtr>& children);
		void removeNonRootPrimaryNode(const NodePtr& node, const NodePtr& parent, const std::vector<NodePtr>& children);

		void fuseHubChain(const NodePtr& node);
		


		// Helper methods for comparison
		NodePtr findLowestCommonAncestor(const NodePtr& a, const NodePtr& b) const;
		NodePtr findChildLeadingTo(const NodePtr& ancestor, const NodePtr& descendant) const;
		NodePtr findRoot(const NodePtr& node) const;
		int findPositionInChildren(const NodePtr& parent, const NodePtr& child) const;
		int findPositionInRoots(const NodePtr& node) const;


		static std::vector<NodePtr> checkCycles(const NodePtr& node);
	};
}


