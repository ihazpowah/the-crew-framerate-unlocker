# Regression coverage

Run the canonical build from the project root:

~~~powershell
cmd /c "build\build-scripts\build_TheCrew.FramerateUnlocker.bat"
~~~

Require exit code 0 and BUILD CHECK: PASS. The script compiles and runs the six
framerate suites before building/deploying that ASI, using the same MinHook C
objects. It then invokes the standalone graphics, intro and proxy builds with
their own checks. A failing stage stops the build; deployment is sequential,
so a later failure can occur after an earlier plugin has been deployed.

## Framerate suites

Each executable includes the production TheCrew.FramerateUnlocker.cpp entry
point and exercises its composed implementation.

| Suite | Coverage |
| --- | --- |
| ShadowCameraTests | Camera/world timing, private shadow inputs, native cascade/receiver coherence, solar timeline, held keys, cuts, gaps and exception restoration. |
| ScreenSpaceFlareTests | GPU-camera projection, vehicle screen-space cohorts, emitter identity, native attributes, visibility and fallback. |
| SceneRenderAnimationTests | Recorded scene repeats, latest endpoint selection, history views, sparse intervals, cohort ordering and camera upload/projection behavior. |
| MatrixInterpolationTests | Analytic quaternion, scale and reflection cases, split-coordinate precision, endpoint guards and reference-kernel comparisons. |
| RepeatedSceneUploadTests | Repeated-upload identity, endpoint correction, bounded gap repair, lookup and single-slot reference comparisons. |
| HookSafetyTests | Executed hooks, memory/publication safety, native upload routing, camera/minimap/light presentation, network-worker repair, FPS settings and storage. |

## HookSafetyTests fixtures

- CallbackPublicationTests, MinHookMemoryTests and MinHookPatchTests exercise
  executable/read-only code publication, non-executable metadata, allocation
  and protection failures, cache flushing, rollback, thread instruction-pointer
  handling, teardown and reuse.
- NativeHookChainTests loads two DLLs built from NativeHookChainFixture.cpp,
  each with its own MinHook instance. It checks both installation orders,
  either plugin alone, native caller retention, exceptions and rollback.
- NativeUploadRoutingTests includes MinimapPresentationTests. These exercise
  provider admission, invocation lifetime, Flash/unknown rejection, deformation
  eligibility, minimap identity, native input preservation and fallback.
- CameraRotationTests exercises camera discovery, physics/render-rate source
  updates and camera/object agreement at multiple frame rates.
- NativeLightPresentationTests covers source translation/rotation, native
  rebuilding, identity, authored fields, restoration and invalid-input fallback.
  AmbientProjectorPresentationTests and PointLightPresentationTests exercise
  camera alignment, split-coordinate precision, verified native submission
  pairs, shared hooks, provider retention and delayed delivery.
- SameFrameLightUploadsTests uses WARP buffers to check uploads arriving before
  camera publication. LightGenerationBoundaryTests covers retained static
  sources crossing generations. Both preserve authored fields and test rejection
  of stale, reused, ambiguous or mismatched inputs.
- StaleLightJobTests and StaleClockPathTests control worker ordering to verify
  that stale work cannot overwrite newer history or bindings. Pause callbacks
  are compiled only for the test executable.
- ReusablePoolTests, LightProviderIndexTests, LightPayloadIndexTests and
  NativeLightHistoryLookupTests cover capacity, collisions, stable storage,
  retirement and reuse.
- FramerateMenuTests covers native 30/60 plus Unlimited, VSync ceilings, profile
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
No standalone benchmark or runtime capture workflow is maintained here.
Regression assertions and synthetic fixtures do not establish measured gameplay
frame times, FPS, 1% lows or resolution of every visual issue.

See the [source map](../source/FramerateUnlocker/README.md) for module ownership
and [project instructions](../AGENTS.md) for build and gameplay-task rules.
