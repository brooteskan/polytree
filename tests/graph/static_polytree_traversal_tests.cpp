#include <gtest/gtest.h>

#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <ranges>

using namespace wz::core::algo::next;
using namespace wz::core::graph;

namespace
{
    struct NodeData
    {
        std::uint32_t value{};
    };

    struct EdgeData
    {
        std::uint32_t value{};
    };

    auto make_document_tree()
    {
        PolytreeBuilder<NodeData, EdgeData> builder;
        std::ranges::for_each(std::views::iota(0u, 7u), [&](std::uint32_t value)
        {
            add_node(builder, NodeData{value});
        });

        EXPECT_TRUE(add_edge(builder, 0u, 1u, EdgeData{1}));
        EXPECT_TRUE(add_edge(builder, 1u, 2u, EdgeData{2}));
        EXPECT_TRUE(add_edge(builder, 1u, 3u, EdgeData{3}));
        EXPECT_TRUE(add_edge(builder, 0u, 4u, EdgeData{4}));
        EXPECT_TRUE(add_edge(builder, 0u, 5u, EdgeData{5}));
        EXPECT_TRUE(add_edge(builder, 5u, 6u, EdgeData{6}));
        return *build(std::move(builder));
    }

    auto make_forest()
    {
        PolytreeBuilder<NodeData, EdgeData> builder;
        std::ranges::for_each(std::views::iota(0u, 4u), [&](std::uint32_t value)
        {
            add_node(builder, NodeData{value});
        });
        EXPECT_TRUE(add_edge(builder, 0u, 1u));
        EXPECT_TRUE(add_edge(builder, 2u, 3u));
        return *build(std::move(builder));
    }

    struct BoundedSink
    {
        std::size_t capacity{};
        std::size_t count{};

        bool push(NodeHandle)
        {
            if (count >= capacity)
            {
                return false;
            }
            ++count;
            return true;
        }
    };
}

TEST(PolytreeTraversal, CanonicalOrdersPreserveCharacterizedOrdering)
{
    const auto storage = make_document_tree();
    const auto& tree = storage.polytree;
    constexpr std::array expected_depth_first{0u, 5u, 6u, 4u, 1u, 3u, 2u};
    constexpr std::array expected_breadth_first{0u, 1u, 4u, 5u, 2u, 3u, 6u};
    constexpr std::array expected_ancestors{1u, 0u};

    EXPECT_TRUE(std::ranges::equal(depth_first_order(tree, 0u), expected_depth_first));
    EXPECT_TRUE(std::ranges::equal(
        breadth_first_order(tree, 0u),
        expected_breadth_first));
    EXPECT_TRUE(std::ranges::equal(ancestor_order(tree, 3u), expected_ancestors));
}

TEST(PolytreeTraversal, VisitorAdaptersConsumeCanonicalOrders)
{
    const auto storage = make_document_tree();
    const auto& tree = storage.polytree;
    std::vector<NodeHandle> depth_first;
    std::vector<NodeHandle> breadth_first;
    std::vector<NodeHandle> ancestors;

    dfs(tree, 0u, [&](NodeHandle node) { depth_first.push_back(node); });
    bfs(tree, 0u, [&](NodeHandle node) { breadth_first.push_back(node); });
    walk_ancestors(tree, 3u, [&](NodeHandle node) { ancestors.push_back(node); });

    EXPECT_TRUE(std::ranges::equal(depth_first, depth_first_order(tree, 0u)));
    EXPECT_TRUE(std::ranges::equal(breadth_first, breadth_first_order(tree, 0u)));
    EXPECT_TRUE(std::ranges::equal(ancestors, ancestor_order(tree, 3u)));
}

TEST(PolytreeTraversal, SinkAdaptersReportCompletionAndEarlyTermination)
{
    const auto storage = make_document_tree();
    const auto& tree = storage.polytree;
    BoundedSink complete{7};
    BoundedSink breadth_limited{2};
    BoundedSink depth_limited{3};
    BoundedSink ancestor_limited{1};

    EXPECT_EQ(bfs(tree, 0u, complete), execution_status::completed);
    EXPECT_EQ(bfs(tree, 0u, breadth_limited), execution_status::truncated);
    EXPECT_EQ(dfs(tree, 0u, depth_limited), execution_status::truncated);
    EXPECT_EQ(
        walk_ancestors(tree, 3u, ancestor_limited),
        execution_status::truncated);
    EXPECT_EQ(complete.count, 7u);
    EXPECT_EQ(breadth_limited.count, 2u);
    EXPECT_EQ(depth_limited.count, 3u);
    EXPECT_EQ(ancestor_limited.count, 1u);
}

