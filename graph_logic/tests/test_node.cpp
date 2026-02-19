#include <gtest/gtest.h>
#include "Node.h"

using namespace graph_logic;

// ============================================================================
// PrimaryNode Tests
// ============================================================================

TEST(PrimaryNode, ConstructorSetsName) {
    auto node = std::make_shared<PrimaryNode>("TestNode");
    EXPECT_EQ(node->getName(), "TestNode");
    EXPECT_EQ(node->getDepth(), 0.0f);
    EXPECT_TRUE(node->isPrimary());
    EXPECT_FALSE(node->isHub());
}

TEST(PrimaryNode, SetNameWorks) {
    auto node = std::make_shared<PrimaryNode>("Original");
    EXPECT_EQ(node->getName(), "Original");
    
    node->setName("Modified");
    EXPECT_EQ(node->getName(), "Modified");
}

TEST(PrimaryNode, CanOnlyHaveOneParent) {
    auto parent1 = std::make_shared<PrimaryNode>("Parent1");
    auto parent2 = std::make_shared<PrimaryNode>("Parent2");
    auto child = std::make_shared<PrimaryNode>("Child");

    parent1->addChild(child);
    child->addParent(parent1);

    EXPECT_EQ(child->getParents().size(), 1u);
    
    // Adding second parent should throw
    EXPECT_THROW(child->addParent(parent2), std::logic_error);
    EXPECT_EQ(child->getParents().size(), 1u);
}

TEST(PrimaryNode, AddingDuplicateParentIsIgnored) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    parent->addChild(child);
    child->addParent(parent);
    child->addParent(parent); // Duplicate

    EXPECT_EQ(child->getParents().size(), 1u);
}

TEST(PrimaryNode, DepthFromPrimaryParent) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    parent->addChild(child);
    child->addParent(parent);
    child->recomputeDepth();

    EXPECT_EQ(parent->getDepth(), 0.0f);
    EXPECT_EQ(child->getDepth(), 1.0f); // Primary parent: +1.0
}

TEST(PrimaryNode, DepthFromHubParent) {
    auto primary = std::make_shared<PrimaryNode>("Primary");
    auto hub = std::make_shared<HubNode>();
    auto child = std::make_shared<PrimaryNode>("Child");

    primary->addChild(hub);
    hub->addParent(primary);
    hub->recomputeDepth();

    hub->addChild(child);
    child->addParent(hub);
    child->recomputeDepth();

    EXPECT_EQ(primary->getDepth(), 0.0f);
    EXPECT_EQ(hub->getDepth(), 0.5f);    // Hub: +0.5
    EXPECT_EQ(child->getDepth(), 1.0f);  // Hub parent: 0.5 + 0.5 = 1.0
}

TEST(PrimaryNode, LeftRightEquivalenceBasic) {
    auto left = std::make_shared<PrimaryNode>("Left");
    auto right = std::make_shared<PrimaryNode>("Right");

    left->addRight(right);

    EXPECT_EQ(left->getRight(), right);
    EXPECT_EQ(right->getLeft(), left);
}

TEST(PrimaryNode, AddLeftSetsBothEnds) {
    auto left = std::make_shared<PrimaryNode>("Left");
    auto right = std::make_shared<PrimaryNode>("Right");

    right->addLeft(left);

    EXPECT_EQ(left->getRight(), right);
    EXPECT_EQ(right->getLeft(), left);
}

TEST(PrimaryNode, CannotAddRightWhenAlreadyExists) {
    auto a = std::make_shared<PrimaryNode>("A");
    auto b = std::make_shared<PrimaryNode>("B");
    auto c = std::make_shared<PrimaryNode>("C");

    a->addRight(b);
    a->addRight(c); // Should be ignored

    EXPECT_EQ(a->getRight(), b);
    EXPECT_EQ(c->getLeft(), nullptr);
}

TEST(PrimaryNode, CannotAddLeftWhenTargetHasRight) {
    auto a = std::make_shared<PrimaryNode>("A");
    auto b = std::make_shared<PrimaryNode>("B");
    auto c = std::make_shared<PrimaryNode>("C");

    a->addRight(b);
    c->addLeft(b); // Should be ignored, b already has left

    EXPECT_EQ(b->getLeft(), a);
    EXPECT_EQ(c->getRight(), nullptr);
}

