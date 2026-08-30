# Mutable authoring and freeze contracts

`MutablePolytree<NodeData, EdgeData>` is an ordered editable forest. It uses
`StableNodeId` for authoring identity; immutable `Polytree` continues to use
dense `NodeHandle` values. Stable IDs are relative to one mutable tree and are
never reused, including after subtree deletion.

The mutable implementation is isolated in `graph/mutable_polytree.h` and
`graph/polytree_freeze.h`. `graph/static_polytree.h` does not include either
header, so a static-only consumer does not instantiate or store authoring data.

## Mutation contracts

| Operation | Ordering | Complexity and allocation | Failure and invalidation |
|---|---|---|---|
| `insert_root` | Inserts at the requested root ordinal; `APPEND_CHILD` appends. | Amortized `O(1)` plus shifted roots; allocates a node record and payload. | Invalid ordinal, exhausted ID, or exhausted revision changes nothing. Existing IDs and payload references remain valid; the root span is invalidated on success. |
| `insert_child` | Inserts at the requested child ordinal. | Amortized `O(1)` plus shifted siblings; allocates node and edge payloads. | Invalid parent/ordinal and exhaustion change nothing. The selected child span is invalidated on success. |
| `reparent` | Moves to the requested position. Reparenting to the same parent is an ordered move. | `O(depth + source siblings + destination siblings)`; the replacement edge payload may allocate. | Invalid IDs/ordinal and cycles change nothing. Source and destination child spans are invalidated on success. |
| `detach_to_root` | Moves a node to the requested root position. A root input is reordered. | `O(source siblings + roots)`. | Invalid ID/ordinal changes nothing. Source and root spans are invalidated on success. |
| `erase_subtree` | Preserves the order of every surviving sequence. | `O(subtree size + source siblings)` time and subtree scratch. | Invalid ID or revision exhaustion changes nothing. All erased IDs and references are permanently invalid; unrelated IDs and payload references remain valid. |
| `replace_node_data` | N/A | One replacement payload allocation. | Invalid ID changes nothing. Only the replaced payload reference is invalidated. |
| `replace_parent_edge_data` | N/A | One replacement payload allocation. | Invalid/root ID changes nothing. Only the replaced edge reference is invalidated. |

Every successful mutation increments the tree revision exactly once. Logical
failures do not increment it and do not invalidate ranges or references.
Payload construction exceptions occur before topology is committed. Payload
destructors are required to be non-throwing, following standard container
requirements.

`roots` and `children` return borrowed contiguous spans. A span is invalidated
when its corresponding ordered sequence changes. Stable IDs are values and
remain valid until their subtree is erased. Moving a mutable tree transfers its
identity domain to the destination; the moved-from object has no useful handle
contract.

## Validation

`validate` checks the dense-handle limit, root state, parent/child reciprocity,
incoming edge presence, duplicate reachability, and unreachable cycle/orphan
components. Normal public mutations preserve these invariants, while freeze
still validates defensively.

Validation and hierarchy operations use iterative range/algorithm worklists;
deep chains do not consume the call stack.

## Deterministic freeze

`freeze` is non-consuming. It assigns dense runtime handles in canonical
root-first preorder, using explicit root and child order. It then builds the
existing compact static representation through `PolytreeBuilder` and `build`.
Consequently:

- static roots are ascending dense handles and reflect authoring root order;
- child CSR ranges preserve authoring child order;
- cached topological, reverse-topological, root, and dependency-level ranges
  retain the static v0.1.0 contract;
- repeated freeze of unchanged authoring input produces equal public topology
  spans and identity maps.

The result owns runtime-to-authoring and authoring-to-runtime maps. The latter
is sorted by stable ID and queried in `O(log nodes)`; the former is indexed in
`O(1)` by a valid dense runtime handle. Maps remain valid snapshot values after
the source changes, but `source_revision` no longer matches and their dense
handles must not be applied to a later freeze.

Freeze is `O(nodes log nodes + edges)` time: canonical traversal and expected
constant-time identity lookup are linear, while the public reverse map is
sorted by stable ID. Output owns one compact topology allocation and the two
identity-map vectors. `FreezeWorkspace` retains preorder, stack, and identity-
lookup scratch between calls. `FreezeMetrics` reports reusable scratch, builder
capacity, and identity-map capacity separately from runtime evaluation, which
remains allocation-free for cached plan access.

Node and edge payloads must be copy constructible for repeatable `freeze(const&)`;
edge payloads must also be default initializable and copy assignable for the
static parent-edge array.
