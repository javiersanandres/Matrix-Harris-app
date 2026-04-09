#include <gtest/gtest.h>
#include "Hyperedge.h"


namespace hypergraph_logic::hyperedge_tests {

    // ============================================================================
    // Original Hyperedge Tests
    // ============================================================================

    TEST(OriginalHyperedge, ConstructorWithSourcesAndTargets) {
        auto source1 = std::make_shared<Node>("Source1");
        auto source2 = std::make_shared<Node>("Source2");
        auto target1 = std::make_shared<Node>("Target1");
        auto target2 = std::make_shared<Node>("Target2");

        std::vector<NodePtr> sources = { source1, source2 };
        std::vector<NodePtr> targets = { target1, target2 };

        auto edge = std::make_shared<Hyperedge>(sources, targets);

        EXPECT_FALSE(edge->isSegment());
        auto retrievedSources = edge->getSources();
        auto retrievedTargets = edge->getTargets();

        EXPECT_EQ(retrievedSources.size(), 2u);
        EXPECT_EQ(retrievedTargets.size(), 2u);
    }

    TEST(OriginalHyperedge, LayerInitiallyUnset) {
        auto source = std::make_shared<Node>("Source");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(std::vector<NodePtr>{source}, std::vector<NodePtr>{target});

        // Layer should be unset (-1) when not created through Hypergraph
        EXPECT_EQ(edge->getLayer(), -1);
    }

    TEST(OriginalHyperedge, GetOriginReturnsNull) {
        auto source = std::make_shared<Node>("Source");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(std::vector<NodePtr>{source}, std::vector<NodePtr>{target});

        auto origin = edge->getOrigin();
        EXPECT_TRUE(origin.expired());  // Null weak_ptr is expired
    }

    TEST(OriginalHyperedge, EmptySourcesAndTargets) {
        std::vector<NodePtr> emptySources;
        std::vector<NodePtr> emptyTargets;

        auto edge = std::make_shared<Hyperedge>(emptySources, emptyTargets);

        EXPECT_EQ(edge->getSources().size(), 0u);
        EXPECT_EQ(edge->getTargets().size(), 0u);
    }

    // ============================================================================
    // Segment Hyperedge Tests
    // ============================================================================

    TEST(SegmentHyperedge, LayerInitiallyUnset) {
        auto original = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{std::make_shared<Node>("S")},
            std::vector<NodePtr>{std::make_shared<Node>("T")}
        );

        auto source = std::make_shared<Node>("SegSource");
        auto target = std::make_shared<Node>("SegTarget");
        auto origin_weak = std::weak_ptr<Hyperedge>(original);

        auto segment = std::make_shared<Hyperedge>(origin_weak, 
            std::vector<NodePtr>{source}, 
            std::vector<NodePtr>{target}
        );

