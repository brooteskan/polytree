#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

#include <graph/mutable_polytree.h>
#include <graph/polytree_concepts.h>
#include <graph/polytree_freeze.h>
#include <graph/static_polytree.h>

using namespace wz::core::graph;

namespace
{
    using MutableTree = MutablePolytree<std::string, std::uint32_t>;
    using StaticTree = Polytree<std::string, std::uint32_t>;

    static_assert(PolytreeTopology<MutableTree>);
    static_assert(PolytreeTopology<StaticTree>);

    struct TrackedPayload
    {
        TrackedPayload()
        {
            ++alive;
        }

        explicit TrackedPayload(int initial_value)
            : value(initial_value)
        {
            ++alive;
        }

        TrackedPayload(const TrackedPayload& other)
            : value(other.value)
        {
            ++alive;
        }

        TrackedPayload(TrackedPayload&& other) noexcept
            : value(other.value)
        {
            ++alive;
        }

        TrackedPayload& operator=(const TrackedPayload& other)
        {
            value = other.value;
            return *this;
        }

        TrackedPayload& operator=(TrackedPayload&& other) noexcept
        {
            value = other.value;
            return *this;
        }

        ~TrackedPayload()
        {
            --alive;
        }

        static inline int alive{};
        int value{};
    };

    struct ThrowingPayload
    {
        ThrowingPayload()
        {
            ++alive;
        }

        explicit ThrowingPayload(int initial_value)
            : value(initial_value)
        {
            ++alive;
        }

        ThrowingPayload(const ThrowingPayload& other)
            : value(other.value)
        {
            if (throw_on_copy)
            {
                throw std::runtime_error("copy failure");
            }
            ++alive;
        }

        ThrowingPayload(ThrowingPayload&& other) noexcept
            : value(other.value)
        {
            ++alive;
        }

        ThrowingPayload& operator=(const ThrowingPayload& other)
        {
            if (throw_on_copy)
            {
                throw std::runtime_error("copy assignment failure");
            }
            value = other.value;
            return *this;
        }

        ThrowingPayload& operator=(ThrowingPayload&& other) noexcept
        {
            value = other.value;
            return *this;
        }

        ~ThrowingPayload()
        {
            --alive;
        }

        static inline bool throw_on_copy{};
        static inline int alive{};
        int value{};
    };
}

TEST(MutablePolytreeIdentity, StableIdsSurviveInsertionAndReparenting)
{
    MutableTree tree;
    const auto root_result = insert_root(tree, std::string{"root"});
    const auto other_result = insert_root(tree, std::string{"other"});
    ASSERT_TRUE(root_result);
    ASSERT_TRUE(other_result);
    const auto root = root_result.value();
    const auto other = other_result.value();

    const auto child_result = insert_child(
        tree,
        root,
        std::string{"child"},
        10u);
    ASSERT_TRUE(child_result);
    const auto child = child_result.value();
    const auto revision_before_move = revision(tree);

    EXPECT_TRUE(reparent(tree, child, other, 20u));
    EXPECT_EQ(child_result.value(), child);
    EXPECT_TRUE(contains(tree, root));
    EXPECT_TRUE(contains(tree, other));
    EXPECT_TRUE(contains(tree, child));
    EXPECT_EQ(parent(tree, child), other);
    EXPECT_EQ(parent_edge_data(tree, child), 20u);
    EXPECT_EQ(revision(tree), revision_before_move + 1);
}

TEST(MutablePolytreeOrdering, ReparentDetachAndReorderPreserveExplicitOrder)
{
    MutableTree tree;
    const auto root = insert_root(tree, std::string{"root"}).value();
    const auto other = insert_root(tree, std::string{"other"}).value();
    const auto a = insert_child(tree, root, std::string{"a"}, 1u).value();
    const auto b = insert_child(tree, root, std::string{"b"}, 2u).value();
    const auto c = insert_child(tree, root, std::string{"c"}, 3u).value();

    ASSERT_TRUE(reparent(tree, b, other, 20u));
    EXPECT_TRUE(std::ranges::equal(children(tree, root), std::array{a, c}));
    EXPECT_TRUE(std::ranges::equal(children(tree, other), std::array{b}));

    ASSERT_TRUE(detach_to_root(tree, b, 1u));
    EXPECT_TRUE(std::ranges::equal(roots(tree), std::array{root, b, other}));
    EXPECT_TRUE(is_root(tree, b));

    ASSERT_TRUE(reparent(tree, c, root, 30u, 0u));
    EXPECT_TRUE(std::ranges::equal(children(tree, root), std::array{c, a}));
    EXPECT_EQ(parent_edge_data(tree, c), 30u);
}

