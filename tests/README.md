# Regression coverage

Run the canonical build from the project root:

~~~powershell
cmd /c "build\build-scripts\build_TheCrew.FramerateUnlocker.bat"
~~~

Require exit code 0 and BUILD CHECK: PASS. The script runs the independent
portable-core checks before compiling and running the six
framerate suites before building/deploying that ASI, using the same MinHook C
objects. It then invokes the standalone graphics, intro and proxy builds with
their own checks. A failing stage stops the build; deployment is sequential,
so a later failure can occur after an earlier plugin has been deployed.

The [portable interpolator checks](interpolator/README.md) run independently of
the native integration fixtures.

## Fixture directories

Suite entry points remain in this directory. Native fixtures are grouped under
`native/cpu`, `native/hooks`, `native/plugins` and `native/presentation`.
Historical helpers are under `reference`, with later-light correction under
`reference/light-correction`. Portable checks remain under `interpolator`.
The hook-chain fixture DLL source is `native/hooks/NativeHookChainFixture.cpp`.

## Framerate suites

Each executable includes FramerateUnlockerHarness.h, which selects historical
reference compatibility and includes the production TheCrew.FramerateUnlocker.cpp
entry point. Production does not define test compatibility switches.

| Suite | Coverage |
| --- | --- |
| ShadowCameraTests | Native cascade/receiver coherence, solar timeline and exception restoration; historical camera-cohort fallback is isolated in LegacyShadowCameraFixtures. Live CPU-selected shadow inputs are also covered by HookSafetyTests. |
| ScreenSpaceFlareTests | Generic CPU-camera projection/depth, direct native vehicle flares, identity guards, native attributes, visibility and fallback. |
| SceneRenderAnimationTests | Recorded scene repeats, latest endpoint selection, history views, sparse intervals, cohort ordering and camera upload/projection behavior. |
| MatrixInterpolationTests | Analytic quaternion, scale and reflection cases, split-coordinate precision, endpoint guards and reference-kernel comparisons. |
| RepeatedSceneUploadTests | Repeated-upload identity, endpoint correction, bounded gap repair, lookup and single-slot reference comparisons. |
| HookSafetyTests | Executed hooks, memory/publication safety, native upload routing, camera/minimap/light presentation, network-worker repair, FPS settings and storage. |

## Current behavior and historical references

Production symbols use responsibility-based names: CpuPresentationSnapshot,
CpuTransformSample, SelectedVehicleBinding, LightSourceInterpolation and
PresentationFrameClock. QueueForFinalization queues cohort work; it does not
refresh inactivity age. Actor IDs, pose identities, frame generations, native
resource generations, capture IDs and reset epochs have separate meanings.

Legacy-prefixed fixtures and reference/light-correction preserve removed
GPU association, shadow-camera fallback and later light-correction behavior.
Their passing results validate historical references, not installed runtime
features. The six suite filenames remain stable canonical build entry points.
Frozen reference fixtures retain their original source; the test-only
PresentationNamingCompatibility adapter supplies names for the current harness.

## HookSafetyTests fixtures

- StartupFeatureTests verifies that unavailable optional hooks leave core
  presentation activation independent of minimap installation.
- PlayerTagPresentationTests captures a native vehicle-bound Flash label with a
  valid null optional pose, rejects unsupported pose/ancestor modes, and checks
  four render phases against one unchanged native batch. It also covers private
  position writes, rotated anchors, latched snapshots, identity/epoch changes,
  changed records and ambiguous batch sharing.
- CpuAdmissionTests checks retained transform/wheel/bone histories across missed
  captures, generation and owner changes, invalid poses, deep/wide cyclic
  hierarchies, 2,049 bindings per owner, 4,097 vehicles, and 133,121 bones.
  Native span, palette capacity, timestamp and teleport guards remain covered.
- CpuHistoryPruningTests checks the combined 60-tick/one-second expiry boundary,
  pauses, active stationary poses, generation/null-slot retirement in both native
  buffers, failed reads, owner destruction and sharing, snapshot independence,
  repeated slot churn, incremental scans and hash-table memory reclamation.
- EffectPopulationTests crosses the former point/projector/source/emitter and
  sun/shadow owner limits, bridges missed generations, preserves held stationary
  samples and snapshot caps, and checks combined 60-tick/one-second retirement
  plus superseded same-tick candidate cleanup. Native provider identity, packet
  layout, camera matching, ambiguity and cut guards retain their own fixtures.
- Historical LightConsumptionScalingTests checks that 8,192 unrelated native providers do
  not rescan light-record payloads, that full validation still distinguishes or
  rejects duplicate identities, and that growing submission chains retain
  collisions and ordering across generation resets.
- CallbackPublicationTests, MinHookMemoryTests and MinHookPatchTests exercise
  executable/read-only code publication, non-executable metadata, allocation
  and protection failures, cache flushing, rollback, thread instruction-pointer
  handling, teardown and reuse.
- NativeHookChainTests loads two DLLs built from NativeHookChainFixture.cpp,
  each with its own MinHook instance. It checks both installation orders,
  either plugin alone, native caller retention, exceptions and rollback.
- LegacyUploadRoutingTests preserves historical provider admission and routing coverage.
  MinimapPresentationTests checks the current native minimap path, identity,
  native input preservation and fallback.
- LightSourceInterpolationTests covers source translation/rotation, native
  rebuilding, identity, authored fields, restoration and invalid-input fallback.
  Historical AmbientProjectorPresentationTests and PointLightPresentationTests exercise
  camera alignment, split-coordinate precision, verified native submission
  pairs, shared hooks, provider retention and delayed delivery.