        // Layer should be unset (-1) when not created through Hypergraph
        EXPECT_EQ(segment->getLayer(), -1);
    }

    TEST(SegmentHyperedge, ConstructorWithOrigin) {
        auto original = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{std::make_shared<Node>("S")},
            std::vector<NodePtr>{std::make_shared<Node>("T")}
        );

        auto source = std::make_shared<Node>("SegSource");
        auto target = std::make_shared<Node>("SegTarget");
        auto origin_weak = std::weak_ptr<Hyperedge>(original);

        auto segment = std::make_shared<Hyperedge>(origin_weak, 
            std::vector<NodePtr>{source}, 
            std::vector<NodePtr>{target}
        );

        EXPECT_TRUE(segment->isSegment());
        auto retrieved_origin = segment->getOrigin();
        EXPECT_FALSE(retrieved_origin.expired());  // Origin should not be expired

        auto locked = retrieved_origin.lock();
        EXPECT_EQ(locked, original);
    }

    // ============================================================================
    // Source and Target Query Tests
    // ============================================================================

    TEST(Hyperedge, GetSourcesWorks) {
        auto source1 = std::make_shared<Node>("Source1");
        auto source2 = std::make_shared<Node>("Source2");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source1, source2},
            std::vector<NodePtr>{target}
        );

        auto sources = edge->getSources();
        EXPECT_EQ(sources.size(), 2u);
        EXPECT_TRUE(std::find(sources.begin(), sources.end(), source1) != sources.end());
        EXPECT_TRUE(std::find(sources.begin(), sources.end(), source2) != sources.end());
    }

    TEST(Hyperedge, GetTargetsWorks) {
        auto source = std::make_shared<Node>("Source");
        auto target1 = std::make_shared<Node>("Target1");
        auto target2 = std::make_shared<Node>("Target2");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{target1, target2}
        );

        auto targets = edge->getTargets();
        EXPECT_EQ(targets.size(), 2u);
        EXPECT_TRUE(std::find(targets.begin(), targets.end(), target1) != targets.end());
        EXPECT_TRUE(std::find(targets.begin(), targets.end(), target2) != targets.end());
    }

    TEST(Hyperedge, GetSourcesReturnsEmptyList) {
        auto target = std::make_shared<Node>("Target");
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{},
            std::vector<NodePtr>{target}
        );

        EXPECT_EQ(edge->getSources().size(), 0u);
    }

    TEST(Hyperedge, GetTargetsReturnsEmptyList) {
        auto source = std::make_shared<Node>("Source");
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{}
        );

        EXPECT_EQ(edge->getTargets().size(), 0u);
    }

    // ============================================================================
    // Add Source and Target Tests
    // ============================================================================

    TEST(Hyperedge, AddSourceWorks) {
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{},
            std::vector<NodePtr>{std::make_shared<Node>("T")}
        );

        auto newSource = std::make_shared<Node>("NewSource");
        edge->addSource(newSource);

        auto sources = edge->getSources();
        EXPECT_EQ(sources.size(), 1u);
        EXPECT_EQ(sources[0], newSource);
    }

    TEST(Hyperedge, AddTargetWorks) {
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{std::make_shared<Node>("S")},
            std::vector<NodePtr>{}
        );

        auto newTarget = std::make_shared<Node>("NewTarget");
        edge->addTarget(newTarget);

        auto targets = edge->getTargets();
        EXPECT_EQ(targets.size(), 1u);
        EXPECT_EQ(targets[0], newTarget);
    }

    TEST(Hyperedge, AddSourceIgnoresNull) {
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{},
            std::vector<NodePtr>{std::make_shared<Node>("T")}
        );

        edge->addSource(nullptr);
        EXPECT_EQ(edge->getSources().size(), 0u);
    }

    TEST(Hyperedge, AddTargetIgnoresNull) {
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{std::make_shared<Node>("S")},
            std::vector<NodePtr>{}
        );

        edge->addTarget(nullptr);
        EXPECT_EQ(edge->getTargets().size(), 0u);
    }

    TEST(Hyperedge, AddSourceIgnoresDuplicates) {
        auto source = std::make_shared<Node>("Source");
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{std::make_shared<Node>("T")}
        );

        edge->addSource(source);
        EXPECT_EQ(edge->getSources().size(), 1u);
    }

    TEST(Hyperedge, AddTargetIgnoresDuplicates) {
        auto target = std::make_shared<Node>("Target");
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{std::make_shared<Node>("S")},
            std::vector<NodePtr>{target}
        );

        edge->addTarget(target);
        EXPECT_EQ(edge->getTargets().size(), 1u);
    }

    // ============================================================================
    // Remove Source and Target Tests
    // ============================================================================

    TEST(Hyperedge, RemoveSourceWorks) {
        auto source = std::make_shared<Node>("Source");
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{std::make_shared<Node>("T")}
        );

        bool removed = edge->removeSource(source);
        EXPECT_TRUE(removed);
        EXPECT_EQ(edge->getSources().size(), 0u);
    }

    TEST(Hyperedge, RemoveTargetWorks) {
        auto target = std::make_shared<Node>("Target");
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{std::make_shared<Node>("S")},
            std::vector<NodePtr>{target}
        );

        bool removed = edge->removeTarget(target);
        EXPECT_TRUE(removed);
        EXPECT_EQ(edge->getTargets().size(), 0u);
    }

    TEST(Hyperedge, RemoveSourceReturnsFalseIfNotFound) {
        auto source = std::make_shared<Node>("Source");
        auto s = std::make_shared<Node>("S");
        auto t = std::make_shared<Node>("T");

        auto edge = std::make_shared<Hyperedge>(std::vector<NodePtr>{s}, std::vector<NodePtr>{t});

        bool removed = edge->removeSource(source);
        EXPECT_FALSE(removed);
    }

    TEST(Hyperedge, RemoveTargetReturnsFalseIfNotFound) {
        auto target = std::make_shared<Node>("Target");
        auto s = std::make_shared<Node>("S");
        auto t = std::make_shared<Node>("T");

        auto edge = std::make_shared<Hyperedge>(std::vector<NodePtr>{s}, std::vector<NodePtr>{t});

        bool removed = edge->removeTarget(target);
        EXPECT_FALSE(removed);
    }

    TEST(Hyperedge, RemoveSourceReturnsFalseIfNull) {
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{std::make_shared<Node>("S")},
            std::vector<NodePtr>{std::make_shared<Node>("T")}
        );

        bool removed = edge->removeSource(nullptr);
        EXPECT_FALSE(removed);
    }

    TEST(Hyperedge, RemoveTargetReturnsFalseIfNull) {
        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{std::make_shared<Node>("S")},
            std::vector<NodePtr>{std::make_shared<Node>("T")}
        );

        bool removed = edge->removeTarget(nullptr);
        EXPECT_FALSE(removed);
    }

    // ============================================================================
    // Replace Source Tests
    // ============================================================================

    TEST(Hyperedge, ReplaceSourceWithSingleNodeWorks) {
        auto oldSource = std::make_shared<Node>("OldSource");
        auto newSource = std::make_shared<Node>("NewSource");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{oldSource},
            std::vector<NodePtr>{target}
        );

        edge->replaceSource(oldSource, newSource);

        auto sources = edge->getSources();
        EXPECT_EQ(sources.size(), 1u);
        EXPECT_EQ(sources[0], newSource);
    }

    TEST(Hyperedge, ReplaceSourceWithMultipleNodesWorks) {
        auto oldSource = std::make_shared<Node>("OldSource");
        auto newSource1 = std::make_shared<Node>("NewSource1");
        auto newSource2 = std::make_shared<Node>("NewSource2");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{oldSource},
            std::vector<NodePtr>{target}
        );

        std::vector<NodePtr> newSources = { newSource1, newSource2 };
        edge->replaceSource(oldSource, newSources);

        auto sources = edge->getSources();
        EXPECT_EQ(sources.size(), 2u);
        EXPECT_EQ(sources[0], newSource1);
        EXPECT_EQ(sources[1], newSource2);
    }

    TEST(Hyperedge, ReplaceSourceIgnoresNullOldNode) {
        auto source = std::make_shared<Node>("Source");
        auto newSource = std::make_shared<Node>("NewSource");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{target}
        );

        edge->replaceSource(nullptr, newSource);

        auto sources = edge->getSources();
        EXPECT_EQ(sources.size(), 1u);
        EXPECT_EQ(sources[0], source); // Unchanged
    }

    TEST(Hyperedge, ReplaceSourceIgnoresEmptyNewNodes) {
        auto oldSource = std::make_shared<Node>("OldSource");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{oldSource},
            std::vector<NodePtr>{target}
        );

        std::vector<NodePtr> emptyNewSources;
        edge->replaceSource(oldSource, emptyNewSources);

        auto sources = edge->getSources();
        EXPECT_EQ(sources.size(), 1u);
        EXPECT_EQ(sources[0], oldSource); // Unchanged
    }

    // ============================================================================
    // Replace Target Tests
    // ============================================================================

    TEST(Hyperedge, ReplaceTargetWithSingleNodeWorks) {
        auto source = std::make_shared<Node>("Source");
        auto oldTarget = std::make_shared<Node>("OldTarget");
        auto newTarget = std::make_shared<Node>("NewTarget");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{oldTarget}
        );

        edge->replaceTarget(oldTarget, newTarget);

        auto targets = edge->getTargets();
        EXPECT_EQ(targets.size(), 1u);
        EXPECT_EQ(targets[0], newTarget);
    }

    TEST(Hyperedge, ReplaceTargetWithMultipleNodesWorks) {
        auto source = std::make_shared<Node>("Source");
        auto oldTarget = std::make_shared<Node>("OldTarget");
        auto newTarget1 = std::make_shared<Node>("NewTarget1");
        auto newTarget2 = std::make_shared<Node>("NewTarget2");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{oldTarget}
        );

        std::vector<NodePtr> newTargets = { newTarget1, newTarget2 };
        edge->replaceTarget(oldTarget, newTargets);

        auto targets = edge->getTargets();
        EXPECT_EQ(targets.size(), 2u);
        EXPECT_EQ(targets[0], newTarget1);
        EXPECT_EQ(targets[1], newTarget2);
    }

    TEST(Hyperedge, ReplaceTargetIgnoresNullOldNode) {
        auto source = std::make_shared<Node>("Source");
        auto target = std::make_shared<Node>("Target");
        auto newTarget = std::make_shared<Node>("NewTarget");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{target}
        );

        edge->replaceTarget(nullptr, newTarget);

        auto targets = edge->getTargets();
        EXPECT_EQ(targets.size(), 1u);
        EXPECT_EQ(targets[0], target); // Unchanged
    }

    TEST(Hyperedge, ReplaceTargetIgnoresEmptyNewNodes) {
        auto source = std::make_shared<Node>("Source");
        auto oldTarget = std::make_shared<Node>("OldTarget");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{oldTarget}
        );

        std::vector<NodePtr> emptyNewTargets;
        edge->replaceTarget(oldTarget, emptyNewTargets);

        auto targets = edge->getTargets();
        EXPECT_EQ(targets.size(), 1u);
        EXPECT_EQ(targets[0], oldTarget); // Unchanged
    }

    // ============================================================================
    // Replace Tests: Weak Ptr Handling (Fixed in updated Hyperedge.cpp)
    // ============================================================================

    TEST(Hyperedge, ReplaceSourceAvoidsDuplicatesWithWeakPtr) {
        // Tests the fix: properly locks weak_ptr before comparing
        auto oldSource = std::make_shared<Node>("OldSource");
        auto newSource = std::make_shared<Node>("NewSource");
        auto existingSource = std::make_shared<Node>("ExistingSource");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{oldSource, existingSource},
            std::vector<NodePtr>{target}
        );

        std::vector<NodePtr> newSources = { newSource, existingSource };
        edge->replaceSource(oldSource, newSources);

        auto sources = edge->getSources();
        // Should have: newSource, existingSource (not duplicated)
        EXPECT_EQ(sources.size(), 2u);
        EXPECT_TRUE(std::find(sources.begin(), sources.end(), newSource) != sources.end());
        EXPECT_TRUE(std::find(sources.begin(), sources.end(), existingSource) != sources.end());
    }

    TEST(Hyperedge, ReplaceTargetAvoidsDuplicatesWithWeakPtr) {
        // Tests the fix: properly locks weak_ptr before comparing
        auto source = std::make_shared<Node>("Source");
        auto oldTarget = std::make_shared<Node>("OldTarget");
        auto newTarget = std::make_shared<Node>("NewTarget");
        auto existingTarget = std::make_shared<Node>("ExistingTarget");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{oldTarget, existingTarget}
        );

        std::vector<NodePtr> newTargets = { newTarget, existingTarget };
        edge->replaceTarget(oldTarget, newTargets);

        auto targets = edge->getTargets();
        // Should have: newTarget, existingTarget (not duplicated)
        EXPECT_EQ(targets.size(), 2u);
        EXPECT_TRUE(std::find(targets.begin(), targets.end(), newTarget) != targets.end());
        EXPECT_TRUE(std::find(targets.begin(), targets.end(), existingTarget) != targets.end());
    }

    TEST(Hyperedge, ReplaceSourceMultipleDuplicatesFiltered) {
        // Tests the fix: properly checks != sources_.end() for each candidate
        auto oldSource = std::make_shared<Node>("OldSource");
        auto newSource1 = std::make_shared<Node>("NewSource1");
        auto newSource2 = std::make_shared<Node>("NewSource2");
        auto existingSource = std::make_shared<Node>("ExistingSource");
        auto target = std::make_shared<Node>("Target");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{oldSource, existingSource},
            std::vector<NodePtr>{target}
        );

        std::vector<NodePtr> newSources = { newSource1, existingSource, newSource2 };
        edge->replaceSource(oldSource, newSources);

        auto sources = edge->getSources();
        // Should have: newSource1, existingSource (not duplicated), newSource2
        EXPECT_EQ(sources.size(), 3u);
        EXPECT_EQ(sources[0], existingSource);
        EXPECT_EQ(sources[1], newSource1);
        EXPECT_EQ(sources[2], newSource2);
    }

    // ============================================================================
    // Complex Hyperedge Tests
    // ============================================================================

    TEST(Hyperedge, ComplexTopologyWithMultipleSourcesAndTargets) {
        auto source1 = std::make_shared<Node>("S1");
        auto source2 = std::make_shared<Node>("S2");
        auto source3 = std::make_shared<Node>("S3");
        auto target1 = std::make_shared<Node>("T1");
        auto target2 = std::make_shared<Node>("T2");

        auto edge = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source1, source2, source3},
            std::vector<NodePtr>{target1, target2}
        );

        auto sources = edge->getSources();
        auto targets = edge->getTargets();

        EXPECT_EQ(sources.size(), 3u);
        EXPECT_EQ(targets.size(), 2u);

        // Add more sources
        auto source4 = std::make_shared<Node>("S4");
        edge->addSource(source4);

        sources = edge->getSources();
        EXPECT_EQ(sources.size(), 4u);

        // Remove a source
        edge->removeSource(source1);
        sources = edge->getSources();
        EXPECT_EQ(sources.size(), 3u);
        EXPECT_TRUE(std::find(sources.begin(), sources.end(), source1) == sources.end());
    }

    TEST(Hyperedge, SegmentChain) {
        auto source = std::make_shared<Node>("Source");
        auto middle = std::make_shared<Node>("Middle");
        auto target = std::make_shared<Node>("Target");

        // Create original hyperedge
        auto original = std::make_shared<Hyperedge>(
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{target}
        );

        // Create first segment
        auto segment1 = std::make_shared<Hyperedge>(
            std::weak_ptr<Hyperedge>(original),
            std::vector<NodePtr>{source},
            std::vector<NodePtr>{middle}
        );

        // Create second segment
        auto segment2 = std::make_shared<Hyperedge>(
            std::weak_ptr<Hyperedge>(original),
            std::vector<NodePtr>{middle},
            std::vector<NodePtr>{target}
        );

        EXPECT_TRUE(segment1->isSegment());
        EXPECT_TRUE(segment2->isSegment());
        EXPECT_FALSE(original->isSegment());

        auto origin1 = segment1->getOrigin();
        auto origin2 = segment2->getOrigin();

        EXPECT_FALSE(origin1.expired());
        EXPECT_FALSE(origin2.expired());
        EXPECT_EQ(origin1.lock(), original);
        EXPECT_EQ(origin2.lock(), original);
    }

} // namespace hypergraph_logic::hyperedge_tests