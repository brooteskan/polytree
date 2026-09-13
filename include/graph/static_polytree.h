#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include <graph/handles.h>

namespace wz::core::graph
{
    struct PolytreeEvaluationPlan
    {
        std::span<const NodeHandle> topological_order;
        std::span<const NodeHandle> reverse_topological_order;
        std::span<const NodeHandle> roots;
        std::span<const NodeHandle> dependency_order;
        std::span<const std::uint32_t> dependency_level_offsets;

        [[nodiscard]] constexpr std::size_t node_count() const noexcept
        {
            return topological_order.size();
        }

        [[nodiscard]] constexpr std::size_t level_count() const noexcept
        {
            return dependency_level_offsets.empty()
                ? 0
                : dependency_level_offsets.size() - 1;
        }

        [[nodiscard]] constexpr std::span<const NodeHandle> dependency_level(
            std::size_t index) const noexcept
        {
            if (index >= level_count())
            {
                return {};
            }

            const auto first = dependency_level_offsets[index];
            const auto last = dependency_level_offsets[index + 1];
            return dependency_order.subspan(first, last - first);
        }
    };

    // A compact immutable forest. Every span points into PolytreeStorage::buffer.
    // Cached evaluation data is invalidated only when the owning storage is destroyed
    // or moved-from. Querying a valid node never allocates.
    template<typename NodeData, typename EdgeData>
    struct Polytree
    {
        using node_handle_type = NodeHandle;
        using node_data_type = NodeData;
        using edge_data_type = EdgeData;

        std::span<const NodeData> node_data;
        std::span<const std::uint32_t> out_offsets;
        std::span<const NodeHandle> out_neighbors;
        std::span<const EdgeData> out_edge_data;
        std::span<const NodeHandle> parent;
        std::span<const EdgeData> parent_edge_data;
        std::span<const NodeHandle> topo_order;
        std::span<const NodeHandle> reverse_topo_order;
        std::span<const NodeHandle> root_order;
        std::span<const NodeHandle> dependency_order;
        std::span<const std::uint32_t> dependency_level_offsets;
    };

    template<typename NodeData, typename EdgeData>
    struct PolytreeStorage
    {
        PolytreeStorage() = default;

        PolytreeStorage(
            std::unique_ptr<std::byte[]> owned_buffer,
            Polytree<NodeData, EdgeData> owned_polytree) noexcept
            : buffer(std::move(owned_buffer))
            , polytree(owned_polytree)
        {
        }

        PolytreeStorage(const PolytreeStorage&) = delete;
        PolytreeStorage& operator=(const PolytreeStorage&) = delete;

        PolytreeStorage(PolytreeStorage&& other) noexcept
            : buffer(std::move(other.buffer))
            , polytree(other.polytree)
        {
            other.polytree = {};
        }

        PolytreeStorage& operator=(PolytreeStorage&& other) noexcept
        {
            if (this != &other)
            {
                destroy_payloads();
                buffer = std::move(other.buffer);
                polytree = other.polytree;
                other.polytree = {};
            }
            return *this;
        }

        ~PolytreeStorage()
        {
            destroy_payloads();
        }

        std::unique_ptr<std::byte[]> buffer;
        Polytree<NodeData, EdgeData> polytree;

    private:
        void destroy_payloads() noexcept
        {
            if (!buffer)
            {
                return;
            }

            std::destroy_n(
                const_cast<NodeData*>(polytree.node_data.data()),
                polytree.node_data.size());
            std::destroy_n(
                const_cast<EdgeData*>(polytree.out_edge_data.data()),
                polytree.out_edge_data.size());
            std::destroy_n(
                const_cast<EdgeData*>(polytree.parent_edge_data.data()),
                polytree.parent_edge_data.size());
            polytree = {};
        }
    };

    template<typename NodeData, typename EdgeData>
    struct PolytreeBuilder
    {
        struct PendingEdge
        {
            NodeHandle from;
            NodeHandle to;
            EdgeData data;
        };

