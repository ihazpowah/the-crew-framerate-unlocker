# Interpolator checks

`InterpolatorTests.cpp` includes only the portable core, standard C++ headers
and a frozen test oracle. The canonical build compiles it without native include
paths or libraries. `BoundaryTests.ps1` rejects dependencies escaping the core.

The same executable can be built independently on a C++17 platform:

```sh
cmake -S tests/interpolator -B build/portable-core
cmake --build build/portable-core --config Release
ctest --test-dir build/portable-core -C Release --output-on-failure
```

Two synthetic adapters use different matrix layouts and coordinate encodings:
row-major float basis/double world coordinates, and column-major doubles with
integer millimetres relative to an origin. They decode into the same core pose,
use isolated indexed sessions and encode results while preserving opaque data.
Tests cover same-tick correction, sparse samples, rollback, identity reuse,
channel separation, retirement, immutable result ownership, active stationary
observations, dual-threshold pruning, bounded sweeps, cohort selection,
extrapolation boundaries and independent direction/physics clocks.
Mutation checks exercise partial sweeps across reserve, rehash, erasure, reset
and container swap, including finalization-queue ownership.

`reference/BaselineMath.h` and `reference/BaselineHistory.h` are frozen from commit `df2741c`, with their own types
and no dependency on extracted kernels. Differential tests require byte equality
for rigid/affine output, status codes and split-reference encoding, plus 840 identical history windows/intervals. The native
integration suites retain their existing tolerances and byte assertions.

[The Crew integration and historical references](../README.md)
cover hooks, resource validation, native private copies, worker ordering and
exception restoration. They are deliberately outside these independent checks.
