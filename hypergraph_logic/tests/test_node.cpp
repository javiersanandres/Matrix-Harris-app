#include <gtest/gtest.h>
#include "Node.h"
#include "Hypergraph.h"

using namespace hypergraph_logic;

namespace hypergraph_logic::node_tests {

    // ============================================================================
    // Real Node Tests
    // ============================================================================

    TEST(RealNode, ConstructorWorks) {
        auto node = std::make_shared<Node>("TestNode");
        EXPECT_EQ(node->getName(), "TestNode");
        EXPECT_FALSE(node->isDummy());
    }

    TEST(RealNode, NameConstructorAppliesDefaultAttributes) {
        auto node = std::make_shared<Node>("TestNode");
        const NodeAttributes& a = node->getAttributes();
        EXPECT_EQ(a.name, "TestNode");
        EXPECT_EQ(a.shape, NodeShape::Rectangle);
        EXPECT_EQ(a.colour, (Color{ 255, 255, 200, 255 }));
        EXPECT_EQ(a.font_colour, (Color{ 0, 0, 0, 255 }));
        EXPECT_EQ(a.font_size, NodeAttributes::DEFAULT_FONT_SIZE);
        EXPECT_EQ(a.fire, FireState::None);
        EXPECT_FALSE(node->isFire());
        EXPECT_FALSE(node->hasAshes());
    }

    TEST(RealNode, AttributesConstructorKeepsEveryField) {
        NodeAttributes a("Fire");
        a.shape = NodeShape::Rhombus;
        a.colour = { 255, 0, 0, 255 };
        a.font_colour = { 255, 255, 255, 128 };
        a.font_size = 14;
        a.fire = FireState::FireWithAshes;

        auto node = std::make_shared<Node>(a);
        EXPECT_FALSE(node->isDummy());
        EXPECT_EQ(node->getAttributes(), a);
        EXPECT_EQ(node->getShape(), NodeShape::Rhombus);
        EXPECT_EQ(node->getColour(), (Color{ 255, 0, 0, 255 }));
        EXPECT_EQ(node->getFontColour(), (Color{ 255, 255, 255, 128 }));
        EXPECT_EQ(node->getFontSize(), 14);
        EXPECT_TRUE(node->isFire());
        EXPECT_TRUE(node->hasAshes());
    }

    TEST(RealNode, FireStateQueries) {
        NodeAttributes a("F");
        a.fire = FireState::Fire;
        EXPECT_TRUE(a.isFire());
        EXPECT_FALSE(a.hasAshes());
    }

    TEST(RealNode, SetNameKeepsOtherAttributes) {
        NodeAttributes a("Old");
        a.shape = NodeShape::Circle;
        auto node = std::make_shared<Node>(a);
        node->setName("New");
        EXPECT_EQ(node->getName(), "New");
        EXPECT_EQ(node->getShape(), NodeShape::Circle);
    }

    TEST(RealNode, SetAttributesReplacesEverything) {
        auto node = std::make_shared<Node>("Old");
        NodeAttributes a("New");
        a.shape = NodeShape::Circle;
        a.fire = FireState::Fire;
        node->setAttributes(a);
        EXPECT_EQ(node->getAttributes(), a);
    }

    // ============================================================================
    // Dummy Node Tests
    // ============================================================================

    TEST(DummyNode, ConstructorSetsDummyFlag) {
        auto dummy = std::make_shared<Node>();

        EXPECT_TRUE(dummy->isDummy());
        EXPECT_EQ(dummy->getName(), "");
    }

    TEST(DummyNode, ReportsDefaultAttributes) {
        auto dummy = std::make_shared<Node>();
        EXPECT_EQ(dummy->getAttributes(), NodeAttributes{});
        EXPECT_FALSE(dummy->isFire());
    }

    TEST(ColourHex, RoundTripAndFormats) {
        const Color c{ 0x0A, 0xB0, 0xFF, 0x80 };
        EXPECT_EQ(colorToHex(c), "#0AB0FF80");
        EXPECT_EQ(colorFromHex("#0AB0FF80"), c);
        EXPECT_EQ(colorFromHex("#0ab0ff80"), c);
        EXPECT_EQ(colorFromHex("#0AB0FF"), (Color{ 0x0A, 0xB0, 0xFF, 255 }));
        for (const char* bad : { "", "#12", "0AB0FF", "#0AB0FG", "#0AB0FF8" })
            EXPECT_THROW(colorFromHex(bad), std::invalid_argument) << bad;
    }