TEST(PolytreeTraversal, PipelineSinkTruncationPropagatesThroughTraversal)
{
    const auto storage = make_document_tree();
    BoundedSink output{2};
    const auto pipeline = filter([](NodeHandle) { return true; });
    auto sink = as_sink(pipeline, output);

    EXPECT_EQ(bfs(storage.polytree, 0u, sink), execution_status::truncated);
    EXPECT_EQ(output.count, 2u);
}

TEST(PolytreeTraversal, MaterializersExposeCompletionStatus)
{
    const auto storage = make_document_tree();
    const auto& tree = storage.polytree;
    std::array<NodeHandle, 7> complete_scratch{};
    std::array<NodeHandle, 2> limited_scratch{};
    std::array<NodeHandle, 1> ancestor_scratch{};

    const auto complete = bfs_materialize(tree, 0u, complete_scratch);
    const auto limited = dfs_materialize(tree, 0u, limited_scratch);
    const auto ancestors = ancestors_materialize(tree, 3u, ancestor_scratch);

    EXPECT_EQ(complete.status, execution_status::completed);
    EXPECT_FALSE(complete.was_truncated());
    EXPECT_EQ(limited.status, execution_status::truncated);
    EXPECT_TRUE(limited.was_truncated());
    EXPECT_EQ(ancestors.status, execution_status::truncated);
    EXPECT_TRUE(std::ranges::equal(limited.values, std::array{0u, 5u}));
    EXPECT_TRUE(std::ranges::equal(ancestors.values, std::array{1u}));
}

TEST(PolytreeTraversal, RootFirstAncestorTruncationPreservesBaselineValues)
{
    const auto storage = make_document_tree();
    std::array<NodeHandle, 1> scratch{};
    const auto result = ancestors_materialize_root_first(
        storage.polytree,
        3u,
        scratch);

    EXPECT_EQ(result.status, execution_status::truncated);
    EXPECT_TRUE(std::ranges::equal(result.values, std::array{1u}));
}

