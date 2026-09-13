#include <graph/ancestor_closure.h>
#include <gtest/gtest.h>
#include <array>

using namespace wz::core::graph;

TEST(AncestorClosure, ExactOrderDuplicatesEmptyAndCapacityReuse)
{
    PolytreeBuilder<int, int> builder;
    std::ranges::for_each(std::views::iota(0, 8), [&](int n) { add_node(builder, n); });
    ASSERT_TRUE(add_edge(builder, 0, 1));
    ASSERT_TRUE(add_edge(builder, 0, 2));
    ASSERT_TRUE(add_edge(builder, 1, 3));
    ASSERT_TRUE(add_edge(builder, 1, 4));
    ASSERT_TRUE(add_edge(builder, 5, 6));
    auto storage = build(std::move(builder));
    const auto& tree = storage->polytree;
    AncestorClosureWorkspace workspace;
    workspace.prepare(evaluation_plan(tree));
    const auto* capacity = workspace.order.data();
    const std::array<NodeHandle, 5> seeds{4, 3, 6, 4, 1};
    std::vector<NodeHandle> expected;
    std::ranges::copy_if(evaluation_plan(tree).reverse_topological_order, std::back_inserter(expected),
        [](NodeHandle n) { return n != 2 && n != 7; });
    EXPECT_TRUE(std::ranges::equal(ancestor_closure_order(tree, seeds, workspace), expected));
    EXPECT_EQ(capacity, workspace.order.data());
    EXPECT_TRUE(ancestor_closure_order(tree, {}, workspace).empty());
    EXPECT_TRUE(std::ranges::equal(ancestor_closure_order(tree, seeds, workspace), expected));
    EXPECT_EQ(capacity, workspace.order.data());
}

TEST(AncestorClosure, RandomForestMatchesIndependentParentChains)
{
    uint32_t state = 12345;
    const auto next = [&] { state = state * 1664525u + 1013904223u; return state; };
    std::ranges::for_each(std::views::iota(0, 100), [&](int)
    {
        PolytreeBuilder<int, int> builder;
        std::ranges::for_each(std::views::iota(0, 300), [&](int n)
        {
            add_node(builder, n);
            if (n && next() % 4) EXPECT_TRUE(add_edge(builder, NodeHandle(next() % n), NodeHandle(n)));
        });
        auto storage = build(std::move(builder));
        const auto& tree = storage->polytree;
        AncestorClosureWorkspace workspace;
        workspace.prepare(evaluation_plan(tree));
        std::array<NodeHandle, 20> seeds;
        std::ranges::generate(seeds, [&] { return NodeHandle(next() % 300); });
        std::vector<bool> included(300);
        std::ranges::for_each(seeds, [&](NodeHandle seed)
        {
            included[seed] = true;
            std::ranges::for_each(ancestor_order(tree, seed), [&](NodeHandle n) { included[n] = true; });
        });
        std::vector<NodeHandle> expected;
        std::ranges::copy_if(evaluation_plan(tree).reverse_topological_order, std::back_inserter(expected),
            [&](NodeHandle n) { return included[n]; });
        EXPECT_TRUE(std::ranges::equal(ancestor_closure_order(tree, seeds, workspace), expected));
    });
}

TEST(AncestorClosure, DeepChainAndRebind)
{
    PolytreeBuilder<int, int> builder;
    std::ranges::for_each(std::views::iota(0, 10000), [&](int n)
    {
        add_node(builder, n);
        if (n) ASSERT_TRUE(add_edge(builder, NodeHandle(n - 1), NodeHandle(n)));
    });
    auto storage = build(std::move(builder));
    AncestorClosureWorkspace workspace;
    workspace.prepare(evaluation_plan(storage->polytree));
    const std::array<NodeHandle, 1> seeds{9999};
    EXPECT_TRUE(std::ranges::equal(ancestor_closure_order(storage->polytree, seeds, workspace),
        evaluation_plan(storage->polytree).reverse_topological_order));
    auto empty = build(PolytreeBuilder<int, int>{});
    workspace.prepare(evaluation_plan(empty->polytree));
    EXPECT_TRUE(ancestor_closure_order(empty->polytree, {}, workspace).empty());
}