TEST(PrimaryNode, RemoveLeftClearsBothEnds) {
    auto left = std::make_shared<PrimaryNode>("Left");
    auto right = std::make_shared<PrimaryNode>("Right");

    left->addRight(right);
    ASSERT_EQ(right->getLeft(), left);

    bool removed = right->removeLeft();
    EXPECT_TRUE(removed);
    EXPECT_EQ(left->getRight(), nullptr);
    EXPECT_EQ(right->getLeft(), nullptr);
}

TEST(PrimaryNode, RemoveRightClearsBothEnds) {
    auto left = std::make_shared<PrimaryNode>("Left");
    auto right = std::make_shared<PrimaryNode>("Right");

    left->addRight(right);
    ASSERT_EQ(left->getRight(), right);

    bool removed = left->removeRight();
    EXPECT_TRUE(removed);
    EXPECT_EQ(left->getRight(), nullptr);
    EXPECT_EQ(right->getLeft(), nullptr);
}

TEST(PrimaryNode, RemoveNonExistentLeftReturnsFalse) {
    auto node = std::make_shared<PrimaryNode>("Node");
    EXPECT_FALSE(node->removeLeft());
}

TEST(PrimaryNode, RemoveNonExistentRightReturnsFalse) {
    auto node = std::make_shared<PrimaryNode>("Node");
    EXPECT_FALSE(node->removeRight());
}

TEST(PrimaryNode, AddNullLeftOrRightIsIgnored) {
    auto node = std::make_shared<PrimaryNode>("Node");
    
    node->addLeft(nullptr);
    node->addRight(nullptr);

    EXPECT_EQ(node->getLeft(), nullptr);
    EXPECT_EQ(node->getRight(), nullptr);
}

TEST(PrimaryNode, DepthPropagatesFromEquivalence) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto left = std::make_shared<PrimaryNode>("Left");
    auto right = std::make_shared<PrimaryNode>("Right");

    left->addRight(right);
    parent->addChild(left);
    left->addParent(parent);
    left->recomputeDepth();

    EXPECT_EQ(parent->getDepth(), 0.0f);
    EXPECT_EQ(left->getDepth(), 1.0f);
    EXPECT_EQ(right->getDepth(), 1.0f); // Same as left
}

TEST(PrimaryNode, EquivalenceDepthTakeMaximum) {
    auto p1 = std::make_shared<PrimaryNode>("P1");
    auto p2 = std::make_shared<PrimaryNode>("P2");
    auto left = std::make_shared<PrimaryNode>("Left");
    auto right = std::make_shared<PrimaryNode>("Right");

    left->addRight(right);
    
    p1->addChild(left);
    left->addParent(p1);
    left->recomputeDepth();

    p2->addChild(p1);
    p1->addParent(p2);
    p1->recomputeDepth();

    EXPECT_EQ(p2->getDepth(), 0.0f);
    EXPECT_EQ(p1->getDepth(), 1.0f);
    EXPECT_EQ(left->getDepth(), 2.0f);
    EXPECT_EQ(right->getDepth(), 2.0f); // Propagated from left
}

// ============================================================================
// HubNode Tests
// ============================================================================

TEST(HubNode, ConstructorInitializesCorrectly) {
    auto hub = std::make_shared<HubNode>();
    
    EXPECT_EQ(hub->getDepth(), 0.0f);
    EXPECT_FALSE(hub->isPrimary());
    EXPECT_TRUE(hub->isHub());
}

TEST(HubNode, CanHaveMultipleParents) {
    auto parent1 = std::make_shared<PrimaryNode>("Parent1");
    auto parent2 = std::make_shared<PrimaryNode>("Parent2");
    auto hub = std::make_shared<HubNode>();

    parent1->addChild(hub);
    hub->addParent(parent1);
    
    parent2->addChild(hub);
    hub->addParent(parent2);

    EXPECT_EQ(hub->getParents().size(), 2u);
}

