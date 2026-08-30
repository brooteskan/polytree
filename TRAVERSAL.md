# Traversal and evaluation contracts

All contracts below apply to a valid immutable polytree produced by `build`.
Node handles passed to queries or traversal entry points are unchecked. Cached
views borrow `PolytreeStorage`; destroying or moving-from that owner invalidates
them. The topology is immutable, so no other operation invalidates a cached
view.

## Construction and cached plans

| Operation | Ordering | Complexity and allocation | Completion | Invalidation |
|---|---|---|---|---|
| `build` | Preserves insertion order among siblings. Topological selection retains the characterized LIFO worklist order. | `O(nodes + edges)` time and temporary storage; one persistent compact allocation. | Returns `nullopt` for a cycle and a storage value otherwise, including an empty forest. | Consumes the builder. Returned views borrow the storage. |
| `topo_order` | Parent before child; ties follow the characterized build order. | `O(1)`, no allocation. | Always returns the full cached range. | Borrows storage. |
| `reverse_topo_order` | Exact reverse of `topo_order`. | `O(1)`, no allocation. | Always returns the full cached range. | Borrows storage. |
| `roots` | Ascending dense node-handle order. | `O(1)`, no allocation. | Always returns the full cached range. | Borrows storage. |
| `dependency_order` / `dependency_level` | Increasing dependency depth. Within a level, nodes retain topological order. Each level is contiguous. | `O(1)` view creation, no allocation. | An out-of-range level is empty. | Borrows storage. |
| `evaluation_plan` | Bundles the four cached order forms and the level offsets without copying. | `O(1)`, no allocation. | Always complete. | Every member borrows storage. |

Nodes in one dependency level have no parent-child dependency on each other.
Levels must still be consumed in increasing order when evaluating data that
depends on a parent result.

The persistent allocation stores aligned spans in this order:

```text
node payloads
child CSR offsets
child handles
outgoing edge payloads
parent handles
parent edge payloads
topological handles
reverse-topological handles
root handles
dependency-level handles
dependency-level offsets
```

Child CSR ranges are grouped by parent and preserve edge insertion order among
siblings. Plan spans contain copied handles, not pointers into builder vectors.

## Canonical traversal orders

| Operation | Ordering | Complexity and allocation | Completion | Invalidation |
|---|---|---|---|---|
| `depth_first_order` | Root first. Because stored children are pushed in order onto a LIFO worklist, siblings are visited in reverse stored order. | `O(subtree size)` time and owning vector storage. | Produces the full subtree. | Independent of storage after return because handles are copied. |
| `breadth_first_order` | Root first, then breadth-first in stored sibling order. | `O(subtree size)` time and owning vector storage. | Produces the full subtree. | Independent of storage after return. |
| `ancestor_order` | Immediate parent first, ending at the root; excludes the input node. | `O(depth)` time and owning vector storage. | Produces the full ancestor chain. | Independent of storage after return. |

The three owning orders are the canonical descendant and ancestor mechanism.
They use standard range algorithms and contain no recursive hierarchy walk, so
deep chains do not consume the call stack.

## Sequential adapters and materialization

| Operation | Ordering | Complexity and allocation | Completion | Invalidation |
|---|---|---|---|---|
| Visitor `dfs`, `bfs`, `walk_ancestors` | Respective canonical order. | Canonical-order cost; visitor execution adds linear time. | Visitors consume the full order. | The temporary order is destroyed on return. |
| Sink `dfs`, `bfs`, `walk_ancestors` | Respective canonical order. | Canonical-order cost; sink execution stops at first rejection. | Returns `algo::next::execution_status::completed` or `truncated`. The canonical order is planned before sink execution. | The temporary order is destroyed on return. |
| `*_materialize` | Respective canonical order; root-first ancestors reverse the successfully written immediate-first prefix, preserving baseline truncation values. | Canonical-order cost plus caller scratch; no second owning result. | `PolytreeMaterialization::status` reports whether scratch accepted the full order. Exact capacity is complete. | `values` borrows caller scratch. |
| `roots_materialize` | Ascending cached root order. | `O(root count)` time, caller scratch only. | Explicit completed/truncated status. | `values` borrows caller scratch. |
| `as_sink` | Pipeline stage order inside traversal order. | No adapter allocation. | A pipeline/output rejection becomes sink rejection and propagates as traversal truncation. | Borrows the pipeline and output. |

## Document-tree helpers

| Operation | Ordering | Complexity and allocation | Completion | Invalidation |
|---|---|---|---|---|
| `child_ordinal`, `previous_sibling`, `next_sibling` | Stored sibling order. | `O(sibling count)`, no allocation. | Sentinels report roots, boundaries, or absence. | Returned handles are values. |
| `depth` | Immediate-parent chain. | `O(depth)` time and temporary ancestor-order allocation. | Complete for a valid node. | Returned count is a value. |
| `subtree_size` | Uses canonical DFS; result is order-independent. | `O(subtree size)` time and temporary DFS allocation. | Complete for a valid root. | Returned count is a value. |
| `find_child_if` | Stored sibling order; first match wins. | `O(child count)`, no allocation. | Returns `INVALID_NODE` when no child matches. | Returned handle is a value. |
| `walk_path_from_root` | Root-to-node edges. | `O(depth + sibling scans)` plus temporary ancestor storage and caller scratch. | Returns `false` for `INVALID_NODE` or insufficient scratch; invokes nothing on those failures. | Visitor arguments borrow topology payloads; scratch remains caller-owned. |
