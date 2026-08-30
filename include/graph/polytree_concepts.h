#pragma once

#include <concepts>
#include <cstddef>
#include <ranges>

namespace wz::core::graph
{
    template<typename Topology>
    concept PolytreeTopology = requires(
        const Topology& topology,
        typename Topology::node_handle_type node)
    {
        typename Topology::node_handle_type;
        typename Topology::node_data_type;
        typename Topology::edge_data_type;

        { node_count(topology) } -> std::convertible_to<std::size_t>;
        { edge_count(topology) } -> std::convertible_to<std::size_t>;
        { contains(topology, node) } -> std::convertible_to<bool>;
        { parent(topology, node) } ->
            std::same_as<typename Topology::node_handle_type>;
        { children(topology, node) } -> std::ranges::contiguous_range;
        { roots(topology) } -> std::ranges::contiguous_range;
        { node_data(topology, node) } ->
            std::same_as<const typename Topology::node_data_type&>;
    };
}