- Historical SameFrameLightUploadsTests uses WARP buffers to check uploads arriving before
  camera publication. LightGenerationBoundaryTests covers retained static
  sources crossing generations. Both preserve authored fields and test rejection
  of stale, reused, ambiguous or mismatched inputs.
- StaleLightJobTests and StaleClockPathTests control worker ordering to verify
  that stale work cannot overwrite newer history or bindings. Pause callbacks
  are compiled only for the test executable.
- Historical ReusablePoolTests, LightProviderIndexTests, LightPayloadIndexTests and
  LightSourceHistoryLookupTests cover capacity, collisions, stable storage,
  retirement and reuse.
- FramerateMenuTests covers native 30/60, sorted/deduplicated Windows refresh
  rows filtered by current resolution and grouped from fractional DXGI timings,
  DXGI primary-output selection, rotated desktop dimensions, current-mode mismatch
  rejection and fractional VSync refresh ceilings,
  genuine integer-rate preservation, invalid fractions, dynamic-row Apply/persistence, unavailable
  resolution/enumeration fallback and final Unlimited at 1000 with VSync,
  refresh-based interpolation activation, the 1000 FPS bound, profile
  refresh, staging, preview, Apply/discard and persistence. MenuSettingsTests
  covers legacy DAT upgrades, field preservation, corruption and failed writes.

## Standalone suites

AdvancedGraphicsTests includes its production entry point and runs
AdvancedGraphicsMenuTests, AdvancedGraphicsFirstOpenTests, ShadowMapResolutionTests
and CubemapResolutionTests. Coverage includes Native/2X/4X scaling, shadow
limits, reflection frame consistency, target exclusions, hook rollback,
first-open menu rendering, event isolation, preview, Apply/discard and
independent persistence failures.

SkipIntroTests includes its production entry point and runs BootStateHookTests,
IntroSkipTests and IntroScreenTests. Coverage includes bounded boot lookup,
instance retention, native ABI/exception forwarding, startup movie eligibility,
decoder guards, seek fallback, deferred release, GFx ownership and publication
rollback.

XInputProxyTests exercises the production proxy with simulated loader outcomes
and real InitOnce concurrency. It checks restricted loading, forwarded exports,
ordinal fallback and continued XInput forwarding when optional ASIs fail.

## Build checks and cleanup

BuildCleanupTests.ps1 runs before the C++ suites. It checks intermediate/fixture
removal, preservation of deliverables and unrelated files, standalone cleanup
scope, and repeated cleanup of an already-clean tree.

ArtifactChecksTests.ps1 runs in the proxy build against corrupted copies of the
built DLL. It checks rejection of missing ASLR/DEP, writable/executable sections,
missing version resources, invalid section tables and truncated images.
Production verification also checks exports, artifact privacy and exact
deployment size, timestamp and SHA256.

Successful builds remove regression executables, fixture DLLs and compiler/linker
intermediates. Final plugin/proxy binaries, release manifests and the final
framerate PDB remain. Failed stages retain remaining files for diagnosis.
See the [project README](../README.md) for the full build and cleanup process.

## Reference data and limits

SceneRepeatCapture.h supplies recorded scene data; CapturedEndpointTimingTests
and ScenePresentationViewTests exercise endpoint ordering and history views.
MatrixInterpolationReference.h, PresentationReferenceHelpers.h,
RepeatedSceneLookupReference.h, SingleSceneSlotReference.h and
SceneClockReadReference.h retain comparison implementations used by the suites.
LegacyVehicleAdmission.h supports historical admission comparisons. These
helpers are test-only and are not compiled into the shipped ASI.

Keep reference behavior independent when changing production algorithms.
Regression assertions and synthetic fixtures do not establish measured gameplay
frame times, FPS, 1% lows or resolution of every visual issue.

See the [source map](../source/FramerateUnlocker/README.md) for module ownership
and [project instructions](../AGENTS.md) for build and gameplay-task rules.
## Scaling and incremental effect maintenance

`ScalingPathTests.h` checks stationary retention,
incremental expiry through map rehash/deletion, binding packet conflicts and
separate wheel identities, and capture-local memory protection validation.
`CpuPresentationCohortTests.h` also checks that next-tick candidates survive an
earlier cohort closing before their own CPU publication.

Effect expiry checks are spread across publications. Both 60 unused physics ticks
and one second must elapse before deletion; reclaiming a large backlog can take
additional publications. Active stationary objects remain observed and retained.
Map/group scan budgets are max(256, ceil(population/16)); delivery-slot scans use
max(512, ceil(population/16)). Rehashing, vector compaction and delivery-index
rebuilds can still perform proportional work when storage is actually reclaimed.


## Runtime diagnostic removal

Runtime profiling and the live collector have been removed. Regression harnesses retain native ABI, identity/validity and interpolation coverage without a runtime diagnostics interface.

Historical later-light correction helpers are isolated under reference/light-correction/ and included only through LegacyLightCorrection.h. The fixtures preserve analytic alignment, provider identity/validity, late-consumer and index coverage without production globals or callbacks. PruneReferenceEffectHistories adds historical delivery pruning only in these tests; production PruneEffectHistories handles source/shadow/sun histories exclusively. Native light-source interpolation and generic/native vehicle flare regressions continue to exercise production code.

LegacySceneHelpers.h / LegacySceneEndpoints.h isolate retired endpoint/association behavior. LegacySceneTrackedSlot adds test-only matching stamps to production candidate storage; prediction references derive world positions directly instead of expanding production history entries. Regression comparisons retain selected slots and presented pose checks, while assertions tied only to the removed cache implementation are retired. Flare tests explicitly reject null textures and descriptors whose tail is unreadable despite a readable texture field.