TEST(HubNode, AddingDuplicateParentIsIgnored) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto hub = std::make_shared<HubNode>();

    parent->addChild(hub);
    hub->addParent(parent);
    hub->addParent(parent); // Duplicate

    EXPECT_EQ(hub->getParents().size(), 1u);
}

TEST(HubNode, DepthIsHalfInteger) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto hub = std::make_shared<HubNode>();

    parent->addChild(hub);
    hub->addParent(parent);
    hub->recomputeDepth();

    EXPECT_EQ(parent->getDepth(), 0.0f);
    EXPECT_EQ(hub->getDepth(), 0.5f); // Primary parent (0.0) + 0.5
}

TEST(HubNode, DepthFromMultipleParents) {
    auto p1 = std::make_shared<PrimaryNode>("P1");
    auto p2 = std::make_shared<PrimaryNode>("P2");
    auto p3 = std::make_shared<PrimaryNode>("P3");
    auto hub = std::make_shared<HubNode>();

    p1->addChild(hub);
    hub->addParent(p1);

    // Create hierarchy: p3 -> p2 -> p1
    p3->addChild(p2);
    p2->addParent(p3);
    p2->recomputeDepth();

    p2->addChild(p1);
    p1->addParent(p2);
    p1->recomputeDepth();

    p2->addChild(hub);
    hub->addParent(p2);
    hub->recomputeDepth();

    EXPECT_EQ(p3->getDepth(), 0.0f);
    EXPECT_EQ(p2->getDepth(), 1.0f);
    EXPECT_EQ(p1->getDepth(), 2.0f);
    EXPECT_EQ(hub->getDepth(), 2.5f); // max(2.0 + 0.5, 1.0 + 0.5) = 2.5
}

TEST(HubNode, LeftRightNotSupported) {
    auto hub = std::make_shared<HubNode>();
    auto primary = std::make_shared<PrimaryNode>("Primary");

    hub->addLeft(primary);
    hub->addRight(primary);

    EXPECT_EQ(hub->getLeft(), nullptr);
    EXPECT_EQ(hub->getRight(), nullptr);
}

TEST(HubNode, RemoveLeftRightReturnsFalse) {
    auto hub = std::make_shared<HubNode>();

    EXPECT_FALSE(hub->removeLeft());
    EXPECT_FALSE(hub->removeRight());
}

// ============================================================================
// Base Node Tests (Common Functionality)
// ============================================================================

TEST(Node, AddChildWorks) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    parent->addChild(child);

    auto children = parent->getChildren();
    EXPECT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], child);
}

TEST(Node, AddNullChildIsIgnored) {
    auto node = std::make_shared<PrimaryNode>("Node");
    node->addChild(nullptr);

    EXPECT_EQ(node->getChildren().size(), 0u);
}

TEST(Node, AddDuplicateChildIsIgnored) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    parent->addChild(child);
    parent->addChild(child); // Duplicate

    EXPECT_EQ(parent->getChildren().size(), 1u);
}

TEST(Node, AddNullParentIsIgnored) {
    auto node = std::make_shared<PrimaryNode>("Node");
    node->addParent(nullptr);

    EXPECT_EQ(node->getParents().size(), 0u);
}

TEST(Node, RemoveChildWorks) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    parent->addChild(child);
    ASSERT_EQ(parent->getChildren().size(), 1u);

    bool removed = parent->removeChild(child);
    EXPECT_TRUE(removed);
    EXPECT_EQ(parent->getChildren().size(), 0u);
}

TEST(Node, RemoveNonExistentChildReturnsFalse) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    bool removed = parent->removeChild(child);
    EXPECT_FALSE(removed);
}

TEST(Node, RemoveNullChildReturnsFalse) {
    auto node = std::make_shared<PrimaryNode>("Node");
    EXPECT_FALSE(node->removeChild(nullptr));
}

TEST(Node, RemoveParentWorks) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    parent->addChild(child);
    child->addParent(parent);
    ASSERT_EQ(child->getParents().size(), 1u);

    bool removed = child->removeParent(parent);
    EXPECT_TRUE(removed);
    EXPECT_EQ(child->getParents().size(), 0u);
}

