# polytree

`polytree` is a compact static polytree implementation extracted from the
Wozzits Engine project.

The faithful Wozzits extraction remains available at the
`v0.0.1-wozzits-baseline` tag. Current development proceeds from that working,
measurable baseline; the graph traversal adapters now use the canonical
`algo::next` API.

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

The installed package requires `algo::algo`.

## Baseline status

The baseline intentionally includes the complete `static_dag.h` dependency
and retains the original `wz::core::graph` namespace, traversal code, storage,
ordering, and failure behavior. These are baseline facts rather than final API
commitments.

See [BASELINE.md](BASELINE.md) and [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Build and test

With an existing local `algo` checkout:

```sh
cmake -S . -B build \
  -DPOLYTREE_BUILD_TESTS=ON \
  -DPOLYTREE_ALGO_SOURCE_DIR=/path/to/algo
cmake --build build
ctest --test-dir build --output-on-failure
```

If neither `POLYTREE_ALGO_SOURCE_DIR` nor an installed `algo` package is
provided, CMake fetches the pinned compatible `algo` revision. Tests similarly
use an installed GTest package when available and otherwise fetch their pinned
upstream version.