        std::vector<NodeData> nodes;
        std::vector<PendingEdge> edges;
        std::vector<NodeHandle> parent_of;
    };

    template<typename N, typename E>
    NodeHandle add_node(PolytreeBuilder<N, E>& builder, N data)
    {
        const auto handle = static_cast<NodeHandle>(builder.nodes.size());
        builder.nodes.push_back(std::move(data));
        builder.parent_of.push_back(INVALID_NODE);
        return handle;
    }

    template<typename N, typename E>
    bool add_edge(
        PolytreeBuilder<N, E>& builder,
        NodeHandle from,
        NodeHandle to,
        E data = {})
    {
        if (from >= builder.nodes.size() || to >= builder.nodes.size())
        {
            return false;
        }
        if (from == to || builder.parent_of[to] != INVALID_NODE)
        {
            return false;
        }

        builder.parent_of[to] = from;
        builder.edges.push_back({from, to, std::move(data)});
        return true;
    }

    template<typename N, typename E>
    std::uint32_t node_count(const Polytree<N, E>& tree)
    {
        return static_cast<std::uint32_t>(tree.node_data.size());
    }

    template<typename N, typename E>
    std::uint32_t edge_count(const Polytree<N, E>& tree)
    {
        return static_cast<std::uint32_t>(tree.out_neighbors.size());
    }

    template<typename N, typename E>
    bool contains(const Polytree<N, E>& tree, NodeHandle node)
    {
        return node < tree.node_data.size();
    }

    template<typename N, typename E>
    const N& node_data(const Polytree<N, E>& tree, NodeHandle node)
    {
        return tree.node_data[node];
    }

    template<typename N, typename E>
    std::span<const NodeHandle> children(const Polytree<N, E>& tree, NodeHandle node)
    {
        const auto first = tree.out_offsets[node];
        return tree.out_neighbors.subspan(first, tree.out_offsets[node + 1] - first);
    }

    template<typename N, typename E>
    NodeHandle parent(const Polytree<N, E>& tree, NodeHandle node)
    {
        return tree.parent[node];
    }

    template<typename N, typename E>
    const E& parent_edge_data(const Polytree<N, E>& tree, NodeHandle node)
    {
        return tree.parent_edge_data[node];
    }

    template<typename N, typename E>
    std::span<const E> outgoing_edge_data(const Polytree<N, E>& tree, NodeHandle node)
    {
        const auto first = tree.out_offsets[node];
        return tree.out_edge_data.subspan(first, tree.out_offsets[node + 1] - first);
    }

    template<typename N, typename E>
    bool is_root(const Polytree<N, E>& tree, NodeHandle node)
    {
        return tree.parent[node] == INVALID_NODE;
    }

    template<typename N, typename E>
    bool is_leaf(const Polytree<N, E>& tree, NodeHandle node)
    {
        return tree.out_offsets[node] == tree.out_offsets[node + 1];
    }

    template<typename N, typename E>
    bool has_edge(const Polytree<N, E>& tree, NodeHandle from, NodeHandle to)
    {
        const auto child_nodes = children(tree, from);
        return std::ranges::find(child_nodes, to) != child_nodes.end();
    }

    template<typename N, typename E>
    std::span<const NodeHandle> topo_order(const Polytree<N, E>& tree)
    {
        return tree.topo_order;
    }

    template<typename N, typename E>
    std::span<const NodeHandle> reverse_topo_order(const Polytree<N, E>& tree)
    {
        return tree.reverse_topo_order;
    }

    template<typename N, typename E>
    std::span<const NodeHandle> roots(const Polytree<N, E>& tree)
    {
        return tree.root_order;
    }

    template<typename N, typename E>
    std::span<const NodeHandle> dependency_order(const Polytree<N, E>& tree)
    {
        return tree.dependency_order;
    }