TEST(Node, RemoveNonExistentParentReturnsFalse) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    bool removed = child->removeParent(parent);
    EXPECT_FALSE(removed);
}

TEST(Node, RemoveNullParentReturnsFalse) {
    auto node = std::make_shared<PrimaryNode>("Node");
    EXPECT_FALSE(node->removeParent(nullptr));
}

TEST(Node, GetSiblingsWorks) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child1 = std::make_shared<PrimaryNode>("Child1");
    auto child2 = std::make_shared<PrimaryNode>("Child2");
    auto child3 = std::make_shared<PrimaryNode>("Child3");

    parent->addChild(child1);
    parent->addChild(child2);
    parent->addChild(child3);
    child1->addParent(parent);
    child2->addParent(parent);
    child3->addParent(parent);

    auto siblings = child1->getSiblings();
    EXPECT_EQ(siblings.size(), 2u);
    EXPECT_TRUE(std::find(siblings.begin(), siblings.end(), child2) != siblings.end());
    EXPECT_TRUE(std::find(siblings.begin(), siblings.end(), child3) != siblings.end());
}

TEST(Node, GetAllAncestorsWorks) {
    auto root = std::make_shared<PrimaryNode>("Root");
    auto middle = std::make_shared<PrimaryNode>("Middle");
    auto leaf = std::make_shared<PrimaryNode>("Leaf");

    root->addChild(middle);
    middle->addParent(root);
    middle->recomputeDepth();

    middle->addChild(leaf);
    leaf->addParent(middle);
    leaf->recomputeDepth();

    auto ancestors = leaf->getAllAncestors();
    EXPECT_EQ(ancestors.size(), 2u);
    EXPECT_TRUE(ancestors.count(middle.get()) > 0);
    EXPECT_TRUE(ancestors.count(root.get()) > 0);
}

TEST(Node, GetAllDescendantsWorks) {
    auto root = std::make_shared<PrimaryNode>("Root");
    auto middle = std::make_shared<PrimaryNode>("Middle");
    auto leaf = std::make_shared<PrimaryNode>("Leaf");

    root->addChild(middle);
    middle->addParent(root);
    middle->recomputeDepth();

    middle->addChild(leaf);
    leaf->addParent(middle);
    leaf->recomputeDepth();

    auto descendants = root->getAllDescendants();
    EXPECT_EQ(descendants.size(), 2u);
    EXPECT_TRUE(descendants.count(middle.get()) > 0);
    EXPECT_TRUE(descendants.count(leaf.get()) > 0);
}

// ============================================================================
// Depth Propagation Tests
// ============================================================================

TEST(DepthPropagation, ThroughSimpleHierarchy) {
    auto root = std::make_shared<PrimaryNode>("Root");
    auto child = std::make_shared<PrimaryNode>("Child");
    auto grandchild = std::make_shared<PrimaryNode>("Grandchild");

    root->addChild(child);
    child->addParent(root);
    child->recomputeDepth();

    child->addChild(grandchild);
    grandchild->addParent(child);
    grandchild->recomputeDepth();

    EXPECT_EQ(root->getDepth(), 0.0f);
    EXPECT_EQ(child->getDepth(), 1.0f);
    EXPECT_EQ(grandchild->getDepth(), 2.0f);
}

TEST(DepthPropagation, ThroughHub) {
    auto primary1 = std::make_shared<PrimaryNode>("P1");
    auto primary2 = std::make_shared<PrimaryNode>("P2");
    auto hub = std::make_shared<HubNode>();
    auto child = std::make_shared<PrimaryNode>("Child");

    // P1 -> Hub
    primary1->addChild(hub);
    hub->addParent(primary1);
    hub->recomputeDepth();

    // P2 -> Hub
    primary2->addChild(hub);
    hub->addParent(primary2);
    hub->recomputeDepth();

    // Hub -> Child
    hub->addChild(child);
    child->addParent(hub);
    child->recomputeDepth();

    EXPECT_EQ(primary1->getDepth(), 0.0f);
    EXPECT_EQ(primary2->getDepth(), 0.0f);
    EXPECT_EQ(hub->getDepth(), 0.5f);
    EXPECT_EQ(child->getDepth(), 1.0f);
}

