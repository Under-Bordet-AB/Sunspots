# Compute

## Purpose

Compute consumes weather/price inputs and produces planning/output artifacts.

It is the policy layer of the system: load inputs, wait for enough data, run an algorithm, write results.

## Key files

- `src/compute/compute_manager.c`
- `src/compute/algorithms/compute_heuristic.c`
- `src/compute/algorithms/compute_lp.c`

## Design strengths

- The high-level flow in `compute_manager.c` is easy to understand.
- Algorithm implementations are at least separated from orchestration.
- The system clearly expects compute to be a consumer of canonical data, not a raw API client.

## Main weaknesses

- `compute_manager.c` still carries too much orchestration and policy.
- Waiting, loading, validation, and result serialization are tightly packed together.
- The algorithm selection/config shape is still fairly primitive.

## Critique

This module has a solid role in the architecture, but the orchestration file is still too central. It acts as bootstrap, poller, loader, coordinator, and serializer. That is manageable now, but it is exactly the kind of file that becomes brittle when new data sources or algorithm modes are added.

What I would expect long term:

- input loading split from freshness/poll policy
- algorithm adapters behind one common interface
- result serialization split from compute orchestration

The compute area is not bad code; it is just carrying more responsibility than one file should.