    template<typename N, typename E>
    std::span<const std::uint32_t> dependency_level_offsets(
        const Polytree<N, E>& tree)
    {
        return tree.dependency_level_offsets;
    }

    template<typename N, typename E>
    PolytreeEvaluationPlan evaluation_plan(const Polytree<N, E>& tree)
    {
        return {
            tree.topo_order,
            tree.reverse_topo_order,
            tree.root_order,
            tree.dependency_order,
            tree.dependency_level_offsets,
        };
    }

    template<typename N, typename E>
    std::span<const NodeHandle> dependency_level(
        const Polytree<N, E>& tree,
        std::size_t index)
    {
        return evaluation_plan(tree).dependency_level(index);
    }

    template<typename N, typename E>
    std::uint32_t child_count(const Polytree<N, E>& tree, NodeHandle node)
    {
        return tree.out_offsets[node + 1] - tree.out_offsets[node];
    }

    template<typename N, typename E>
    NodeHandle child_at(
        const Polytree<N, E>& tree,
        NodeHandle node,
        std::uint32_t index)
    {
        const auto child_nodes = children(tree, node);
        return index < child_nodes.size() ? child_nodes[index] : INVALID_NODE;
    }

    template<typename N, typename E>
    const E* child_edge_data_at(
        const Polytree<N, E>& tree,
        NodeHandle node,
        std::uint32_t index)
    {
        const auto edge_data = outgoing_edge_data(tree, node);
        return index < edge_data.size() ? &edge_data[index] : nullptr;
    }

    // Canonical descendant/ancestor traversal forms. Each function returns one
    // owning contiguous order. DFS and BFS allocate O(subtree size); ancestors
    // allocate O(depth). The orders remain valid independently of the tree view.
    template<typename N, typename E>
    std::vector<NodeHandle> depth_first_order(
        const Polytree<N, E>& tree,
        NodeHandle root)
    {
        std::vector<NodeHandle> order;
        std::vector<NodeHandle> stack{root};
        order.reserve(node_count(tree));

        const auto pending = std::views::iota(std::size_t{0})
            | std::views::take_while([&stack](std::size_t) { return !stack.empty(); });
        std::ranges::for_each(pending, [&](std::size_t)
        {
            const auto node = stack.back();
            stack.pop_back();
            order.push_back(node);
            std::ranges::copy(children(tree, node), std::back_inserter(stack));
        });
        return order;
    }

    template<typename N, typename E>
    std::vector<NodeHandle> breadth_first_order(
        const Polytree<N, E>& tree,
        NodeHandle root)
    {
        std::vector<NodeHandle> order{root};
        order.reserve(node_count(tree));

        const auto pending = std::views::iota(std::size_t{0})
            | std::views::take_while(
                [&order](std::size_t head) { return head < order.size(); });
        std::ranges::for_each(pending, [&](std::size_t head)
        {
            std::ranges::copy(
                children(tree, order[head]),
                std::back_inserter(order));
        });
        return order;
    }

    template<typename N, typename E>
    std::vector<NodeHandle> ancestor_order(
        const Polytree<N, E>& tree,
        NodeHandle node)
    {
        std::vector<NodeHandle> order;
        auto current = parent(tree, node);
        const auto pending = std::views::iota(std::size_t{0})
            | std::views::take_while(
                [&current](std::size_t) { return current != INVALID_NODE; });
        std::ranges::for_each(pending, [&](std::size_t)
        {
            order.push_back(current);
            current = parent(tree, current);
        });
        return order;
    }

    // Compatibility visitor adapters consume the canonical contiguous orders.
    template<typename N, typename E, typename Visitor>
        requires std::invocable<Visitor&, NodeHandle>
    void dfs(const Polytree<N, E>& tree, NodeHandle root, Visitor&& visitor)
    {
        const auto order = depth_first_order(tree, root);
        std::ranges::for_each(order, [&](NodeHandle node)
        {
            std::invoke(visitor, node);
        });
    }

