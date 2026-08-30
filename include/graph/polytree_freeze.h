#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <graph/mutable_polytree.h>
#include <graph/static_polytree.h>

namespace wz::core::graph
{
    struct IdentityMappingEntry
    {
        StableNodeId authoring;
        NodeHandle runtime{INVALID_NODE};

        friend constexpr bool operator==(
            const IdentityMappingEntry&,
            const IdentityMappingEntry&) = default;
    };

    class FrozenIdentityMap
    {
    public:
        FrozenIdentityMap() = default;

        FrozenIdentityMap(
            std::vector<StableNodeId> runtime_to_authoring,
            std::vector<IdentityMappingEntry> authoring_to_runtime,
            std::uint64_t source_revision)
            : m_runtime_to_authoring(std::move(runtime_to_authoring))
            , m_authoring_to_runtime(std::move(authoring_to_runtime))
            , m_source_revision(source_revision)
        {
        }

        [[nodiscard]] std::span<const StableNodeId> runtime_to_authoring()
            const noexcept
        {
            return m_runtime_to_authoring;
        }

        [[nodiscard]] std::span<const IdentityMappingEntry>
        authoring_to_runtime() const noexcept
        {
            return m_authoring_to_runtime;
        }

        [[nodiscard]] std::optional<NodeHandle> runtime_handle(
            StableNodeId authoring) const noexcept
        {
            const auto position = std::ranges::lower_bound(
                m_authoring_to_runtime,
                authoring,
                {},
                &IdentityMappingEntry::authoring);
            if (position == m_authoring_to_runtime.end()
                || position->authoring != authoring)
            {
                return std::nullopt;
            }
            return position->runtime;
        }

        [[nodiscard]] std::optional<StableNodeId> authoring_id(
            NodeHandle runtime) const noexcept
        {
            if (runtime >= m_runtime_to_authoring.size())
            {
                return std::nullopt;
            }
            return m_runtime_to_authoring[runtime];
        }

        [[nodiscard]] std::uint64_t source_revision() const noexcept
        {
            return m_source_revision;
        }

        [[nodiscard]] std::size_t storage_bytes() const noexcept
        {
            return m_runtime_to_authoring.capacity() * sizeof(StableNodeId)
                + m_authoring_to_runtime.capacity()
                    * sizeof(IdentityMappingEntry);
        }

    private:
        std::vector<StableNodeId> m_runtime_to_authoring;
        std::vector<IdentityMappingEntry> m_authoring_to_runtime;
        std::uint64_t m_source_revision{};
    };

    struct FreezeMetrics
    {
        std::size_t reusable_scratch_capacity_bytes{};
        std::size_t builder_capacity_bytes{};
        std::size_t identity_mapping_capacity_bytes{};
    };

    enum class FreezeError
    {
        none,
        invalid_topology,
        node_count_overflow,
        static_build_failed,
    };

    template<typename NodeData, typename EdgeData>
    struct FrozenPolytree
    {
        PolytreeStorage<NodeData, EdgeData> topology;
        FrozenIdentityMap identities;
        FreezeMetrics metrics;
    };

    template<typename NodeData, typename EdgeData>
    class FreezeOutcome
    {
    public:
        using value_type = FrozenPolytree<NodeData, EdgeData>;

        [[nodiscard]] static FreezeOutcome success(value_type value)
        {
            return FreezeOutcome(std::move(value));
        }

        [[nodiscard]] static FreezeOutcome failure(
            FreezeError error,
            MutableValidationResult validation = {})
        {
            return FreezeOutcome(error, validation);
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return m_value.has_value();
        }

        [[nodiscard]] value_type& value() &
        {
            return m_value.value();
        }

        [[nodiscard]] const value_type& value() const&
        {
            return m_value.value();
        }

        [[nodiscard]] value_type&& value() &&
        {
            return std::move(m_value).value();
        }

        [[nodiscard]] value_type* operator->() noexcept
        {
            return &m_value.value();
        }

        [[nodiscard]] const value_type* operator->() const noexcept
        {
            return &m_value.value();
        }

        [[nodiscard]] FreezeError error() const noexcept
        {
            return m_error;
        }

        [[nodiscard]] MutableValidationResult validation() const noexcept
        {
            return m_validation;
        }

    private:
        explicit FreezeOutcome(value_type value)
            : m_value(std::move(value))
        {
        }

        FreezeOutcome(FreezeError error, MutableValidationResult validation)
            : m_error(error)
            , m_validation(validation)
        {
        }

        std::optional<value_type> m_value;
        FreezeError m_error{FreezeError::none};
        MutableValidationResult m_validation{};
    };

    struct FreezeWorkspace
    {
        void prepare(std::size_t node_total)
        {
            order.clear();
            stack.clear();
            dense_by_id.clear();
            order.reserve(node_total);
            stack.reserve(node_total);
            dense_by_id.reserve(node_total);
        }

