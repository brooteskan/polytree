# Contributing

The tagged Wozzits baseline is a historical reference and must remain
reproducible. Changes after the baseline should be small, behaviorally covered,
and separated from mechanical extraction work.

New implementation and test code must not introduce handwritten iteration or
traversal loops. Prefer the `algo` package, standard algorithms, ranges,
functions, and composable operations. Existing imported loops are baseline
debt and should be replaced only through explicit, test-backed refactors.

Run the full CTest suite on every change. Document ordering, allocation,
termination, invalidation, and complexity for new public operations.