    template<typename N, typename E, typename Visitor>
        requires std::invocable<Visitor&, NodeHandle>
    void bfs(const Polytree<N, E>& tree, NodeHandle root, Visitor&& visitor)
    {
        const auto order = breadth_first_order(tree, root);
        std::ranges::for_each(order, [&](NodeHandle node)
        {
            std::invoke(visitor, node);
        });
    }

    template<typename N, typename E, typename Visitor>
        requires std::invocable<Visitor&, NodeHandle>
    void walk_ancestors(
        const Polytree<N, E>& tree,
        NodeHandle node,
        Visitor&& visitor)
    {
        const auto order = ancestor_order(tree, node);
        std::ranges::for_each(order, [&](NodeHandle ancestor)
        {
            std::invoke(visitor, ancestor);
        });
    }

    namespace detail
    {
        template<typename T>
        std::byte* polytree_carve(
            std::byte* pointer,
            std::byte* end,
            std::size_t count,
            std::span<T>& output)
        {
            constexpr auto alignment = alignof(T);
            auto address = reinterpret_cast<std::uintptr_t>(pointer);
            address = (address + alignment - 1) & ~(alignment - 1);
            pointer = reinterpret_cast<std::byte*>(address);
            assert(pointer + count * sizeof(T) <= end);
            output = {reinterpret_cast<T*>(pointer), count};
            return pointer + count * sizeof(T);
        }

        template<typename N, typename E>
        std::vector<NodeHandle> polytree_kahn_topo(
            std::uint32_t node_total,
            const std::vector<typename PolytreeBuilder<N, E>::PendingEdge>& edges)
        {
            // Contiguous construction adjacency avoids one allocation per
            // non-leaf node while retaining exact edge/sibling insertion order.
            std::vector<std::uint32_t> offsets(node_total + 1, 0);
            std::vector<std::uint32_t> in_degree(node_total, 0);
            std::ranges::for_each(edges, [&](const auto& edge)
            {
                ++offsets[edge.from + 1];
                ++in_degree[edge.to];
            });
            std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
            auto cursor = offsets;
            std::vector<NodeHandle> adjacency(edges.size());
            std::ranges::for_each(edges, [&](const auto& edge)
            {
                adjacency[cursor[edge.from]++] = edge.to;
            });

            std::vector<NodeHandle> queue;
            std::vector<NodeHandle> order;
            queue.reserve(node_total);
            order.reserve(node_total);
            auto root_candidates = std::views::iota(NodeHandle{0}, node_total)
                | std::views::filter(
                    [&in_degree](NodeHandle node) { return in_degree[node] == 0; });
            std::ranges::copy(root_candidates, std::back_inserter(queue));

            const auto pending = std::views::iota(std::size_t{0})
                | std::views::take_while(
                    [&queue](std::size_t) { return !queue.empty(); });
            std::ranges::for_each(pending, [&](std::size_t)
            {
                const auto node = queue.back();
                queue.pop_back();
                order.push_back(node);
                std::ranges::for_each(std::span<const NodeHandle>(adjacency).subspan(
                    offsets[node], offsets[node + 1] - offsets[node]), [&](NodeHandle child)
                {
                    if (--in_degree[child] == 0)
                    {
                        queue.push_back(child);
                    }
                });
            });
            return order.size() == node_total ? order : std::vector<NodeHandle>{};
        }

        struct plan_storage
        {
            std::vector<NodeHandle> topological;
            std::vector<NodeHandle> reverse_topological;
            std::vector<NodeHandle> roots;
            std::vector<NodeHandle> dependency;
            std::vector<std::uint32_t> level_offsets;
        };