        [[nodiscard]] std::size_t scratch_capacity_bytes() const noexcept
        {
            return (order.capacity() + stack.capacity()) * sizeof(StableNodeId)
                + dense_by_id.size()
                    * sizeof(std::unordered_map<std::uint64_t, NodeHandle>::value_type)
                + dense_by_id.bucket_count() * sizeof(void*);
        }

        std::vector<StableNodeId> order;
        std::vector<StableNodeId> stack;
        std::unordered_map<std::uint64_t, NodeHandle> dense_by_id;
    };

    template<typename N, typename E>
        requires std::copy_constructible<N>
            && std::copyable<E>
            && std::default_initializable<E>
    [[nodiscard]] FreezeOutcome<N, E> freeze(
        const MutablePolytree<N, E>& source,
        FreezeWorkspace& workspace)
    {
        const auto validation = validate(source);
        if (!validation)
        {
            const auto error = validation.error
                    == MutableValidationError::node_count_overflow
                ? FreezeError::node_count_overflow
                : FreezeError::invalid_topology;
            return FreezeOutcome<N, E>::failure(error, validation);
        }

        const auto node_total = node_count(source);
        workspace.prepare(node_total);
        std::ranges::copy(
            roots(source) | std::views::reverse,
            std::back_inserter(workspace.stack));

        const auto pending = std::views::iota(std::size_t{0})
            | std::views::take_while(
                [&workspace](std::size_t)
                {
                    return !workspace.stack.empty();
                });
        std::ranges::for_each(pending, [&](std::size_t)
        {
            const auto current = workspace.stack.back();
            workspace.stack.pop_back();
            workspace.order.push_back(current);
            std::ranges::copy(
                children(source, current) | std::views::reverse,
                std::back_inserter(workspace.stack));
        });

        std::vector<StableNodeId> runtime_to_authoring = workspace.order;
        std::vector<IdentityMappingEntry> authoring_to_runtime;
        authoring_to_runtime.reserve(node_total);
        const auto indices = std::views::iota(
            std::size_t{0},
            workspace.order.size());
        std::ranges::for_each(indices, [&](std::size_t index)
        {
            const auto stable = workspace.order[index];
            const auto dense = static_cast<NodeHandle>(index);
            workspace.dense_by_id.emplace(stable.value, dense);
            authoring_to_runtime.push_back({stable, dense});
        });
        std::ranges::sort(
            authoring_to_runtime,
            {},
            &IdentityMappingEntry::authoring);

        PolytreeBuilder<N, E> builder;
        builder.nodes.reserve(node_total);
        builder.parent_of.reserve(node_total);
        builder.edges.reserve(edge_count(source));
        std::ranges::for_each(workspace.order, [&](StableNodeId node)
        {
            add_node(builder, node_data(source, node));
        });

        bool edges_valid = true;
        std::ranges::for_each(indices, [&](std::size_t index)
        {
            const auto authoring_parent = workspace.order[index];
            const auto runtime_parent = static_cast<NodeHandle>(index);
            std::ranges::for_each(
                children(source, authoring_parent),
                [&](StableNodeId authoring_child)
                {
                    const auto runtime_child =
                        workspace.dense_by_id.at(authoring_child.value);
                    edges_valid = add_edge(
                        builder,
                        runtime_parent,
                        runtime_child,
                        parent_edge_data(source, authoring_child))
                        && edges_valid;
                });
        });
        if (!edges_valid)
        {
            return FreezeOutcome<N, E>::failure(
                FreezeError::static_build_failed);
        }

        const auto builder_capacity_bytes =
            builder.nodes.capacity() * sizeof(N)
            + builder.parent_of.capacity() * sizeof(NodeHandle)
            + builder.edges.capacity()
                * sizeof(typename PolytreeBuilder<N, E>::PendingEdge);
        auto static_topology = build(std::move(builder));
        if (!static_topology)
        {
            return FreezeOutcome<N, E>::failure(
                FreezeError::static_build_failed);
        }

        FrozenIdentityMap identities{
            std::move(runtime_to_authoring),
            std::move(authoring_to_runtime),
            revision(source)};
        const FreezeMetrics metrics{
            workspace.scratch_capacity_bytes(),
            builder_capacity_bytes,
            identities.storage_bytes(),
        };
        return FreezeOutcome<N, E>::success({
            std::move(*static_topology),
            std::move(identities),
            metrics,
        });
    }

    template<typename N, typename E>
        requires std::copy_constructible<N>
            && std::copyable<E>
            && std::default_initializable<E>
    [[nodiscard]] FreezeOutcome<N, E> freeze(
        const MutablePolytree<N, E>& source)
    {
        FreezeWorkspace workspace;
        return freeze(source, workspace);
    }
}
