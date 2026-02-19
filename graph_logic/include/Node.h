#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_set>

namespace graph_logic {

    class Node;
    using NodePtr = std::shared_ptr<Node>;
    using WeakNodePtr = std::weak_ptr<Node>;

    class PrimaryNode;
    using PrimaryNodePtr = std::shared_ptr<PrimaryNode>;

    class HubNode;
    using HubNodePtr = std::shared_ptr<HubNode>;

    // Base Node class
    class Node : public std::enable_shared_from_this<Node> {
    public:
        // Prevent copying
        Node(const Node&) = delete;
        Node& operator=(const Node&) = delete;

        virtual ~Node() = default;

        float getDepth() const noexcept;

        // Type checking methods
        virtual bool isPrimary() const noexcept = 0;
        bool isHub() const noexcept { return !isPrimary(); }

        std::vector<NodePtr> getChildren() const;
        std::vector<NodePtr> getParents() const;
        std::vector<NodePtr> getSiblings() const;
        std::unordered_set<Node*> getAllAncestors() const;
        std::unordered_set<Node*> getAllDescendants() const;

        // Graph connections
        virtual void addChild(const NodePtr& child);
        void insertChildAt(size_t position, const NodePtr& child);
		virtual void addParent(const NodePtr& parent) = 0; // Primary and hub nodes have different parent management rules

        bool removeChild(const NodePtr& child);
        bool removeParent(const NodePtr& parent);

		void replaceChild(const NodePtr& oldChild, const NodePtr& newChild);
        void replaceChild(const NodePtr& oldChild, const std::vector<NodePtr>& newChildren);
		void replaceParent(const NodePtr& oldParent, const NodePtr& newParent);

        // Left and Right equivalences - only for PrimaryNodes
        virtual NodePtr getLeft() const noexcept = 0;
        virtual void addLeft(const NodePtr& left) = 0;
        virtual bool removeLeft() = 0;

        virtual NodePtr getRight() const noexcept = 0;
        virtual void addRight(const NodePtr& right) = 0;
        virtual bool removeRight() = 0;

        void recomputeDepth();

    protected:
        explicit Node(float depth = 0);

        float depth_;
        std::vector<WeakNodePtr> parents_;
        std::vector<WeakNodePtr> children_;
    };

    // PrimaryNode class - must have name, left, and right; can only have one parent
    class PrimaryNode : public Node {
    public:
        explicit PrimaryNode(std::string name);

        // Properties management
        const std::string& getName() const noexcept;
        void setName(const std::string& name) noexcept;

        // Type checking
        bool isPrimary() const noexcept override { return true; }

        // Parent management
        void addParent(const NodePtr& parent) override;

        // Left and Right equivalences
        NodePtr getLeft() const noexcept override;
        void addLeft(const NodePtr& left) override;
        bool removeLeft() override;

        NodePtr getRight() const noexcept override;
        void addRight(const NodePtr& right) override;
        bool removeRight() override;

    private:
        std::string name_;
        WeakNodePtr left_;
        WeakNodePtr right_;
    };

    // HubNode Class - no name, no left/right; can have multiple parents
    class HubNode : public Node {
    public:
        HubNode();

        // Type checking
        bool isPrimary() const noexcept override { return false; }

        // Parent management - allows multiple parents
        void addParent(const NodePtr& parent) override;
        void insertParentAt(size_t position, const NodePtr& parent);

        // Left and Right equivalences - not supported for Hub nodes
        NodePtr getLeft() const noexcept override { return nullptr; };
        void addLeft(const NodePtr& left) override {};
        bool removeLeft() override { return false; };

        NodePtr getRight() const noexcept override { return nullptr; };
        void addRight(const NodePtr& left) override {};
        bool removeRight() override { return false; };
    };

}