#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <span>

#include <algo/next.h>
#include <graph/concepts.h>
#include <graph/static_polytree.h>

namespace wz::core::graph
{
    namespace detail
    {
        template<typename S>
            requires Sink<S, NodeHandle>
        algo::next::execution_status consume_order(
            std::span<const NodeHandle> order,
            S& sink)
        {
            return algo::next::transform(
                order,
                sink,
                [](NodeHandle node) { return node; });
        }
    }

    // Sequential sink adapters retain early termination while sharing the
    // canonical contiguous traversal orders with visitor and range consumers.
    template<typename N, typename E, typename S>
        requires Sink<S, NodeHandle>
    algo::next::execution_status dfs(
        const Polytree<N, E>& tree,
        NodeHandle root,
        S& sink)
    {
        const auto order = depth_first_order(tree, root);
        return detail::consume_order(std::span<const NodeHandle>{order}, sink);
    }

    template<typename N, typename E, typename S>
        requires Sink<S, NodeHandle>
    algo::next::execution_status bfs(
        const Polytree<N, E>& tree,
        NodeHandle root,
        S& sink)
    {
        const auto order = breadth_first_order(tree, root);
        return detail::consume_order(std::span<const NodeHandle>{order}, sink);
    }

    template<typename N, typename E, typename S>
        requires Sink<S, NodeHandle>
    algo::next::execution_status walk_ancestors(
        const Polytree<N, E>& tree,
        NodeHandle node,
        S& sink)
    {
        const auto order = ancestor_order(tree, node);
        return detail::consume_order(std::span<const NodeHandle>{order}, sink);
    }

    struct PolytreeMaterialization
    {
        std::span<NodeHandle> values;
        algo::next::execution_status status = algo::next::execution_status::completed;

        [[nodiscard]] constexpr auto begin() noexcept { return values.begin(); }
        [[nodiscard]] constexpr auto end() noexcept { return values.end(); }
        [[nodiscard]] constexpr auto begin() const noexcept { return values.begin(); }
        [[nodiscard]] constexpr auto end() const noexcept { return values.end(); }
        [[nodiscard]] constexpr bool empty() const noexcept { return values.empty(); }
        [[nodiscard]] constexpr std::size_t size() const noexcept { return values.size(); }
        [[nodiscard]] constexpr NodeHandle& operator[](std::size_t index) noexcept
        {
            return values[index];
        }

        [[nodiscard]] constexpr const NodeHandle& operator[](
            std::size_t index) const noexcept
        {
            return values[index];
        }

        [[nodiscard]] constexpr bool was_truncated() const noexcept
        {
            return algo::next::was_truncated(status);
        }

        constexpr operator std::span<NodeHandle>() const noexcept
        {
            return values;
        }
    };

    namespace detail
    {
        struct PolytreeSpanSink
        {
            std::span<NodeHandle> buffer;
            std::size_t count = 0;

            bool push(NodeHandle node)
            {
                if (count >= buffer.size())
                {
                    return false;
                }
                buffer[count++] = node;
                return true;
            }

            [[nodiscard]] std::span<NodeHandle> result() const
            {
                return buffer.first(count);
            }
        };

        inline PolytreeMaterialization materialize(
            std::span<const NodeHandle> order,
            std::span<NodeHandle> scratch)
        {
            PolytreeSpanSink sink{scratch};
            const auto status = consume_order(order, sink);
            return {sink.result(), status};
        }
    }

    template<typename N, typename E>
    [[nodiscard]] PolytreeMaterialization dfs_materialize(
        const Polytree<N, E>& tree,
        NodeHandle root,
        std::span<NodeHandle> scratch)
    {
        const auto order = depth_first_order(tree, root);
        return detail::materialize(order, scratch);
    }

    template<typename N, typename E>
    [[nodiscard]] PolytreeMaterialization bfs_materialize(
        const Polytree<N, E>& tree,
        NodeHandle root,
        std::span<NodeHandle> scratch)
    {
        const auto order = breadth_first_order(tree, root);
        return detail::materialize(order, scratch);
    }

    template<typename N, typename E>
    [[nodiscard]] PolytreeMaterialization ancestors_materialize(
        const Polytree<N, E>& tree,
        NodeHandle node,
        std::span<NodeHandle> scratch)
    {
        const auto order = ancestor_order(tree, node);
        return detail::materialize(order, scratch);
    }

