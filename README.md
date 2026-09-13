# polytree

`polytree` provides compact immutable runtime topology and ordered mutable
authoring topology. The static implementation was extracted from the Wozzits
Engine project.

The faithful Wozzits extraction remains available at the
`v0.0.1-wozzits-baseline` tag. Version 0.1.0 preserves its valid-tree traversal
orders while making contiguous traversal ranges and cached evaluation plans the
primary contract. Sequential sink adapters use `algo::next` and report explicit
completion or truncation.

Version 0.2.0 adds stable authoring identity, atomic hierarchy editing,
validation, and deterministic freeze into the existing compact static form.

## CMake target

```cmake
find_package(polytree CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE polytree::polytree)
```

Existing include paths remain valid:

```cpp
#include <graph/static_polytree.h>
#include <graph/static_polytree_algo.h>
```

Mutable authoring is opt-in by header:

```cpp
#include <graph/mutable_polytree.h>
#include <graph/polytree_freeze.h>

wz::core::graph::MutablePolytree<Node, Edge> authoring;
const auto root = insert_root(authoring, Node{}).value();
const auto child = insert_child(authoring, root, Node{}, Edge{}).value();
auto runtime = freeze(authoring);
```

The installed package requires `algo::algo`.

## Evaluation plans

Every built static polytree caches contiguous topological, reverse-topological,
root, and dependency-level ranges in its single backing allocation:

```cpp
const auto plan = wz::core::graph::evaluation_plan(storage.polytree);

consume(plan.topological_order);
consume(plan.reverse_topological_order);
consume(plan.roots);
consume(plan.dependency_level(0));
```

`depth_first_order`, `breadth_first_order`, and `ancestor_order` return owning
contiguous ranges. Existing `dfs`, `bfs`, and `walk_ancestors` visitor and sink
forms remain adapters over those ranges. Scratch materializers return a
`PolytreeMaterialization` containing both the written span and an
`algo::next::execution_status`.

## Baseline status

The baseline intentionally included the complete `static_dag.h` dependency.
The current static-polytree headers use the narrow `graph/handles.h` contract;
the legacy DAG header remains only for source compatibility and is not in the
polytree dependency closure.

See [BASELINE.md](BASELINE.md), [TRAVERSAL.md](TRAVERSAL.md),
[MUTABLE.md](MUTABLE.md), and [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Build and test

Initialize the pinned source dependencies before configuring:

```sh
git submodule update --init --recursive
cmake -S . -B build -DPOLYTREE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`external/algo` and `external/googletest` are exact Git submodule revisions.
CMake never fetches source dependencies or selects installed packages/local
checkout overrides. Shared existing targets must originate from initialized
submodules with the same pinned revisions. A missing or mismatched gitlink
fails configuration with an initialization instruction.

Configure `POLYTREE_BUILD_BENCHMARKS=ON` to build the standalone
`polytree_freeze_benchmark`. It reports freeze time and reusable scratch,
builder, and mapping capacity separately from cached runtime-plan access.
