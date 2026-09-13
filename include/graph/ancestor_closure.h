#pragma once

#include <graph/static_polytree.h>

namespace wz::core::graph
{
    // One owner, bound explicitly to one immutable topology generation. No tree
    // pointers are retained. prepare invalidates previously returned orders.
    struct AncestorClosureWorkspace
    {
        std::vector<NodeHandle> order;
        std::vector<std::uint32_t> rank;
        std::vector<std::uint8_t> marked;

        void prepare(const PolytreeEvaluationPlan& plan)
        {
            order.clear();
            order.reserve(plan.node_count());
            rank.resize(plan.node_count());
            marked.assign(plan.node_count(), 0);
            std::ranges::for_each(std::views::iota(std::size_t{0}, plan.node_count()), [&](std::size_t i)
            {
                rank[plan.reverse_topological_order[i]] = static_cast<std::uint32_t>(i);
            });
        }

        [[nodiscard]] std::size_t capacity_bytes() const noexcept
        {
            return order.capacity() * sizeof(NodeHandle) + rank.capacity() * sizeof(std::uint32_t) + marked.capacity();
        }
    };

    // Inclusive union of the seeds and their ancestors, in the exact cached
    // reverse topological order. Repeated seeds and shared paths are visited once.
    // Preconditions: valid handles and workspace prepared for this tree. O(S + A
    // log A) time, O(N) retained scratch, no allocations after prepare. Returned
    // span expires at the next operation on the workspace. Empty seeds give {}.
    template<typename N, typename E>
    std::span<const NodeHandle> ancestor_closure_order(
        const Polytree<N, E>& tree, std::span<const NodeHandle> seeds, AncestorClosureWorkspace& workspace)
    {
        assert(workspace.rank.size() == node_count(tree));
        workspace.order.clear();
        std::ranges::for_each(seeds, [&](NodeHandle seed)
        {
            auto current = seed;
            const auto path = std::views::iota(std::size_t{0}) | std::views::take_while([&](std::size_t)
            {
                return current != INVALID_NODE && !workspace.marked[current];
            });
            std::ranges::for_each(path, [&](std::size_t)
            {
                workspace.marked[current] = 1;
                workspace.order.push_back(current);
                current = tree.parent[current];
            });
        });
        std::ranges::sort(workspace.order, [&](NodeHandle a, NodeHandle b) { return workspace.rank[a] < workspace.rank[b]; });
        std::ranges::for_each(workspace.order, [&](NodeHandle node) { workspace.marked[node] = 0; });
        return workspace.order;
    }
}
