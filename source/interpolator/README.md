# Portable interpolator

Header-only C++17 code in the `interpolator` namespace. There are no game,
Windows, graphics or hook dependencies and no process-wide mutable state.
A game composes its own binary with this core; no adapter is loaded at runtime.

## Values and operations

`Pose` contains nine float basis values and three double world coordinates.
`InterpolatePose` shares the rigid/affine kernels used by the first adapter.
Basis views also accept a compile-time row stride, allowing an adapter to retain
owned numeric samples without repacking their basis on every render pass.
Opaque native fields and split-coordinate encoding remain outside the core.

`ValueEndpointHistory<Sample>` owns two samples and their times, ticks and
basis cache. `ObserveAndEvaluate<Codec>` handles seeding, same-tick corrections,
rollback, discontinuity policy and evaluation. `PoseHistory` and `PoseCodec`
provide the canonical representation. An adapter may supply a static codec for
another owned sample representation; its `Copy`, `Position` and `Blend`
operations must access only those supplied values, never native memory. The
codec also declares its `Input` and `Output` parameter types so pointer-backed
views pass pointers directly, while canonical poses pass by reference.
These calls are resolved at compile time, without virtual dispatch.

`CandidateHistory<Lanes, Capacity>` retains generation candidates and a
bounded finalized window; The Crew selects four keys. `ObservePresentationCandidate`, `FinalizeCohort` and
`EvaluatePresentationHistory` accept explicit provenance and evaluation times.
Evaluation caches belong to each history. Views borrow immutable endpoints only
for the duration of evaluation; returned/copied poses belong to the caller.

`Session<Key,Record,Hash>` owns one indexed channel/domain. `Observe`, `Retire`,
`Reset` and `Sweep` provide indexed storage and bounded scanning/compaction.
Maintenance cursors are private. Controlled insertion, erasure, reserve and rehash
preserve sweep validity; callers cannot mutate the underlying map directly.
`EffectHistoryMap` likewise owns its pruning cursor and finalization queue.
`TrackKey` separates channel, object and incarnation;
adapters may instead index a native pool slot and validate the full incarnation
in their record. Native retirement probes run in the adapter's maintenance loop.
`Expired` receives explicit tick/millisecond values and both inactivity thresholds.

`DirectionHistory<Capacity>` owns a separate held-direction timeline. Its delay,
cut angle, time and generation are inputs. `PresentationState` and normalized
rotation evaluation support independently guarded presentation domains.

## Adapter contract

Validate and decode native inputs under game lifetime protection before calling
the core. Supply seconds for sample/target times, monotonically sampled
milliseconds for observation age, and explicit tick/generation identities.
Advance the game's cohort publication even for empty captures. Keep physics,
solar and UI clocks in separate histories/sessions. The core samples no clocks
and acquires no game locks.

Encode results into private copies, preserving opaque fields and the game's
arithmetic conventions. Repeat identity and validity checks at consumption.
Game cuts, native resource generations, discovery, ownership, synchronization,
retirement proof, exception restoration and hooks remain adapter responsibilities.

See [The Crew adapter](../FramerateUnlocker/README.md) and
[standalone tests](../../tests/interpolator/README.md).

Retained third-party attribution is in
[licenses/ThirdPartyNotices.txt](licenses/ThirdPartyNotices.txt).