    TEST(DummyNode, CannotBeGivenAttributes) {
        auto dummy = std::make_shared<Node>();
        EXPECT_THROW(dummy->setAttributes(NodeAttributes("X")), std::logic_error);
        EXPECT_THROW(dummy->setName("X"), std::logic_error);
        EXPECT_TRUE(dummy->isDummy());
    }

    // ============================================================================
    // Adjacency Query Tests
    // ============================================================================

    TEST(Node, GetChildrenWorks) {
        auto parent = std::make_shared<Node>("Parent");
        auto child1 = std::make_shared<Node>("Child1");
        auto child2 = std::make_shared<Node>("Child2");

        parent->addChild(child1);
        parent->addChild(child2);

        auto children = parent->getChildren();
        EXPECT_EQ(children.size(), 2u);
        EXPECT_TRUE(std::find(children.begin(), children.end(), child1) != children.end());
        EXPECT_TRUE(std::find(children.begin(), children.end(), child2) != children.end());
    }

    TEST(Node, GetChildrenReturnsEmptyList) {
        auto node = std::make_shared<Node>("Node");
        auto children = node->getChildren();
        EXPECT_EQ(children.size(), 0u);
    }

    TEST(Node, GetParentsWorks) {
        auto parent1 = std::make_shared<Node>("Parent1");
        auto parent2 = std::make_shared<Node>("Parent2");
        auto child = std::make_shared<Node>("Child");

        child->addParent(parent1);
        child->addParent(parent2);

        auto parents = child->getParents();
        EXPECT_EQ(parents.size(), 2u);
        EXPECT_TRUE(std::find(parents.begin(), parents.end(), parent1) != parents.end());
        EXPECT_TRUE(std::find(parents.begin(), parents.end(), parent2) != parents.end());
    }

    TEST(Node, GetParentsReturnsEmptyList) {
        auto node = std::make_shared<Node>("Node");
        auto parents = node->getParents();
        EXPECT_EQ(parents.size(), 0u);
    }

    TEST(Node, GetAllAncestorsWorks) {
        auto root = std::make_shared<Node>("Root");
        auto middle = std::make_shared<Node>("Middle");
        auto leaf = std::make_shared<Node>("Leaf");

        root->addChild(middle);
        middle->addParent(root);

        middle->addChild(leaf);
        leaf->addParent(middle);

        auto ancestors = leaf->getAllAncestors();
        EXPECT_EQ(ancestors.size(), 2u);
        EXPECT_TRUE(ancestors.count(middle.get()) > 0);
        EXPECT_TRUE(ancestors.count(root.get()) > 0);
    }

    TEST(Node, GetAllAncestorsReturnsEmptySet) {
        auto node = std::make_shared<Node>("Node");
        auto ancestors = node->getAllAncestors();
        EXPECT_EQ(ancestors.size(), 0u);
    }

    TEST(Node, GetAllDescendantsReturnsEmptySet) {
        auto node = std::make_shared<Node>("Node");
        auto descendants = node->getAllDescendants();
        EXPECT_EQ(descendants.size(), 0u);
    }

    // ============================================================================
    // Mutation Tests
    // ============================================================================

    TEST(Node, AddParentWorks) {
        auto parent = std::make_shared<Node>("Parent");
        auto child = std::make_shared<Node>("Child");

        child->addParent(parent);

        auto parents = child->getParents();
        EXPECT_EQ(parents.size(), 1u);
        EXPECT_EQ(parents[0], parent);
    }

    TEST(Node, AddParentIgnoresNull) {
        auto child = std::make_shared<Node>("Child");
        child->addParent(nullptr);

        EXPECT_EQ(child->getParents().size(), 0u);
    }

    TEST(Node, AddParentIgnoresDuplicates) {
        auto parent = std::make_shared<Node>("Parent");
        auto child = std::make_shared<Node>("Child");

        parent->addChild(child);
        child->addParent(parent);
        child->addParent(parent); // Duplicate

        EXPECT_EQ(child->getParents().size(), 1u);
    }

    TEST(Node, AddChildWorks) {
        auto parent = std::make_shared<Node>("Parent");
        auto child = std::make_shared<Node>("Child");

        parent->addChild(child);

        auto children = parent->getChildren();
        EXPECT_EQ(children.size(), 1u);
        EXPECT_EQ(children[0], child);
    }

    TEST(Node, AddChildIgnoresNull) {
        auto parent = std::make_shared<Node>("Parent");
        parent->addChild(nullptr);

        EXPECT_EQ(parent->getChildren().size(), 0u);
    }

