#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <string_view>

#include <graph/mutable_polytree.h>
#include <graph/polytree_freeze.h>

using namespace wz::core::graph;

namespace
{
    using Clock = std::chrono::steady_clock;
    using Tree = MutablePolytree<std::uint32_t, std::uint32_t>;

    Tree make_chain(std::uint32_t node_total)
    {
        Tree tree;
        auto current = insert_root(tree, 0u).value();
        std::ranges::for_each(
            std::views::iota(1u, node_total),
            [&](std::uint32_t value)
            {
                current = insert_child(tree, current, value, value).value();
            });
        return tree;
    }

    Tree make_wide(std::uint32_t node_total)
    {
        Tree tree;
        const auto root = insert_root(tree, 0u).value();
        std::ranges::for_each(
            std::views::iota(1u, node_total),
            [&](std::uint32_t value)
            {
                (void)insert_child(tree, root, value, value);
            });
        return tree;
    }

    void run_case(
        std::string_view shape,
        const Tree& tree,
        std::uint32_t iterations)
    {
        FreezeWorkspace workspace;
        FreezeMetrics metrics{};
        std::size_t processed = 0;
        const auto freeze_start = Clock::now();
        std::ranges::for_each(
            std::views::iota(0u, iterations),
            [&](std::uint32_t)
            {
                auto outcome = freeze(tree, workspace);
                if (outcome)
                {
                    metrics = outcome->metrics;
                    processed += node_count(outcome->topology.polytree);
                }
            });
        const auto freeze_end = Clock::now();

        auto runtime = freeze(tree, workspace);
        const auto evaluation_start = Clock::now();
        std::ranges::for_each(
            std::views::iota(0u, iterations),
            [&](std::uint32_t)
            {
                processed += evaluation_plan(runtime->topology.polytree).node_count();
            });
        const auto evaluation_end = Clock::now();

        const auto freeze_microseconds =
            std::chrono::duration<double, std::micro>(freeze_end - freeze_start)
                .count()
            / iterations;
        const auto evaluation_microseconds =
            std::chrono::duration<double, std::micro>(
                evaluation_end - evaluation_start).count()
            / iterations;

        std::cout
            << "shape=" << shape
            << " nodes=" << node_count(tree)
            << " freeze_us=" << freeze_microseconds
            << " evaluation_plan_us=" << evaluation_microseconds
            << " reusable_scratch_bytes="
            << metrics.reusable_scratch_capacity_bytes
            << " builder_capacity_bytes=" << metrics.builder_capacity_bytes
            << " identity_mapping_bytes="
            << metrics.identity_mapping_capacity_bytes
            << " checksum=" << processed
            << '\n';
    }
}

int main()
{
    constexpr std::uint32_t iterations = 20;
    run_case("chain", make_chain(1'000), iterations);
    run_case("wide", make_wide(1'000), iterations);
    run_case("chain", make_chain(10'000), iterations);
    run_case("wide", make_wide(10'000), iterations);
    return 0;
}