        inline plan_storage make_plan(
            std::vector<NodeHandle> topological,
            std::span<const NodeHandle> parents)
        {
            plan_storage plan;
            plan.topological = std::move(topological);
            plan.reverse_topological = plan.topological;
            std::ranges::reverse(plan.reverse_topological);

            const auto candidates = std::views::iota(
                NodeHandle{0},
                static_cast<NodeHandle>(parents.size()));
            auto root_nodes = candidates
                | std::views::filter(
                    [parents](NodeHandle node) { return parents[node] == INVALID_NODE; });
            std::ranges::copy(root_nodes, std::back_inserter(plan.roots));

            if (parents.empty())
            {
                plan.level_offsets.push_back(0);
                return plan;
            }

            std::vector<std::uint32_t> levels(parents.size(), 0);
            std::ranges::for_each(plan.topological, [&](NodeHandle node)
            {
                const auto parent_node = parents[node];
                levels[node] = parent_node == INVALID_NODE ? 0 : levels[parent_node] + 1;
            });

            const auto level_count = *std::ranges::max_element(levels) + 1;
            plan.level_offsets.assign(level_count + 1, 0);
            std::ranges::for_each(levels, [&](std::uint32_t level)
            {
                ++plan.level_offsets[level + 1];
            });
            std::partial_sum(
                plan.level_offsets.begin(),
                plan.level_offsets.end(),
                plan.level_offsets.begin());

            plan.dependency.resize(parents.size());
            auto cursor = plan.level_offsets;
            std::ranges::for_each(plan.topological, [&](NodeHandle node)
            {
                plan.dependency[cursor[levels[node]]++] = node;
            });
            return plan;
        }
    }