    TEST(Node, AddChildIgnoresDuplicates) {
        auto parent = std::make_shared<Node>("Parent");
        auto child = std::make_shared<Node>("Child");

        parent->addChild(child);
        parent->addChild(child); // Duplicate

        EXPECT_EQ(parent->getChildren().size(), 1u);
    }

    TEST(Node, RemoveParentWorks) {
        auto parent = std::make_shared<Node>("Parent");
        auto child = std::make_shared<Node>("Child");

        parent->addChild(child);
        child->addParent(parent);
        ASSERT_EQ(child->getParents().size(), 1u);

        bool removed = child->removeParent(parent);
        EXPECT_TRUE(removed);
        EXPECT_EQ(child->getParents().size(), 0u);
    }

    TEST(Node, RemoveParentReturnsFalseIfNotFound) {
        auto parent = std::make_shared<Node>("Parent");
        auto child = std::make_shared<Node>("Child");

        bool removed = child->removeParent(parent);
        EXPECT_FALSE(removed);
    }

    TEST(Node, RemoveParentReturnsFalseIfNull) {
        auto child = std::make_shared<Node>("Child");
        bool removed = child->removeParent(nullptr);
        EXPECT_FALSE(removed);
    }

    TEST(Node, RemoveChildWorks) {
        auto parent = std::make_shared<Node>("Parent");
        auto child = std::make_shared<Node>("Child");

        parent->addChild(child);
        ASSERT_EQ(parent->getChildren().size(), 1u);

        bool removed = parent->removeChild(child);
        EXPECT_TRUE(removed);
        EXPECT_EQ(parent->getChildren().size(), 0u);
    }

    TEST(Node, RemoveChildReturnsFalseIfNotFound) {
        auto parent = std::make_shared<Node>("Parent");
        auto child = std::make_shared<Node>("Child");

        bool removed = parent->removeChild(child);
        EXPECT_FALSE(removed);
    }

    TEST(Node, RemoveChildReturnsFalseIfNull) {
        auto parent = std::make_shared<Node>("Parent");
        bool removed = parent->removeChild(nullptr);
        EXPECT_FALSE(removed);
    }

    // ============================================================================
    // Replace Tests
    // ============================================================================

    TEST(Node, ReplaceParentWorks) {
        auto oldParent = std::make_shared<Node>("OldParent");
        auto newParent = std::make_shared<Node>("NewParent");
        auto child = std::make_shared<Node>("Child");

        oldParent->addChild(child);
        child->addParent(oldParent);

        child->replaceParent(oldParent, newParent);

        auto parents = child->getParents();
        EXPECT_EQ(parents.size(), 1u);
        EXPECT_EQ(parents[0], newParent);
    }

    TEST(Node, ReplaceChildWithSingleChildWorks) {
        auto parent = std::make_shared<Node>("Parent");
        auto oldChild = std::make_shared<Node>("OldChild");
        auto newChild = std::make_shared<Node>("NewChild");

        parent->addChild(oldChild);

        parent->replaceChild(oldChild, newChild);

        auto children = parent->getChildren();
        EXPECT_EQ(children.size(), 1u);
        EXPECT_EQ(children[0], newChild);
    }

    TEST(Node, ReplaceChildWithMultipleChildrenWorks) {
        auto parent = std::make_shared<Node>("Parent");
        auto oldChild = std::make_shared<Node>("OldChild");
        auto newChild1 = std::make_shared<Node>("NewChild1");
        auto newChild2 = std::make_shared<Node>("NewChild2");

        parent->addChild(oldChild);

        std::vector<NodePtr> newChildren = { newChild1, newChild2 };
        parent->replaceChild(oldChild, newChildren);

        auto children = parent->getChildren();
        EXPECT_EQ(children.size(), 2u);
        EXPECT_EQ(children[0], newChild1);
        EXPECT_EQ(children[1], newChild2);
    }

    TEST(Node, ReplaceChildWithMultipleChildrenWorks2) {
        auto parent = std::make_shared<Node>("Parent");
        auto oldChild = std::make_shared<Node>("OldChild");
        auto oldChild2 = std::make_shared<Node>("OldChild2");
        auto newChild1 = std::make_shared<Node>("NewChild1");
        auto newChild2 = std::make_shared<Node>("NewChild2");

        parent->addChild(oldChild);
        parent->addChild(oldChild2);

        std::vector<NodePtr> newChildren = { newChild1, newChild2 };
        parent->replaceChild(oldChild, newChildren);

        auto children = parent->getChildren();
        EXPECT_EQ(children.size(), 3u);
        EXPECT_EQ(children[0], newChild1);
        EXPECT_EQ(children[1], newChild2);
        EXPECT_EQ(children[2], oldChild2);
    }