TEST(DepthPropagation, ComplexHierarchyWithHubsAndEquivalences) {
    auto root = std::make_shared<PrimaryNode>("Root");
    auto a = std::make_shared<PrimaryNode>("A");
    auto b = std::make_shared<PrimaryNode>("B");
    auto hub = std::make_shared<HubNode>();
    auto c = std::make_shared<PrimaryNode>("C");

    // Root -> A, A <-> B (equivalence)
    root->addChild(a);
    a->addParent(root);
    a->recomputeDepth();

    a->addRight(b);
    b->recomputeDepth();

    // A -> Hub, B -> Hub
    a->addChild(hub);
    hub->addParent(a);
    
    b->addChild(hub);
    hub->addParent(b);
    hub->recomputeDepth();

    // Hub -> C
    hub->addChild(c);
    c->addParent(hub);
    c->recomputeDepth();

    EXPECT_EQ(root->getDepth(), 0.0f);
    EXPECT_EQ(a->getDepth(), 1.0f);
    EXPECT_EQ(b->getDepth(), 1.0f);
    EXPECT_EQ(hub->getDepth(), 1.5f);
    EXPECT_EQ(c->getDepth(), 2.0f);
}

TEST(DepthPropagation, RecomputeWhenParentAdded) {
    auto grandparent = std::make_shared<PrimaryNode>("Grandparent");
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    // Initially: parent -> child
    parent->addChild(child);
    child->addParent(parent);
    child->recomputeDepth();

    EXPECT_EQ(parent->getDepth(), 0.0f);
    EXPECT_EQ(child->getDepth(), 1.0f);

    // Now add: grandparent -> parent
    grandparent->addChild(parent);
    parent->addParent(grandparent);
    parent->recomputeDepth();

    EXPECT_EQ(grandparent->getDepth(), 0.0f);
    EXPECT_EQ(parent->getDepth(), 1.0f);
    EXPECT_EQ(child->getDepth(), 2.0f); // Should propagate
}

TEST(DepthPropagation, RemoveParentRecomputesDepth) {
    auto parent = std::make_shared<PrimaryNode>("Parent");
    auto child = std::make_shared<PrimaryNode>("Child");

    parent->addChild(child);
    child->addParent(parent);
    child->recomputeDepth();

    EXPECT_EQ(child->getDepth(), 1.0f);

    child->removeParent(parent);
    child->recomputeDepth();

    EXPECT_EQ(child->getDepth(), 0.0f); // Back to root level
}

// ============================================================================
// Type Checking Tests
// ============================================================================

TEST(TypeChecking, PrimaryNodeTypeMethods) {
    NodePtr node = std::make_shared<PrimaryNode>("Primary");
    
    EXPECT_TRUE(node->isPrimary());
    EXPECT_FALSE(node->isHub());
}

TEST(TypeChecking, HubNodeTypeMethods) {
    NodePtr node = std::make_shared<HubNode>();
    
    EXPECT_FALSE(node->isPrimary());
    EXPECT_TRUE(node->isHub());
}

TEST(TypeChecking, DynamicCastToPrimaryNode) {
    NodePtr node = std::make_shared<PrimaryNode>("Primary");
    
    auto primary = std::dynamic_pointer_cast<PrimaryNode>(node);
    EXPECT_NE(primary, nullptr);
    EXPECT_EQ(primary->getName(), "Primary");
}

TEST(TypeChecking, DynamicCastToHubNode) {
    NodePtr node = std::make_shared<HubNode>();
    
    auto hub = std::dynamic_pointer_cast<HubNode>(node);
    EXPECT_NE(hub, nullptr);
}

TEST(TypeChecking, DynamicCastFailsForWrongType) {
    NodePtr primary = std::make_shared<PrimaryNode>("Primary");
    NodePtr hub = std::make_shared<HubNode>();
    
    auto primaryAsHub = std::dynamic_pointer_cast<HubNode>(primary);
    auto hubAsPrimary = std::dynamic_pointer_cast<PrimaryNode>(hub);
    
    EXPECT_EQ(primaryAsHub, nullptr);
    EXPECT_EQ(hubAsPrimary, nullptr);
}