    // Consumes the builder. Temporary construction storage is O(nodes + edges).
    // The resulting cached plan is immutable and all of its ranges are contiguous.
    template<typename N, typename E>
    std::optional<PolytreeStorage<N, E>> build(PolytreeBuilder<N, E> builder)
    {
        const auto node_total = static_cast<std::uint32_t>(builder.nodes.size());
        const auto edge_total = static_cast<std::uint32_t>(builder.edges.size());
        auto topological = detail::polytree_kahn_topo<N, E>(node_total, builder.edges);
        if (topological.empty() && node_total > 0)
        {
            return std::nullopt;
        }

        auto plan = detail::make_plan(std::move(topological), builder.parent_of);
        const auto by_parent = [](const auto& left, const auto& right) { return left.from < right.from; };
        if (!std::ranges::is_sorted(builder.edges, by_parent))
        {
            std::stable_sort(builder.edges.begin(), builder.edges.end(), by_parent);
        }

        const std::size_t buffer_size =
            sizeof(N) * node_total + alignof(N)
            + sizeof(std::uint32_t) * (node_total + 1) + alignof(std::uint32_t)
            + sizeof(NodeHandle) * edge_total + alignof(NodeHandle)
            + sizeof(E) * edge_total + alignof(E)
            + sizeof(NodeHandle) * node_total + alignof(NodeHandle)
            + sizeof(E) * node_total + alignof(E)
            + sizeof(NodeHandle) * node_total + alignof(NodeHandle)
            + sizeof(NodeHandle) * node_total + alignof(NodeHandle)
            + sizeof(NodeHandle) * plan.roots.size() + alignof(NodeHandle)
            + sizeof(NodeHandle) * node_total + alignof(NodeHandle)
            + sizeof(std::uint32_t) * plan.level_offsets.size() + alignof(std::uint32_t);

        auto buffer = std::make_unique<std::byte[]>(buffer_size);
        auto* pointer = buffer.get();
        auto* const end = pointer + buffer_size;
        std::span<N> node_data_output;
        std::span<std::uint32_t> out_offsets_output;
        std::span<NodeHandle> out_neighbors_output;
        std::span<E> out_edge_data_output;
        std::span<NodeHandle> parent_output;
        std::span<E> parent_edge_data_output;
        std::span<NodeHandle> topological_output;
        std::span<NodeHandle> reverse_topological_output;
        std::span<NodeHandle> roots_output;
        std::span<NodeHandle> dependency_output;
        std::span<std::uint32_t> level_offsets_output;

        pointer = detail::polytree_carve(
            pointer, end, node_total, node_data_output);
        pointer = detail::polytree_carve(
            pointer, end, node_total + 1, out_offsets_output);
        pointer = detail::polytree_carve(
            pointer, end, edge_total, out_neighbors_output);
        pointer = detail::polytree_carve(
            pointer, end, edge_total, out_edge_data_output);
        pointer = detail::polytree_carve(
            pointer, end, node_total, parent_output);
        pointer = detail::polytree_carve(
            pointer, end, node_total, parent_edge_data_output);
        pointer = detail::polytree_carve(
            pointer, end, node_total, topological_output);
        pointer = detail::polytree_carve(
            pointer, end, node_total, reverse_topological_output);
        pointer = detail::polytree_carve(
            pointer, end, plan.roots.size(), roots_output);
        pointer = detail::polytree_carve(
            pointer, end, node_total, dependency_output);
        detail::polytree_carve(
            pointer, end, plan.level_offsets.size(), level_offsets_output);

        std::size_t constructed_nodes = 0;
        std::size_t constructed_out_edges = 0;
        std::size_t constructed_parent_edges = 0;
        try
        {
            const auto node_indices = std::views::iota(NodeHandle{0}, node_total);
            std::ranges::for_each(node_indices, [&](NodeHandle index)
            {
                std::construct_at(
                    &node_data_output[index],
                    std::move(builder.nodes[index]));
                ++constructed_nodes;
            });

            std::ranges::fill(out_offsets_output, 0u);
            std::ranges::for_each(builder.edges, [&](const auto& edge)
            {
                ++out_offsets_output[edge.from + 1];
            });
            std::partial_sum(
                out_offsets_output.begin(),
                out_offsets_output.end(),
                out_offsets_output.begin());

            std::vector<std::uint32_t> cursor(
                out_offsets_output.begin(),
                out_offsets_output.end());
            std::ranges::for_each(builder.edges, [&](const auto& edge)
            {
                const auto position = cursor[edge.from]++;
                out_neighbors_output[position] = edge.to;
                std::construct_at(&out_edge_data_output[position], edge.data);
                ++constructed_out_edges;
            });

            std::ranges::copy(builder.parent_of, parent_output.begin());
            std::uninitialized_value_construct_n(
                parent_edge_data_output.data(),
                node_total);
            constructed_parent_edges = node_total;
            std::ranges::for_each(builder.edges, [&](const auto& edge)
            {
                parent_edge_data_output[edge.to] = edge.data;
            });
        }
        catch (...)
        {
            std::destroy_n(node_data_output.data(), constructed_nodes);
            std::destroy_n(out_edge_data_output.data(), constructed_out_edges);
            std::destroy_n(
                parent_edge_data_output.data(),
                constructed_parent_edges);
            throw;
        }
        std::ranges::copy(plan.topological, topological_output.begin());
        std::ranges::copy(
            plan.reverse_topological,
            reverse_topological_output.begin());
        std::ranges::copy(plan.roots, roots_output.begin());
        std::ranges::copy(plan.dependency, dependency_output.begin());
        std::ranges::copy(plan.level_offsets, level_offsets_output.begin());

        Polytree<N, E> tree{
            .node_data = node_data_output,
            .out_offsets = out_offsets_output,
            .out_neighbors = out_neighbors_output,
            .out_edge_data = out_edge_data_output,
            .parent = parent_output,
            .parent_edge_data = parent_edge_data_output,
            .topo_order = topological_output,
            .reverse_topo_order = reverse_topological_output,
            .root_order = roots_output,
            .dependency_order = dependency_output,
            .dependency_level_offsets = level_offsets_output,
        };
        return PolytreeStorage<N, E>{std::move(buffer), tree};
    }
}
