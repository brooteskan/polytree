# Incremental ancestor evaluation

`AncestorClosureWorkspace::prepare(evaluation_plan(tree))` binds reusable scratch
to an immutable tree. Call it after every topology replacement, even if the node
count and dense handle values are unchanged. It retains no tree pointers.

`ancestor_closure_order(tree, seeds, workspace)` returns the inclusive union of
all seed nodes and their ancestors. Each node appears exactly once, in the exact
cached reverse topological order (children before parents). Seeds may be repeated,
unordered, empty, roots, or members of different trees in a forest. Node handles
are unchecked, matching the other immutable traversal APIs. A cycle cannot be
present in a successfully built polytree.

Construction of the workspace costs O(N) time and retained capacity. A closure
costs O(S + A log A) time, where S is the seed count and A the union size. Marks are
cleared for the current union before returning, so an empty call does not scan
the previous union. There are no recursive calls, and a deep chain does not grow
the call stack. Closure evaluation allocates nothing after prepare. Order,
rank, and mark capacity consume approximately 9 bytes per node, excluding vector
objects and allocator overhead; capacity_bytes() reports retained vector payload.

The returned span borrows workspace.order until the next prepare, closure call,
move, or destruction of that workspace. Callers must not use an old handle set
against a new topology. The workspace is for one serial owner, not concurrent
evaluation. The library supplies topology and ordering only: deciding which
inputs changed, what aggregates mean, when propagation can stop, and publishing
consumer resources remain caller responsibilities.

Ancestor propagation differs from downward transform propagation. For a cached
aggregate depending only on child aggregate values and local inputs, callers can
mark directly changed nodes, obtain the complete closure, and filter that order
to nodes still marked. After processing a node they mark its parent only if the
aggregate changed. Local output changes must be delivered even when the aggregate
does not change. No such policy is built into the generic closure.

The immutable builder now uses contiguous temporary CSR adjacency rather than
one vector allocation per non-leaf. It preserves the existing LIFO topological
order and sibling insertion order. Already parent-grouped edges skip stable_sort;
other insertion sequences still use stable sorting. Build still owns O(N + E)
temporary vectors and one retained compact allocation; reusable bulk construction
or a reduced cached-plan layout is not part of this change.

The independent tests cover forests, shared ancestors, duplicate seeds, empty
calls, capacity reuse, random differential parent-chain unions, rebinding and a
10,000-node chain. Existing ordering, mutable/freeze, lifetime and policy tests
also apply. Terrain integration's separately allocation-instrumented tests
exercise the operation under warm sparse/dense updates.

