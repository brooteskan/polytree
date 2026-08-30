# Known issues

This repository starts with observed Wozzits behavior, including design and
implementation concerns that will be addressed only through separately tested
changes.

## Legacy static-DAG header

`static_polytree.h` no longer imports `static_dag.h`; shared handles now live in
`graph/handles.h`, and storage carving is private to the polytree implementation.
The baseline DAG header remains installed for source compatibility. It is not
part of the current polytree traversal contract or control-flow policy check.

## Legacy API surface

The namespace remains `wz::core::graph`, and headers remain under `graph/`.
These names preserve compatibility but are not a final public-API decision.

## Unchecked handles

Queries and traversal entry points assume a handle from the same valid built
polytree. Invalid handles are unchecked, matching the baseline contract.

## Payload lifetime

The compact allocation retains the extracted placement-construction and
storage ownership model. Non-trivial payload destruction and allocation-failure
recovery require a separate storage-lifetime pass before such payloads can be
advertised as supported.
