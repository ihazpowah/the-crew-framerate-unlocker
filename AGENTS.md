# TheCrewFramerateUnlocker

An x64 D3D11 ASI plugin that uncaps framerate and smooths vehicle motion in The Crew.

## Source organization

- source/interpolator: portable header-only C++17 math, owned histories, evaluation and indexed sessions under the interpolator namespace. No Windows, native game, graphics or hook dependencies.
- source/FramerateUnlocker: TheCrew.FramerateUnlocker.cpp composes the framerate adapter into one translation unit. It owns native capture, validity and identity checks, clocks, hooks, settings, conversions and private delivery.
- source/FramerateUnlocker/presentation/cpu: ordered CPU state, validation, capture, delivery and hook implementation headers composed by CpuInterpolation.h.
- tests/interpolator: independent core checks and synthetic adapters. Native fixtures are grouped under tests/native; historical helpers are under tests/reference. Canonical suite entry points remain in the tests root.
- SkipIntro: independent, always-enabled TheCrew.SkipIntro.asi handles intro movies, intro screens and boot state. No INI.
- AdvancedGraphics: independent, always-enabled TheCrew.AdvancedGraphics.asi owns shadow-map and cubemap resolution settings and its Apply/discard menu. Uses TheCrew.AdvancedGraphics.dat; no INI.
- Shared code owns native GFx values, menu ABI declarations, native memory access, settings storage and coordinated hook chaining. Mutable plugin state remains independent.
- The XInput proxy loads the three optional plugins independently and forwards XInput calls. It declares XInput structs locally.
- Vendored MinHook provides the native hooking implementation. Preserve its source checksum record and copyright notices.

Keep native ABI layouts, hook signatures, callback behavior and presentation timing unchanged during structural edits. Implementation headers are not separate translation units.

## Build workflow

Requires Visual Studio 2022 or 2026 with the Desktop development with C++ workload and x64 tools.

Use build_TheCrew.FramerateUnlocker.bat as the canonical build entry point, with the project root as the working directory. Locate the script within the project; do not hardcode a user-specific location.

The canonical script:
1. Requires TheCrew.exe to be fully closed and validates project and deployment locations.
2. Uses setup_msvc.bat to discover the x64 toolchain.
   invoke_private_build.ps1 supplies a temporary neutral drive mapping and build
   temporary directory. Compiler path mapping and a filename-only PDB reference
   keep personal locations out of artifacts. Artifact checks reject the local
   profile name in binaries and final debugging symbols.
3. Checks MinHook provenance and compiles its C sources.
4. Runs build-cleanup and independent portable-core checks, followed by the six native C++ regression suites with the same MinHook objects used by the plugin.
5. Builds and validates the framerate ASI.
6. Invokes build_TheCrew.AdvancedGraphics.bat, build_TheCrew.SkipIntro.bat and build_xinput1_3.bat, including their regressions and artifact checks.
7. Deploys and verifies each exact artifact.
8. Removes temporary build files before reporting success.

The six suites are ShadowCameraTests, ScreenSpaceFlareTests, SceneRenderAnimationTests, MatrixInterpolationTests, RepeatedSceneUploadTests and HookSafetyTests.

Success requires exit code 0 and BUILD CHECK: PASS. Then stop. Do not repeat tests, manually copy files, perform extra deployment checks, inspect runtime memory, launch the game or automatically request gameplay testing after success. A separate explicit debugging or gameplay request is required.

On failure, address the reported stage and rerun the same script. Never compensate with an ad-hoc copy or claim success. Report the build result concisely. Passing regressions does not establish that every gameplay visual issue is resolved.

## Deployment rules

The build scripts own the configured installation destination and exact-destination verification. Consult those scripts for locations.

Only these plugin filenames may be deployed:
- TheCrew.FramerateUnlocker.asi
- TheCrew.AdvancedGraphics.asi
- TheCrew.SkipIntro.asi

The proxy is xinput1_3.dll. It belongs beside the game executable; the ASIs belong in the installation's plugin directory. Other ASIs must never be overwritten. Each plugin is optional.