TEST(MutablePolytreeOrdering, PositionalInsertionAndIdNonReuseAreExplicit)
{
    MutableTree tree;
    const auto first = insert_root(tree, std::string{"first"}).value();
    const auto third = insert_root(tree, std::string{"third"}).value();
    const auto second = insert_root(tree, std::string{"second"}, 1u).value();
    const auto child_b = insert_child(
        tree,
        first,
        std::string{"b"},
        2u).value();
    const auto child_a = insert_child(
        tree,
        first,
        std::string{"a"},
        1u,
        0u).value();

    EXPECT_TRUE(std::ranges::equal(roots(tree), std::array{first, second, third}));
    EXPECT_TRUE(std::ranges::equal(
        children(tree, first),
        std::array{child_a, child_b}));

    ASSERT_TRUE(erase_subtree(tree, child_a));
    const auto replacement = insert_child(
        tree,
        first,
        std::string{"replacement"},
        3u).value();
    EXPECT_NE(replacement, child_a);
    EXPECT_GT(replacement.value, child_a.value);

    const auto revision_before = revision(tree);
    const auto invalid_parent = insert_child(
        tree,
        INVALID_STABLE_NODE,
        std::string{"invalid"},
        4u);
    const auto invalid_ordinal = insert_root(
        tree,
        std::string{"invalid"},
        99u);
    EXPECT_FALSE(invalid_parent);
    EXPECT_EQ(invalid_parent.error(), MutationError::invalid_parent);
    EXPECT_FALSE(invalid_ordinal);
    EXPECT_EQ(invalid_ordinal.error(), MutationError::invalid_ordinal);
    EXPECT_EQ(revision(tree), revision_before);
}

TEST(MutablePolytreeMutation, CycleAndInvalidOrdinalFailuresAreAtomic)
{
    MutableTree tree;
    const auto root = insert_root(tree, std::string{"root"}).value();
    const auto child = insert_child(
        tree,
        root,
        std::string{"child"},
        1u).value();
    const auto leaf = insert_child(
        tree,
        child,
        std::string{"leaf"},
        2u).value();
    const auto revision_before = revision(tree);
    const auto roots_before = std::vector<StableNodeId>(
        roots(tree).begin(),
        roots(tree).end());
    const auto children_before = std::vector<StableNodeId>(
        children(tree, root).begin(),
        children(tree, root).end());

    const auto cycle = reparent(tree, root, leaf, 3u);
    EXPECT_FALSE(cycle);
    EXPECT_EQ(cycle.error(), MutationError::cycle);
    const auto ordinal = reparent(tree, child, root, 4u, 99u);
    EXPECT_FALSE(ordinal);
    EXPECT_EQ(ordinal.error(), MutationError::invalid_ordinal);
    EXPECT_EQ(revision(tree), revision_before);
    EXPECT_TRUE(std::ranges::equal(roots(tree), roots_before));
    EXPECT_TRUE(std::ranges::equal(children(tree, root), children_before));
    EXPECT_EQ(parent(tree, root), INVALID_STABLE_NODE);
    EXPECT_EQ(parent(tree, child), root);
    EXPECT_EQ(parent(tree, leaf), child);
    EXPECT_TRUE(validate(tree));
}

TEST(MutablePolytreeMutation, EraseSubtreeInvalidatesOnlyRemovedIds)
{
    MutableTree tree;
    const auto root = insert_root(tree, std::string{"root"}).value();
    const auto removed_root = insert_child(
        tree,
        root,
        std::string{"removed"},
        1u).value();
    const auto removed_leaf = insert_child(
        tree,
        removed_root,
        std::string{"removed leaf"},
        2u).value();
    const auto survivor = insert_child(
        tree,
        root,
        std::string{"survivor"},
        3u).value();

    const auto erased = erase_subtree(tree, removed_root);
    ASSERT_TRUE(erased);
    EXPECT_EQ(erased.value(), 2u);
    EXPECT_FALSE(contains(tree, removed_root));
    EXPECT_FALSE(contains(tree, removed_leaf));
    EXPECT_TRUE(contains(tree, root));
    EXPECT_TRUE(contains(tree, survivor));
    EXPECT_TRUE(std::ranges::equal(children(tree, root), std::array{survivor}));
    EXPECT_EQ(node_count(tree), 2u);
    EXPECT_EQ(edge_count(tree), 1u);
    EXPECT_TRUE(validate(tree));
}