TEST(PolytreeEvaluationPlan, CachesEveryContiguousEvaluationView)
{
    const auto storage = make_document_tree();
    const auto& tree = storage.polytree;
    const auto plan = evaluation_plan(tree);
    constexpr std::array expected_topological{0u, 5u, 6u, 4u, 1u, 3u, 2u};
    constexpr std::array expected_reverse{2u, 3u, 1u, 4u, 6u, 5u, 0u};
    constexpr std::array expected_roots{0u};
    constexpr std::array expected_dependency{0u, 5u, 4u, 1u, 6u, 3u, 2u};
    constexpr std::array<std::uint32_t, 4> expected_offsets{0u, 1u, 4u, 7u};

    EXPECT_TRUE(std::ranges::equal(plan.topological_order, expected_topological));
    EXPECT_TRUE(std::ranges::equal(
        plan.reverse_topological_order,
        expected_reverse));
    EXPECT_TRUE(std::ranges::equal(plan.roots, expected_roots));
    EXPECT_TRUE(std::ranges::equal(plan.dependency_order, expected_dependency));
    EXPECT_TRUE(std::ranges::equal(
        plan.dependency_level_offsets,
        expected_offsets));
    EXPECT_EQ(plan.node_count(), 7u);
    EXPECT_EQ(plan.level_count(), 3u);
    EXPECT_TRUE(std::ranges::equal(plan.dependency_level(0), std::array{0u}));
    EXPECT_TRUE(std::ranges::equal(
        dependency_level(tree, 1),
        std::array{5u, 4u, 1u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.dependency_level(2),
        std::array{6u, 3u, 2u}));
    EXPECT_TRUE(plan.dependency_level(3).empty());
    EXPECT_EQ(plan.topological_order.data(), topo_order(tree).data());
    EXPECT_EQ(plan.roots.data(), roots(tree).data());
}

TEST(PolytreeEvaluationPlan, ForestRootsStayAscendingAndLevelsStayDeterministic)
{
    const auto storage = make_forest();
    const auto plan = evaluation_plan(storage.polytree);

    EXPECT_TRUE(std::ranges::equal(plan.roots, std::array{0u, 2u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.topological_order,
        std::array{2u, 3u, 0u, 1u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.reverse_topological_order,
        std::array{1u, 0u, 3u, 2u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.dependency_order,
        std::array{2u, 0u, 3u, 1u}));
    EXPECT_TRUE(std::ranges::equal(
        plan.dependency_level_offsets,
        std::array<std::uint32_t, 3>{0u, 2u, 4u}));
}

TEST(PolytreeEvaluationPlan, EmptyTopologyHasAnEmptyCompletePlan)
{
    PolytreeBuilder<NodeData, EdgeData> builder;
    const auto storage = build(std::move(builder));
    ASSERT_TRUE(storage.has_value());
    const auto plan = evaluation_plan(storage->polytree);
    std::array<NodeHandle, 0> scratch{};
    const auto roots_result = roots_materialize(storage->polytree, scratch);

    EXPECT_EQ(plan.node_count(), 0u);
    EXPECT_EQ(plan.level_count(), 0u);
    EXPECT_TRUE(plan.topological_order.empty());
    EXPECT_TRUE(plan.reverse_topological_order.empty());
    EXPECT_TRUE(plan.roots.empty());
    EXPECT_TRUE(plan.dependency_order.empty());
    EXPECT_TRUE(std::ranges::equal(
        plan.dependency_level_offsets,
        std::array<std::uint32_t, 1>{0u}));
    EXPECT_EQ(roots_result.status, execution_status::completed);
}

TEST(PolytreeEvaluationPlan, RootMaterializationReportsTruncation)
{
    const auto storage = make_forest();
    std::array<NodeHandle, 1> scratch{};
    const auto result = roots_materialize(storage.polytree, scratch);

    EXPECT_EQ(result.status, execution_status::truncated);
    EXPECT_TRUE(std::ranges::equal(result.values, std::array{0u}));
}

TEST(PolytreeTraversal, DeepChainUsesIterativeCanonicalOrders)
{
    constexpr std::uint32_t node_total = 10'000;
    PolytreeBuilder<NodeData, EdgeData> builder;
    std::ranges::for_each(std::views::iota(0u, node_total), [&](std::uint32_t value)
    {
        add_node(builder, NodeData{value});
    });
    std::ranges::for_each(std::views::iota(1u, node_total), [&](NodeHandle node)
    {
        EXPECT_TRUE(add_edge(builder, node - 1, node));
    });
    const auto storage = build(std::move(builder));
    ASSERT_TRUE(storage.has_value());
    const auto& tree = storage->polytree;

    EXPECT_EQ(depth_first_order(tree, 0u).size(), node_total);
    EXPECT_EQ(breadth_first_order(tree, 0u).size(), node_total);
    EXPECT_EQ(ancestor_order(tree, node_total - 1).size(), node_total - 1);
    EXPECT_EQ(evaluation_plan(tree).level_count(), node_total);
}

TEST(PolytreeEvaluationPlan, WideTreeGroupsIndependentChildrenTogether)
{
    constexpr std::uint32_t child_total = 4'096;
    PolytreeBuilder<NodeData, EdgeData> builder;
    add_node(builder, NodeData{0});
    std::ranges::for_each(
        std::views::iota(1u, child_total + 1),
        [&](NodeHandle child)
        {
            add_node(builder, NodeData{child});
            EXPECT_TRUE(add_edge(builder, 0u, child));
        });
    const auto storage = build(std::move(builder));
    ASSERT_TRUE(storage.has_value());
    const auto plan = evaluation_plan(storage->polytree);

    EXPECT_EQ(plan.level_count(), 2u);
    EXPECT_EQ(plan.dependency_level(0).size(), 1u);
    EXPECT_EQ(plan.dependency_level(1).size(), child_total);
    EXPECT_EQ(breadth_first_order(storage->polytree, 0u).size(), child_total + 1);
}
