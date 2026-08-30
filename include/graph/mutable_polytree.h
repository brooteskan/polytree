#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <graph/handles.h>

namespace wz::core::graph
{
    struct StableNodeId
    {
        std::uint64_t value{std::numeric_limits<std::uint64_t>::max()};

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return value != std::numeric_limits<std::uint64_t>::max();
        }

        friend constexpr auto operator<=>(
            const StableNodeId&,
            const StableNodeId&) = default;
    };

    inline constexpr StableNodeId INVALID_STABLE_NODE{};
    inline constexpr std::size_t APPEND_CHILD =
        std::numeric_limits<std::size_t>::max();

    enum class MutationError
    {
        none,
        invalid_node,
        invalid_parent,
        invalid_ordinal,
        cycle,
        id_exhausted,
        revision_exhausted,
    };

    template<typename T>
    class MutationResult
    {
    public:
        [[nodiscard]] static MutationResult success(T value)
        {
            return MutationResult(std::move(value), MutationError::none);
        }

        [[nodiscard]] static MutationResult failure(MutationError error)
        {
            return MutationResult(std::nullopt, error);
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return m_value.has_value();
        }

        [[nodiscard]] const T& value() const&
        {
            return m_value.value();
        }

        [[nodiscard]] T& value() &
        {
            return m_value.value();
        }

        [[nodiscard]] T&& value() &&
        {
            return std::move(m_value).value();
        }

        [[nodiscard]] MutationError error() const noexcept
        {
            return m_error;
        }

    private:
        MutationResult(std::optional<T> value, MutationError error)
            : m_value(std::move(value))
            , m_error(error)
        {
        }

        MutationResult(T value, MutationError error)
            : m_value(std::move(value))
            , m_error(error)
        {
        }

        std::optional<T> m_value;
        MutationError m_error{MutationError::none};
    };

    enum class MutableValidationError
    {
        none,
        node_count_overflow,
        invalid_root,
        root_has_parent,
        root_has_parent_edge,
        invalid_child,
        parent_mismatch,
        missing_parent_edge,
        duplicate_reachability,
        cycle_or_orphan,
    };

    struct MutableValidationResult
    {
        MutableValidationError error{MutableValidationError::none};
        StableNodeId node{INVALID_STABLE_NODE};

        [[nodiscard]] explicit constexpr operator bool() const noexcept
        {
            return error == MutableValidationError::none;
        }
    };

    template<typename NodeData, typename EdgeData>
    class MutablePolytree
    {
    public:
        using node_handle_type = StableNodeId;
        using node_data_type = NodeData;
        using edge_data_type = EdgeData;

        MutablePolytree() = default;
        MutablePolytree(const MutablePolytree&) = delete;
        MutablePolytree& operator=(const MutablePolytree&) = delete;
        MutablePolytree(MutablePolytree&&) noexcept = default;
        MutablePolytree& operator=(MutablePolytree&&) noexcept = default;

        [[nodiscard]] std::size_t node_count() const noexcept
        {
            return m_slot_by_id.size();
        }

        [[nodiscard]] std::size_t edge_count() const noexcept
        {
            return node_count() - m_roots.size();
        }

        [[nodiscard]] std::uint64_t revision() const noexcept
        {
            return m_revision;
        }

        [[nodiscard]] bool contains(StableNodeId node) const noexcept
        {
            return node.valid() && m_slot_by_id.contains(node.value);
        }

        [[nodiscard]] std::span<const StableNodeId> roots() const noexcept
        {
            return m_roots;
        }

        [[nodiscard]] std::span<const StableNodeId> children(
            StableNodeId node) const noexcept
        {
            const auto* record = find_record(node);
            assert(record != nullptr);
            return record->children;
        }

        [[nodiscard]] StableNodeId parent(StableNodeId node) const noexcept
        {
            const auto* record = find_record(node);
            assert(record != nullptr);
            return record->parent.value_or(INVALID_STABLE_NODE);
        }

        [[nodiscard]] const NodeData& node_data(StableNodeId node) const noexcept
        {
            const auto* record = find_record(node);
            assert(record != nullptr);
            return *record->data;
        }

        [[nodiscard]] const EdgeData& parent_edge_data(
            StableNodeId node) const noexcept
        {
            const auto* record = find_record(node);
            assert(record != nullptr && record->parent_edge != nullptr);
            return *record->parent_edge;
        }

        [[nodiscard]] MutationResult<StableNodeId> insert_root(
            NodeData data,
            std::size_t ordinal = APPEND_CHILD)
        {
            if (!revision_available())
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::revision_exhausted);
            }
            if (!identity_available())
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::id_exhausted);
            }

            const auto insertion = insertion_ordinal(m_roots.size(), ordinal);
            if (!insertion)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_ordinal);
            }

            const StableNodeId id{m_next_id};
            auto record = std::make_unique<NodeRecord>(
                id,
                std::make_unique<NodeData>(std::move(data)),
                std::nullopt,
                nullptr);
            m_roots.insert(m_roots.begin() + *insertion, id);
            try
            {
                commit_record(id, std::move(record));
            }
            catch (...)
            {
                m_roots.erase(m_roots.begin() + *insertion);
                throw;
            }

            ++m_next_id;
            ++m_revision;
            return MutationResult<StableNodeId>::success(id);
        }

        [[nodiscard]] MutationResult<StableNodeId> insert_child(
            StableNodeId parent_node,
            NodeData data,
            EdgeData edge_data,
            std::size_t ordinal = APPEND_CHILD)
        {
            if (!revision_available())
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::revision_exhausted);
            }

            auto* parent_record = find_record(parent_node);
            if (parent_record == nullptr)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_parent);
            }
            if (!identity_available())
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::id_exhausted);
            }

            const auto insertion = insertion_ordinal(
                parent_record->children.size(),
                ordinal);
            if (!insertion)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_ordinal);
            }

            const StableNodeId id{m_next_id};
            auto record = std::make_unique<NodeRecord>(
                id,
                std::make_unique<NodeData>(std::move(data)),
                parent_node,
                std::make_unique<EdgeData>(std::move(edge_data)));
            parent_record->children.insert(
                parent_record->children.begin() + *insertion,
                id);
            try
            {
                commit_record(id, std::move(record));
            }
            catch (...)
            {
                parent_record->children.erase(
                    parent_record->children.begin() + *insertion);
                throw;
            }

            ++m_next_id;
            ++m_revision;
            return MutationResult<StableNodeId>::success(id);
        }

        [[nodiscard]] MutationResult<StableNodeId> reparent(
            StableNodeId node,
            StableNodeId new_parent,
            EdgeData edge_data,
            std::size_t ordinal = APPEND_CHILD)
        {
            if (!revision_available())
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::revision_exhausted);
            }

            auto* record = find_record(node);
            if (record == nullptr)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_node);
            }
            auto* parent_record = find_record(new_parent);
            if (parent_record == nullptr)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_parent);
            }
            if (node == new_parent || would_create_cycle(node, new_parent))
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::cycle);
            }

            auto replacement_edge =
                std::make_unique<EdgeData>(std::move(edge_data));
            if (record->parent == new_parent)
            {
                const auto target = move_ordinal(
                    parent_record->children.size(),
                    ordinal);
                if (!target)
                {
                    return MutationResult<StableNodeId>::failure(
                        MutationError::invalid_ordinal);
                }

                const auto current = child_index(
                    parent_record->children,
                    node);
                move_within(parent_record->children, current, *target);
                record->parent_edge.swap(replacement_edge);
                ++m_revision;
                return MutationResult<StableNodeId>::success(node);
            }

            const auto insertion = insertion_ordinal(
                parent_record->children.size(),
                ordinal);
            if (!insertion)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_ordinal);
            }

            auto& source = sibling_sequence(*record);
            const auto current = child_index(source, node);
            parent_record->children.insert(
                parent_record->children.begin() + *insertion,
                node);
            source.erase(source.begin() + current);
            record->parent = new_parent;
            record->parent_edge.swap(replacement_edge);
            ++m_revision;
            return MutationResult<StableNodeId>::success(node);
        }

        [[nodiscard]] MutationResult<StableNodeId> detach_to_root(
            StableNodeId node,
            std::size_t ordinal = APPEND_CHILD)
        {
            if (!revision_available())
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::revision_exhausted);
            }

            auto* record = find_record(node);
            if (record == nullptr)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_node);
            }

            if (!record->parent)
            {
                const auto target = move_ordinal(m_roots.size(), ordinal);
                if (!target)
                {
                    return MutationResult<StableNodeId>::failure(
                        MutationError::invalid_ordinal);
                }
                const auto current = child_index(m_roots, node);
                move_within(m_roots, current, *target);
                ++m_revision;
                return MutationResult<StableNodeId>::success(node);
            }

            const auto insertion = insertion_ordinal(m_roots.size(), ordinal);
            if (!insertion)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_ordinal);
            }

            auto& source = sibling_sequence(*record);
            const auto current = child_index(source, node);
            m_roots.insert(m_roots.begin() + *insertion, node);
            source.erase(source.begin() + current);
            record->parent.reset();
            record->parent_edge.reset();
            ++m_revision;
            return MutationResult<StableNodeId>::success(node);
        }

        [[nodiscard]] MutationResult<std::size_t> erase_subtree(
            StableNodeId node)
        {
            if (!revision_available())
            {
                return MutationResult<std::size_t>::failure(
                    MutationError::revision_exhausted);
            }

            auto* record = find_record(node);
            if (record == nullptr)
            {
                return MutationResult<std::size_t>::failure(
                    MutationError::invalid_node);
            }

            std::vector<StableNodeId> removed{node};
            std::size_t cursor = 0;
            const auto pending = std::views::iota(std::size_t{0})
                | std::views::take_while(
                    [&removed, &cursor](std::size_t)
                    {
                        return cursor < removed.size();
                    });
            std::ranges::for_each(pending, [&](std::size_t)
            {
                const auto current = removed[cursor++];
                const auto* current_record = find_record(current);
                std::ranges::copy(
                    current_record->children,
                    std::back_inserter(removed));
            });

            m_free_slots.reserve(m_free_slots.size() + removed.size());
            auto& source = sibling_sequence(*record);
            source.erase(source.begin() + child_index(source, node));
            std::ranges::for_each(removed | std::views::reverse, [&](StableNodeId id)
            {
                const auto entry = m_slot_by_id.find(id.value);
                const auto slot = entry->second;
                m_slots[slot].reset();
                m_slot_by_id.erase(entry);
                m_free_slots.push_back(slot);
            });

            ++m_revision;
            return MutationResult<std::size_t>::success(removed.size());
        }

        [[nodiscard]] MutationResult<StableNodeId> replace_node_data(
            StableNodeId node,
            NodeData data)
        {
            if (!revision_available())
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::revision_exhausted);
            }

            auto* record = find_record(node);
            if (record == nullptr)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_node);
            }

            auto replacement = std::make_unique<NodeData>(std::move(data));
            record->data.swap(replacement);
            ++m_revision;
            return MutationResult<StableNodeId>::success(node);
        }

        [[nodiscard]] MutationResult<StableNodeId> replace_parent_edge_data(
            StableNodeId node,
            EdgeData edge_data)
        {
            if (!revision_available())
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::revision_exhausted);
            }

            auto* record = find_record(node);
            if (record == nullptr)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_node);
            }
            if (!record->parent)
            {
                return MutationResult<StableNodeId>::failure(
                    MutationError::invalid_parent);
            }

            auto replacement =
                std::make_unique<EdgeData>(std::move(edge_data));
            record->parent_edge.swap(replacement);
            ++m_revision;
            return MutationResult<StableNodeId>::success(node);
        }

        [[nodiscard]] MutableValidationResult validate() const
        {
            if (node_count() > static_cast<std::size_t>(INVALID_NODE))
            {
                return {
                    MutableValidationError::node_count_overflow,
                    INVALID_STABLE_NODE};
            }

            MutableValidationResult result{};
            std::vector<StableNodeId> pending_nodes;
            pending_nodes.reserve(node_count());
            std::ranges::for_each(m_roots, [&](StableNodeId root)
            {
                if (!result)
                {
                    return;
                }

                const auto* root_record = find_record(root);
                if (root_record == nullptr)
                {
                    result = {MutableValidationError::invalid_root, root};
                }
                else if (root_record->parent)
                {
                    result = {MutableValidationError::root_has_parent, root};
                }
                else if (root_record->parent_edge)
                {
                    result = {
                        MutableValidationError::root_has_parent_edge,
                        root};
                }
                else
                {
                    pending_nodes.push_back(root);
                }
            });
            if (!result)
            {
                return result;
            }

            std::unordered_set<std::uint64_t> visited;
            visited.reserve(node_count());
            std::size_t cursor = 0;
            const auto pending = std::views::iota(std::size_t{0})
                | std::views::take_while(
                    [&pending_nodes, &cursor, &result](std::size_t)
                    {
                        return result && cursor < pending_nodes.size();
                    });
            std::ranges::for_each(pending, [&](std::size_t)
            {
                const auto current = pending_nodes[cursor++];
                if (!visited.insert(current.value).second)
                {
                    result = {
                        MutableValidationError::duplicate_reachability,
                        current};
                    return;
                }

                const auto* current_record = find_record(current);
                std::ranges::for_each(
                    current_record->children,
                    [&](StableNodeId child)
                    {
                        if (!result)
                        {
                            return;
                        }

                        const auto* child_record = find_record(child);
                        if (child_record == nullptr)
                        {
                            result = {
                                MutableValidationError::invalid_child,
                                child};
                        }
                        else if (child_record->parent != current)
                        {
                            result = {
                                MutableValidationError::parent_mismatch,
                                child};
                        }
                        else if (!child_record->parent_edge)
                        {
                            result = {
                                MutableValidationError::missing_parent_edge,
                                child};
                        }
                        else
                        {
                            pending_nodes.push_back(child);
                        }
                    });
            });

            if (result && visited.size() != node_count())
            {
                return {
                    MutableValidationError::cycle_or_orphan,
                    INVALID_STABLE_NODE};
            }
            return result;
        }

    private:
        struct NodeRecord
        {
            NodeRecord(
                StableNodeId record_id,
                std::unique_ptr<NodeData> record_data,
                std::optional<StableNodeId> record_parent,
                std::unique_ptr<EdgeData> record_parent_edge)
                : id(record_id)
                , data(std::move(record_data))
                , parent(record_parent)
                , parent_edge(std::move(record_parent_edge))
            {
            }

            StableNodeId id;
            std::unique_ptr<NodeData> data;
            std::optional<StableNodeId> parent;
            std::unique_ptr<EdgeData> parent_edge;
            std::vector<StableNodeId> children;
        };

        [[nodiscard]] bool identity_available() const noexcept
        {
            return m_next_id != INVALID_STABLE_NODE.value;
        }

        [[nodiscard]] bool revision_available() const noexcept
        {
            return m_revision != std::numeric_limits<std::uint64_t>::max();
        }

        [[nodiscard]] static std::optional<std::size_t> insertion_ordinal(
            std::size_t size,
            std::size_t ordinal) noexcept
        {
            if (ordinal == APPEND_CHILD)
            {
                return size;
            }
            return ordinal <= size
                ? std::optional<std::size_t>{ordinal}
                : std::nullopt;
        }

        [[nodiscard]] static std::optional<std::size_t> move_ordinal(
            std::size_t size,
            std::size_t ordinal) noexcept
        {
            if (size == 0)
            {
                return std::nullopt;
            }
            if (ordinal == APPEND_CHILD)
            {
                return size - 1;
            }
            return ordinal < size
                ? std::optional<std::size_t>{ordinal}
                : std::nullopt;
        }

        [[nodiscard]] NodeRecord* find_record(StableNodeId node) noexcept
        {
            const auto entry = m_slot_by_id.find(node.value);
            return entry == m_slot_by_id.end()
                ? nullptr
                : m_slots[entry->second].get();
        }

        [[nodiscard]] const NodeRecord* find_record(
            StableNodeId node) const noexcept
        {
            const auto entry = m_slot_by_id.find(node.value);
            return entry == m_slot_by_id.end()
                ? nullptr
                : m_slots[entry->second].get();
        }

        void commit_record(
            StableNodeId id,
            std::unique_ptr<NodeRecord> record)
        {
            const auto slot = m_free_slots.empty()
                ? m_slots.size()
                : m_free_slots.back();
            const auto [entry, inserted] = m_slot_by_id.emplace(id.value, slot);
            if (!inserted)
            {
                throw std::logic_error("stable node identity collision");
            }

            try
            {
                if (m_free_slots.empty())
                {
                    m_slots.push_back(std::move(record));
                }
                else
                {
                    m_slots[slot] = std::move(record);
                    m_free_slots.pop_back();
                }
            }
            catch (...)
            {
                m_slot_by_id.erase(entry);
                throw;
            }
        }

        [[nodiscard]] std::vector<StableNodeId>& sibling_sequence(
            const NodeRecord& record)
        {
            return record.parent
                ? find_record(*record.parent)->children
                : m_roots;
        }

        [[nodiscard]] static std::size_t child_index(
            const std::vector<StableNodeId>& sequence,
            StableNodeId node) noexcept
        {
            const auto position = std::ranges::find(sequence, node);
            assert(position != sequence.end());
            return static_cast<std::size_t>(
                std::distance(sequence.begin(), position));
        }

        static void move_within(
            std::vector<StableNodeId>& sequence,
            std::size_t current,
            std::size_t target) noexcept
        {
            if (current < target)
            {
                std::rotate(
                    sequence.begin() + current,
                    sequence.begin() + current + 1,
                    sequence.begin() + target + 1);
            }
            else if (target < current)
            {
                std::rotate(
                    sequence.begin() + target,
                    sequence.begin() + current,
                    sequence.begin() + current + 1);
            }
        }

        [[nodiscard]] bool would_create_cycle(
            StableNodeId node,
            StableNodeId proposed_parent) const noexcept
        {
            bool cycle = false;
            StableNodeId current = proposed_parent;
            std::size_t inspected = 0;
            const auto ancestors = std::views::iota(std::size_t{0})
                | std::views::take_while(
                    [&](std::size_t)
                    {
                        return current.valid()
                            && !cycle
                            && inspected <= node_count();
                    });
            std::ranges::for_each(ancestors, [&](std::size_t)
            {
                cycle = current == node;
                const auto* record = find_record(current);
                current = cycle || record == nullptr
                    ? INVALID_STABLE_NODE
                    : record->parent.value_or(INVALID_STABLE_NODE);
                ++inspected;
            });
            return cycle || inspected > node_count();
        }

        std::vector<std::unique_ptr<NodeRecord>> m_slots;
        std::unordered_map<std::uint64_t, std::size_t> m_slot_by_id;
        std::vector<std::size_t> m_free_slots;
        std::vector<StableNodeId> m_roots;
        std::uint64_t m_next_id{};
        std::uint64_t m_revision{};
    };

    template<typename N, typename E>
    [[nodiscard]] std::size_t node_count(const MutablePolytree<N, E>& tree)
    {
        return tree.node_count();
    }

    template<typename N, typename E>
    [[nodiscard]] std::size_t edge_count(const MutablePolytree<N, E>& tree)
    {
        return tree.edge_count();
    }

    template<typename N, typename E>
    [[nodiscard]] bool contains(
        const MutablePolytree<N, E>& tree,
        StableNodeId node)
    {
        return tree.contains(node);
    }

    template<typename N, typename E>
    [[nodiscard]] std::span<const StableNodeId> roots(
        const MutablePolytree<N, E>& tree)
    {
        return tree.roots();
    }

    template<typename N, typename E>
    [[nodiscard]] std::span<const StableNodeId> children(
        const MutablePolytree<N, E>& tree,
        StableNodeId node)
    {
        return tree.children(node);
    }

    template<typename N, typename E>
    [[nodiscard]] StableNodeId parent(
        const MutablePolytree<N, E>& tree,
        StableNodeId node)
    {
        return tree.parent(node);
    }

    template<typename N, typename E>
    [[nodiscard]] const N& node_data(
        const MutablePolytree<N, E>& tree,
        StableNodeId node)
    {
        return tree.node_data(node);
    }

    template<typename N, typename E>
    [[nodiscard]] const E& parent_edge_data(
        const MutablePolytree<N, E>& tree,
        StableNodeId node)
    {
        return tree.parent_edge_data(node);
    }

    template<typename N, typename E>
    [[nodiscard]] bool is_root(
        const MutablePolytree<N, E>& tree,
        StableNodeId node)
    {
        return parent(tree, node) == INVALID_STABLE_NODE;
    }

    template<typename N, typename E>
    [[nodiscard]] bool is_leaf(
        const MutablePolytree<N, E>& tree,
        StableNodeId node)
    {
        return children(tree, node).empty();
    }

    template<typename N, typename E>
    [[nodiscard]] std::uint64_t revision(const MutablePolytree<N, E>& tree)
    {
        return tree.revision();
    }

    template<typename N, typename E>
    [[nodiscard]] MutableValidationResult validate(
        const MutablePolytree<N, E>& tree)
    {
        return tree.validate();
    }

    template<typename N, typename E>
    [[nodiscard]] MutationResult<StableNodeId> insert_root(
        MutablePolytree<N, E>& tree,
        N data,
        std::size_t ordinal = APPEND_CHILD)
    {
        return tree.insert_root(std::move(data), ordinal);
    }

    template<typename N, typename E>
    [[nodiscard]] MutationResult<StableNodeId> insert_child(
        MutablePolytree<N, E>& tree,
        StableNodeId parent_node,
        N data,
        E edge_data,
        std::size_t ordinal = APPEND_CHILD)
    {
        return tree.insert_child(
            parent_node,
            std::move(data),
            std::move(edge_data),
            ordinal);
    }

    template<typename N, typename E>
    [[nodiscard]] MutationResult<StableNodeId> reparent(
        MutablePolytree<N, E>& tree,
        StableNodeId node,
        StableNodeId new_parent,
        E edge_data,
        std::size_t ordinal = APPEND_CHILD)
    {
        return tree.reparent(
            node,
            new_parent,
            std::move(edge_data),
            ordinal);
    }

    template<typename N, typename E>
    [[nodiscard]] MutationResult<StableNodeId> detach_to_root(
        MutablePolytree<N, E>& tree,
        StableNodeId node,
        std::size_t ordinal = APPEND_CHILD)
    {
        return tree.detach_to_root(node, ordinal);
    }

    template<typename N, typename E>
    [[nodiscard]] MutationResult<std::size_t> erase_subtree(
        MutablePolytree<N, E>& tree,
        StableNodeId node)
    {
        return tree.erase_subtree(node);
    }

    template<typename N, typename E>
    [[nodiscard]] MutationResult<StableNodeId> replace_node_data(
        MutablePolytree<N, E>& tree,
        StableNodeId node,
        N data)
    {
        return tree.replace_node_data(node, std::move(data));
    }

    template<typename N, typename E>
    [[nodiscard]] MutationResult<StableNodeId> replace_parent_edge_data(
        MutablePolytree<N, E>& tree,
        StableNodeId node,
        E edge_data)
    {
        return tree.replace_parent_edge_data(node, std::move(edge_data));
    }
}