TEST(MutablePolytreeMutation, PayloadReplacementIsExplicitAndRevisioned)
{
    MutableTree tree;
    const auto root = insert_root(tree, std::string{"root"}).value();
    const auto child = insert_child(
        tree,
        root,
        std::string{"child"},
        1u).value();
    const auto revision_before = revision(tree);

    EXPECT_TRUE(replace_node_data(tree, child, std::string{"renamed"}));
    EXPECT_TRUE(replace_parent_edge_data(tree, child, 42u));
    EXPECT_EQ(node_data(tree, child), "renamed");
    EXPECT_EQ(parent_edge_data(tree, child), 42u);
    EXPECT_EQ(revision(tree), revision_before + 2);

    const auto invalid = replace_node_data(
        tree,
        INVALID_STABLE_NODE,
        std::string{"invalid"});
    EXPECT_FALSE(invalid);
    EXPECT_EQ(invalid.error(), MutationError::invalid_node);
    EXPECT_EQ(revision(tree), revision_before + 2);
}

TEST(PolytreeFreeze, PreservesForestAndChildOrderWithCanonicalMappings)
{
    MutableTree tree;
    const auto root = insert_root(tree, std::string{"root"}).value();
    const auto a = insert_child(tree, root, std::string{"a"}, 1u).value();
    const auto b = insert_child(tree, root, std::string{"b"}, 2u).value();
    const auto b_child = insert_child(
        tree,
        b,
        std::string{"b child"},
        3u).value();
    const auto other = insert_root(tree, std::string{"other"}).value();

    FreezeWorkspace workspace;
    auto outcome = freeze(tree, workspace);
    ASSERT_TRUE(outcome);
    const auto& frozen = outcome.value();
    const auto& topology = frozen.topology.polytree;
    const auto plan = evaluation_plan(topology);

    EXPECT_TRUE(std::ranges::equal(
        frozen.identities.runtime_to_authoring(),
        std::array{root, a, b, b_child, other}));
    EXPECT_EQ(frozen.identities.runtime_handle(root), 0u);
    EXPECT_EQ(frozen.identities.runtime_handle(a), 1u);
    EXPECT_EQ(frozen.identities.runtime_handle(b), 2u);
    EXPECT_EQ(frozen.identities.runtime_handle(b_child), 3u);
    EXPECT_EQ(frozen.identities.runtime_handle(other), 4u);
    EXPECT_EQ(frozen.identities.authoring_id(3u), b_child);
    EXPECT_EQ(frozen.identities.source_revision(), revision(tree));

    EXPECT_TRUE(std::ranges::equal(roots(topology), std::array{0u, 4u}));
    EXPECT_TRUE(std::ranges::equal(children(topology, 0u), std::array{1u, 2u}));
    EXPECT_TRUE(std::ranges::equal(children(topology, 2u), std::array{3u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.topological_order,
        std::array{4u, 0u, 2u, 3u, 1u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.reverse_topological_order,
        std::array{1u, 3u, 2u, 0u, 4u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.dependency_order,
        std::array{4u, 0u, 2u, 1u, 3u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.dependency_level_offsets,
        std::array<std::uint32_t, 4>{0u, 2u, 4u, 5u}));
    EXPECT_GT(frozen.metrics.reusable_scratch_capacity_bytes, 0u);
    EXPECT_GT(frozen.metrics.builder_capacity_bytes, 0u);
    EXPECT_GT(frozen.metrics.identity_mapping_capacity_bytes, 0u);
}

TEST(PolytreeFreeze, RepeatedFreezeIsDeterministicAndNonConsuming)
{
    MutableTree tree;
    const auto root = insert_root(tree, std::string{"root"}).value();
    (void)insert_child(tree, root, std::string{"a"}, 1u);
    (void)insert_child(tree, root, std::string{"b"}, 2u);
    const auto source_revision = revision(tree);
    FreezeWorkspace workspace;

    auto first = freeze(tree, workspace);
    auto second = freeze(tree, workspace);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const auto& first_tree = first->topology.polytree;
    const auto& second_tree = second->topology.polytree;
    const auto first_plan = evaluation_plan(first_tree);
    const auto second_plan = evaluation_plan(second_tree);

    EXPECT_EQ(revision(tree), source_revision);
    EXPECT_TRUE(std::ranges::equal(first_tree.node_data, second_tree.node_data));
    EXPECT_TRUE(std::ranges::equal(first_tree.parent, second_tree.parent));
    EXPECT_TRUE(std::ranges::equal(first_tree.out_offsets, second_tree.out_offsets));
    EXPECT_TRUE(std::ranges::equal(first_tree.out_neighbors, second_tree.out_neighbors));
    EXPECT_TRUE(std::ranges::equal(
        first_tree.out_edge_data,
        second_tree.out_edge_data));
    EXPECT_TRUE(std::ranges::equal(
        first_plan.topological_order,
        second_plan.topological_order));
    EXPECT_TRUE(std::ranges::equal(
        first_plan.reverse_topological_order,
        second_plan.reverse_topological_order));
    EXPECT_TRUE(std::ranges::equal(first_plan.roots, second_plan.roots));
    EXPECT_TRUE(std::ranges::equal(
        first_plan.dependency_order,
        second_plan.dependency_order));
    EXPECT_TRUE(std::ranges::equal(
        first_plan.dependency_level_offsets,
        second_plan.dependency_level_offsets));
    EXPECT_TRUE(std::ranges::equal(
        first->identities.runtime_to_authoring(),
        second->identities.runtime_to_authoring()));
    EXPECT_TRUE(std::ranges::equal(
        first->identities.authoring_to_runtime(),
        second->identities.authoring_to_runtime()));
}

TEST(PolytreeFreeze, SnapshotRevisionAndMappingsBecomeExplicitlyStale)
{
    MutableTree tree;
    const auto root = insert_root(tree, std::string{"root"}).value();
    const auto child = insert_child(
        tree,
        root,
        std::string{"child"},
        1u).value();
    const auto other = insert_root(tree, std::string{"other"}).value();
    auto first = freeze(tree);
    ASSERT_TRUE(first);
    ASSERT_EQ(first->identities.runtime_handle(child), 1u);
    const auto frozen_revision = first->identities.source_revision();

    ASSERT_TRUE(detach_to_root(tree, child, 0u));
    EXPECT_NE(frozen_revision, revision(tree));
    EXPECT_EQ(first->identities.runtime_handle(child), 1u);

    auto second = freeze(tree);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->identities.source_revision(), revision(tree));
    EXPECT_EQ(second->identities.runtime_handle(child), 0u);
    EXPECT_TRUE(std::ranges::equal(
        second->identities.runtime_to_authoring(),
        std::array{child, root, other}));
}

TEST(PolytreeFreeze, EmptyForestProducesCompleteEmptySnapshot)
{
    MutableTree tree;
    auto outcome = freeze(tree);
    ASSERT_TRUE(outcome);

    const auto plan = evaluation_plan(outcome->topology.polytree);
    EXPECT_EQ(plan.node_count(), 0u);
    EXPECT_EQ(plan.level_count(), 0u);
    EXPECT_TRUE(plan.roots.empty());
    EXPECT_TRUE(std::ranges::equal(
        plan.dependency_level_offsets,
        std::array<std::uint32_t, 1>{0u}));
    EXPECT_TRUE(outcome->identities.runtime_to_authoring().empty());
    EXPECT_TRUE(outcome->identities.authoring_to_runtime().empty());
}

TEST(PolytreeFreeze, DeepChainValidationAndFreezeAreIterative)
{
    constexpr std::uint32_t node_total = 10'000;
    MutablePolytree<std::uint32_t, std::uint32_t> tree;
    auto current = insert_root(tree, 0u).value();
    std::ranges::for_each(
        std::views::iota(1u, node_total),
        [&](std::uint32_t value)
        {
            current = insert_child(tree, current, value, value).value();
        });

    EXPECT_TRUE(validate(tree));
    auto outcome = freeze(tree);
    ASSERT_TRUE(outcome);
    EXPECT_EQ(node_count(outcome->topology.polytree), node_total);
    EXPECT_EQ(
        evaluation_plan(outcome->topology.polytree).level_count(),
        node_total);
}

TEST(PolytreeStorageLifetime, DestroysNonTrivialCompactPayloads)
{
    TrackedPayload::alive = 0;
    {
        PolytreeBuilder<TrackedPayload, TrackedPayload> builder;
        const auto root = add_node(builder, TrackedPayload{1});
        const auto child = add_node(builder, TrackedPayload{2});
        ASSERT_TRUE(add_edge(builder, root, child, TrackedPayload{3}));
        auto storage = build(std::move(builder));
        ASSERT_TRUE(storage);
        EXPECT_EQ(node_data(storage->polytree, root).value, 1);
        EXPECT_EQ(parent_edge_data(storage->polytree, child).value, 3);
        EXPECT_GT(TrackedPayload::alive, 0);
    }
    EXPECT_EQ(TrackedPayload::alive, 0);
}

TEST(PolytreeStorageLifetime, RollsBackPartiallyConstructedPayloads)
{
    ThrowingPayload::alive = 0;
    ThrowingPayload::throw_on_copy = false;
    {
        PolytreeBuilder<ThrowingPayload, ThrowingPayload> builder;
        const auto root = add_node(builder, ThrowingPayload{1});
        const auto child = add_node(builder, ThrowingPayload{2});
        ASSERT_TRUE(add_edge(builder, root, child, ThrowingPayload{3}));
        ThrowingPayload::throw_on_copy = true;
        EXPECT_THROW(
            (void)build(std::move(builder)),
            std::runtime_error);
        ThrowingPayload::throw_on_copy = false;
    }
    EXPECT_EQ(ThrowingPayload::alive, 0);
}
