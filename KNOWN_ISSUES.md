# Known baseline issues

This repository starts with observed Wozzits behavior, including design and
implementation concerns that will be addressed only through separately tested
changes.

## Full static-DAG dependency

`static_polytree.h` imports `static_dag.h` for shared graph handles, the invalid
node sentinel, and a storage-layout helper. The baseline therefore installs
the whole DAG header even though DAG behavior is not part of the intended
polytree package.

## Legacy API surface

The namespace remains `wz::core::graph`, and headers remain under `graph/`.
These names preserve compatibility but are not a final public-API decision.

## Handwritten control flow

The imported implementation contains loops and duplicated visitor/sink
traversal logic. They remain unchanged in this baseline. New implementation
work follows the functional control-flow policy and must be introduced as
reviewable, behavior-preserving changes.

## Ordering and storage behavior

The builder, cached topological order, edge sorting, scratch truncation,
failure behavior, and payload lifetime characteristics are retained exactly.
Any correction requires explicit characterization and migration tests.