    template<typename N, typename E>
    std::uint32_t child_ordinal(const Polytree<N, E>& tree, NodeHandle node)
    {
        const auto parent_node = parent(tree, node);
        if (parent_node == INVALID_NODE)
        {
            return UINT32_MAX;
        }

        const auto siblings = children(tree, parent_node);
        const auto found = std::ranges::find(siblings, node);
        return found == siblings.end()
            ? UINT32_MAX
            : static_cast<std::uint32_t>(std::ranges::distance(siblings.begin(), found));
    }

    template<typename N, typename E>
    NodeHandle previous_sibling(const Polytree<N, E>& tree, NodeHandle node)
    {
        const auto ordinal = child_ordinal(tree, node);
        return ordinal == UINT32_MAX || ordinal == 0
            ? INVALID_NODE
            : child_at(tree, parent(tree, node), ordinal - 1);
    }

    template<typename N, typename E>
    NodeHandle next_sibling(const Polytree<N, E>& tree, NodeHandle node)
    {
        const auto ordinal = child_ordinal(tree, node);
        return ordinal == UINT32_MAX
            ? INVALID_NODE
            : child_at(tree, parent(tree, node), ordinal + 1);
    }

    template<typename N, typename E>
    std::uint32_t depth(const Polytree<N, E>& tree, NodeHandle node)
    {
        return static_cast<std::uint32_t>(ancestor_order(tree, node).size());
    }

    template<typename N, typename E>
    [[nodiscard]] PolytreeMaterialization roots_materialize(
        const Polytree<N, E>& tree,
        std::span<NodeHandle> scratch)
    {
        return detail::materialize(roots(tree), scratch);
    }

    template<typename N, typename E>
    [[nodiscard]] PolytreeMaterialization ancestors_materialize_root_first(
        const Polytree<N, E>& tree,
        NodeHandle node,
        std::span<NodeHandle> scratch)
    {
        const auto order = ancestor_order(tree, node);
        auto result = detail::materialize(order, scratch);
        std::ranges::reverse(result.values);
        return result;
    }

    template<typename N, typename E>
    std::uint32_t subtree_size(const Polytree<N, E>& tree, NodeHandle root)
    {
        return static_cast<std::uint32_t>(depth_first_order(tree, root).size());
    }

    template<typename N, typename E, typename Predicate>
    NodeHandle find_child_if(
        const Polytree<N, E>& tree,
        NodeHandle node,
        Predicate&& predicate)
    {
        const auto child_nodes = children(tree, node);
        const auto edge_data = outgoing_edge_data(tree, node);
        const auto ordinals = std::views::iota(
            std::uint32_t{0},
            static_cast<std::uint32_t>(child_nodes.size()));
        const auto found = std::ranges::find_if(ordinals, [&](std::uint32_t ordinal)
        {
            return std::invoke(
                predicate,
                child_nodes[ordinal],
                edge_data[ordinal],
                ordinal);
        });
        return found == ordinals.end() ? INVALID_NODE : child_nodes[*found];
    }

    template<typename N, typename E, typename Visitor>
    bool walk_path_from_root(
        const Polytree<N, E>& tree,
        NodeHandle node,
        std::span<NodeHandle> scratch,
        Visitor&& visitor)
    {
        if (node == INVALID_NODE)
        {
            return false;
        }

        const auto ancestors = ancestor_order(tree, node);
        const auto required = ancestors.size() + 1;
        if (required > scratch.size())
        {
            return false;
        }

        std::ranges::reverse_copy(ancestors, scratch.begin());
        scratch[ancestors.size()] = node;
        const auto path = scratch.first(required);
        const auto edge_indices = std::views::iota(std::size_t{0}, path.size() - 1);
        std::ranges::for_each(edge_indices, [&](std::size_t index)
        {
            const auto parent_node = path[index];
            const auto child_node = path[index + 1];
            const auto child_nodes = children(tree, parent_node);
            const auto found = std::ranges::find(child_nodes, child_node);
            if (found != child_nodes.end())
            {
                const auto ordinal = static_cast<std::uint32_t>(
                    std::ranges::distance(child_nodes.begin(), found));
                std::invoke(
                    visitor,
                    parent_node,
                    child_node,
                    outgoing_edge_data(tree, parent_node)[ordinal],
                    ordinal);
            }
        });
        return true;
    }

    template<typename Pipeline, typename Out>
    struct PolytreePipelineSink
    {
        const Pipeline& pipeline;
        Out& output;

        bool push(NodeHandle node)
        {
            const std::span<const NodeHandle> input{&node, 1};
            return !algo::next::was_truncated(pipeline(input, output));
        }
    };

    template<typename Pipeline, typename Out>
    PolytreePipelineSink<Pipeline, Out> as_sink(
        const Pipeline& pipeline,
        Out& output)
    {
        return {pipeline, output};
    }
}
