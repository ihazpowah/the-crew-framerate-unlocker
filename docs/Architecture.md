# The Crew architecture

The framerate plugin is composed into one C++17 translation unit by
`source/FramerateUnlocker/TheCrew.FramerateUnlocker.cpp`. Implementation headers
are included in dependency order. They are not independent translation units.

The portable `source/interpolator` core owns numeric operations, histories and
indexed sessions. It has no Windows, native game, graphics or hook dependencies.
The Crew adapter owns native discovery, identity and lifetime checks, clocks,
synchronization, conversions and private delivery. AdvancedGraphics, SkipIntro
and the XInput proxy remain independent binaries with independent mutable state.

Required startup installs the network worker, FPS/controller hooks and CPU
interpolation. Shadow, flare, minimap, menu and light hooks validate independently;
failure of an optional hook leaves the core running. Core startup owns presentation
activation rather than delegating it to minimap installation.

DXGI display queries live in `IntegratedController::DisplayModes` and are shared
by the FPS menu and runtime refresh-ceiling logic. The menu owns the option list.
Rigid-pose validation lives under `presentation/math`; reset orchestration stays
in `PresentationReset.h`. `ReadNonNullPointer` rejects null fields; `SafeRead` also
supports native fields where null is valid.

## CPU presentation composition

`presentation/CpuInterpolation.h` opens the adapter namespace and includes the
following implementation headers from `presentation/cpu`, in this order:

| Header | Responsibility |
| --- | --- |
| CpuPresentationState.h | Native sample layouts, immutable snapshots, owned history and lifetime state. |
| CpuReadability.h | Capture-local readability scopes and guarded page/array validation. |
| CpuResourceLifetime.h | Actor registration, retirement, native resource tables and publication identity. |
| CpuPoseEncoding.h | Native transform/wheel encoding and the portable interpolation codec. |
| CpuModelBindings.h | Model, wheel, hierarchy and prop ownership discovery and binding indexes. |
| CpuVehicleCapture.h | Bone palettes, mechanical bindings and vehicle-root capture/presentation. |
| CpuFrameCapture.h | Model evaluation and producer-joined immutable frame capture. |
| CpuRenderDelivery.h | Native producer callbacks, publication, render scopes and private input delivery. |
| CpuHookInstallation.h | Native signature validation and coordinated hook installation/rollback. |

`PlayerTagPresentation.h` is included after render delivery and before hook
installation. Its native layouts, addresses and signatures live in
`PlayerTagNative.h`. Flash worker batches retain their vehicle binding; the final
draw builder receives a private position correction from the same snapshot as
the car. Native visibility, billboard basis, scale, lift and game-owned batch
storage remain native.

The compositor preserves declaration and execution order. Hooks, native ABI
layouts, callback signatures and presentation timing must remain stable during
structural changes.

## Regression layout

The six canonical suite entry points stay directly under `tests`, alongside
the companion-plugin suite entry points and build/artifact checks.

| Location | Contents |
| --- | --- |
| tests/native/cpu | CPU capture, ownership, history, camera/light and PlayerTag fixtures. |
| tests/native/hooks | Native hook, publication, stale-worker and MinHook safety fixtures; the hook-chain fixture DLL source. |
| tests/native/plugins | FPS menu, settings, graphics and intro-plugin fixtures. |
| tests/native/presentation | Matrix, scene, effect, minimap and solar presentation fixtures. |
| tests/reference | Historical implementations, frozen native oracles and shared reference helpers. |
| tests/reference/light-correction | Retired later-light correction used only by regression references. |
| tests/interpolator | Independent portable checks and frozen numeric differential oracles. |

Tests keep their existing include order and exercise the composed production
implementation. Historical references remain outside the shipped ASI.
`tests/FramerateUnlockerHarness.h` selects historical-reference compatibility
before including production; those test switches are absent from the ASI entry point.

## Documentation and validation

Public documentation belongs under `docs`. Dependency notices and checksums
live in `docs/dependencies`. Local captures, reverse-engineering notes and
machine-specific evidence stay in the ignored `docs/evidence` directory.

Run the canonical `build/build-scripts/build_TheCrew.FramerateUnlocker.bat` from
the project root with the game closed. It owns regressions, artifact validation,
deployment and cleanup. See [project instructions](../AGENTS.md) and
[regression coverage](../tests/README.md).