    TEST(Node, ReplaceChildWithMultipleChildrenWorks3) {
        auto parent = std::make_shared<Node>("Parent");
        auto oldChild = std::make_shared<Node>("OldChild");
        auto oldChild2 = std::make_shared<Node>("OldChild2");
        auto oldChild3 = std::make_shared<Node>("OldChild3");
        auto newChild1 = std::make_shared<Node>("NewChild1");
        auto newChild2 = std::make_shared<Node>("NewChild2");

        parent->addChild(oldChild);
        parent->addChild(oldChild2);
        parent->addChild(oldChild3);

        std::vector<NodePtr> newChildren = { newChild1, newChild2 };
        parent->replaceChild(oldChild2, newChildren);

        auto children = parent->getChildren();
        EXPECT_EQ(children.size(), 4u);
        EXPECT_EQ(children[0], oldChild);
        EXPECT_EQ(children[1], newChild1);
        EXPECT_EQ(children[2], newChild2);
        EXPECT_EQ(children[3], oldChild3);
    }

    // ============================================================================
    // Replace Tests: Weak Ptr Handling (Fixed in updated Node.cpp)
    // ============================================================================

    TEST(Node, ReplaceChildAvoidsDuplicatesWithWeakPtr) {
        // Tests the fix: properly locks weak_ptr before comparing
        auto parent = std::make_shared<Node>("Parent");
        auto oldChild = std::make_shared<Node>("OldChild");
        auto newChild = std::make_shared<Node>("NewChild");

        parent->addChild(oldChild);
        parent->addChild(newChild); // Already has newChild

        // Try to replace oldChild with newChild (which already exists)
        parent->replaceChild(oldChild, newChild);

        auto children = parent->getChildren();
        // Should have oldChild removed, but NOT duplicate newChild added
        EXPECT_EQ(children.size(), 2u);
        EXPECT_EQ(children[0], oldChild);
		EXPECT_EQ(children[1], newChild);
    }

    TEST(Node, ReplaceChildMultipleAvoidsDuplicatesInVector) {
        // Tests the fix: properly checks != children_.end() for each candidate
        auto parent = std::make_shared<Node>("Parent");
        auto oldChild = std::make_shared<Node>("OldChild");
        auto newChild1 = std::make_shared<Node>("NewChild1");
        auto newChild2 = std::make_shared<Node>("NewChild2");
        auto existingChild = std::make_shared<Node>("ExistingChild");

        parent->addChild(oldChild);
        parent->addChild(existingChild); // Already exists

        std::vector<NodePtr> newChildren = { newChild1, existingChild, newChild2 };
        parent->replaceChild(oldChild, newChildren);

        auto children = parent->getChildren();
        // Should have: newChild1, existingChild (not duplicated), newChild2
        EXPECT_EQ(children.size(), 3u);
        EXPECT_EQ(children[0], newChild1);
        EXPECT_EQ(children[1], newChild2);
        EXPECT_EQ(children[2], existingChild);
    }

    TEST(Node, ReplaceParentAvoidsDuplicateNewParent) {
        // Tests the fix: checks if newParent already exists before replacing
        auto oldParent = std::make_shared<Node>("OldParent");
        auto newParent = std::make_shared<Node>("NewParent");
        auto child = std::make_shared<Node>("Child");

        child->addParent(oldParent);
        child->addParent(newParent); // Already has newParent

        // Try to replace oldParent with newParent (which already exists)
        child->replaceParent(oldParent, newParent);

        auto parents = child->getParents();
        // Should keep newParent, and NOT try to replace oldParent
        EXPECT_EQ(parents.size(), 2u);
        EXPECT_TRUE(std::find(parents.begin(), parents.end(), newParent) != parents.end());
        EXPECT_TRUE(std::find(parents.begin(), parents.end(), oldParent) != parents.end());
    }

    TEST(Node, ReplaceParentIgnoresNullNewParent) {
        // Tests the fix: returns early if newParent is null
        auto oldParent = std::make_shared<Node>("OldParent");
        auto child = std::make_shared<Node>("Child");

        child->addParent(oldParent);

        child->replaceParent(oldParent, nullptr);

        auto parents = child->getParents();
        // Should still have oldParent (no replacement occurred)
        EXPECT_EQ(parents.size(), 1u);
        EXPECT_EQ(parents[0], oldParent);
    }
} // namespace hypergraph_logic::node_tests