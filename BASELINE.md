# Wozzits baseline

This document records the contents of `v0.0.1-wozzits-baseline`. Checksums
below describe that tag and are not assertions about later commits on `main`.

The imported files come from Wozzits `master` at:

```text
0ca3377dd5d9472a5a73426646026f2b085994e1
```

They are intentionally unchanged. Package infrastructure and documentation
are new; production and test sources are byte-for-byte copies.

## Imported files

| Repository path | Wozzits source path | SHA-256 |
|---|---|---|
| `include/graph/concepts.h` | `window_engine/graph/concepts.h` | `ED5146444F72E03AD04BE41F56BA5D66469B6B914C81B77AB862CBE7091D4433` |
| `include/graph/static_dag.h` | `window_engine/graph/static_dag.h` | `82068DAE4AA807E3EA973321445B65DFFACE97EB668E873BF39BB04327F47B9C` |
| `include/graph/static_polytree.h` | `window_engine/graph/static_polytree.h` | `1F0F51C6D031A78857AE65063E9F198A54C749CFC4B1DF6B6DDC193715971256` |
| `include/graph/static_polytree_algo.h` | `window_engine/graph/static_polytree_algo.h` | `09AFF0E99C83933618EF2E548FC15EF1872616810770B0A38C1324CE7C4CCBBB` |
| `tests/graph/static_polytree_tests_0.cpp` | `tests/graph/static_polytree_tests_0.cpp` | `72E0B1E29ED984D0B4E80B735D23BB1F3DD66ED6931BE87AC11826CB78621790` |
| `tests/graph/static_polytree_algo_tests_0.cpp` | `tests/graph/static_polytree_algo_tests_0.cpp` | `425B23F62677AF7D9C9340297E944711ED9A6E42F03EFA8DA0EC2AD79B349BA0` |

## Dependency baseline

The production include closure is preserved rather than cleaned surgically:

```text
polytree::polytree
  -> algo::algo
  -> C++ standard library
```

`static_polytree.h` still includes the complete `static_dag.h` for its handle,
sentinel, and storage carving facilities. Removing that incidental dependency
is a later behavior-preserving refactor.

## Test baseline

The two existing Wozzits static-polytree test translation units are built and
run independently against the narrow standalone package target. No Wozzits
engine target or engine dependency is linked.