Verify a nonempty x64 PE32+ DLL and matching source/destination size, timestamp and SHA256. Preserve installed DAT settings. Standalone builds remove only their obsolete TheCrew.SkipIntro.ini and TheCrew.AdvancedGraphics.ini.

If TheCrew.exe is running, the user must fully close it before rerunning the script; an already-loaded ASI remains in memory.

## Build cleanup

clean_build.ps1 removes compiler/linker intermediates, regression executables, fixture DLLs and the compiler PDB after successful builds. Preserve final ASIs, the proxy DLL, release manifests and the final framerate PDB.

Standalone builds clean their own output directories. The canonical build also cleans shared intermediates after all builds and deployment checks. Cleanup failure fails the build. Failed builds retain files for diagnosis.

## Game launch

When a separately requested task requires launching the game:
1. Confirm TheCrew.exe is not running.
2. Launch the installed TheCrew.exe immediately, without a delay timer.
3. Ask the user to confirm that the game is running.
4. Continue only after that confirmation.

## Game constants

| Constant | Value | Purpose |
| --- | --- | --- |
| kManagerRva | 0x2c98a40 | Game manager object |
| kPhysicsCallbackRva | 0x455c80 | Physics tick callback |
| kExpectedImageSize | 0x535a000 | Executable size check |
| Player sync root offset | 0xA90 | Player vehicle transform |

## Settings

The FPS menu offers native 30, native 60, unique game-window monitor refresh rates above 60 and below 1000 supported at that monitor's current Windows resolution in ascending order, then Unlimited, using the existing 1000 FPS cap. Unlimited retains that software cap with VSync enabled and leaves presentation pacing to native VSync. Interpolation activation uses the game monitor's refresh ceiling. Native 30/60 and the displayed selection are preserved.

The game's own visible unowned window selects its monitor, including while unfocused; the matching DXGI output supplies the desktop mode and available timings. The VSync ceiling follows monitor moves, with cached mode queries refreshed twice per second or immediately after a monitor change. Menu rebuilds refresh available rates while keeping pending selection indices stable. Rates are rounded to the nearest whole FPS before deduplication, so 119.88 and 120 share one 120 option. The current mode must match the output's desktop bounds; current and enumerated dimensions are normalized for rotation. Missing windows/outputs never fall back to the primary display. This does not change the Windows display mode.

Apply stores the selection in TheCrew.FramerateUnlocker.dat beside the ASI. Legacy FPS-only DAT files remain readable and upgrade on save without changing the stored FPS. The framerate plugin requires no INI.

## Naming and privacy

Use TheCrewFramerateUnlocker as the product name. Keep this document free of filesystem paths and personal names. Avoid embedding user-specific locations in distributable artifacts or documentation.

## CPU performance reporting

- Use the recorded parked free-roam workload as the primary reference for future synthetic CPU benchmarks: 336–352 full-path transforms, 8–12 full-path wheels, 128 full-path bones, plus roughly 8–22 traffic transforms and 32–88 traffic wheels. No traffic bone palettes were captured. Keep full interpolation enabled for all objects. Treat larger per-vehicle bone/transform populations as stress tests, not representative gameplay. Recorded counts do not establish motion distributions, palette layout, or total plugin CPU cost.
- Report percentage improvement and milliseconds saved per render frame against the same baseline and workload.
- Label results measured, synthetic or estimated. If render-frame timing is unmeasured, say so. Never relabel per-operation or per-physics-tick savings as per-render-frame savings.
- Prioritize meaningful total CPU frame-time reductions. Around 0.01 ms saved per operation or tick is a minor optimization, not evidence of a meaningful gameplay improvement.
- Include estimated equivalent FPS from a 230 FPS baseline using FPS = 1000 / (1000 / 230 - milliseconds saved per render frame). State that this assumes the saving directly reduces total frame time; synthetic plugin-overhead results do not establish measured gameplay FPS.
- Include estimated 1% low FPS from a 150 FPS baseline using FPS = 1000 / (1000 / 150 - milliseconds saved per render frame). This assumes the same saving applies to slow frames; it is not a measured 1% low without gameplay frame-time distribution measurements.
