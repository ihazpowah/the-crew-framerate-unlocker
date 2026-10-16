# FramerateUnlocker source

TheCrew.FramerateUnlocker.cpp is the ASI entry point and ordered composition of
the production modules. It is compiled as one C++17 translation unit. Headers
are included at file scope and retain their own namespace and include guards.
Do not compile implementation headers as separate translation units.

## Module ownership

| Directory | Responsibility |
| --- | --- |
| runtime | Platform dependencies, plugin/controller state, FPS enforcement, stage resolution and network-worker repair. |
| hooks | Near allocation and generated x64 callback stubs. |
| clock | Physics-time metadata and immutable render-frame clock publication. |
| config | Adjacent DAT path and saved FPS selection. |
| menu | FPS choices and native preview, Apply and discard handling. |
| startup | Plugin worker initialization. |
| presentation | Native camera capture, GPU camera presentation, vehicle basis handling and minimap interpolation. |
| presentation/scene | Snapshot histories, upload identity, matrix interpolation, endpoint correction, cohorts and player-root capture. |
| presentation/effects | GPU-camera projection and vehicle screen-space flare cohorts. |
| presentation/lighting | Native light sources, cone/point presentation, retained providers and delayed uploads. |
| presentation/shadows | Private shadow-camera inputs and a separate solar-direction timeline. |
| rendering | Native upload admission, D3D11 hooks and resource tracking. |

The entry point establishes dependencies in order: platform/settings and clocks;
scene state and interpolation; upload tracking; controller, memory and hook
helpers; camera/effect consumers; clock publication and FPS control; menus,
lighting and startup.

## Shared code and plugin boundaries

The shared modules provide:

- NativeGfx.h: native value layout and ownership operations.
- NativeMenuAbi.h: menu addresses, callback signatures and profile-settings lookup.
- NativeMemory.h: exception-contained memory access and callback replacement.
- NativeHookChain.h: serialized installation, recorded peer-patch validation and original-caller retention.
- MenuSettings.h: versioned DAT storage, atomic writes and legacy FPS-only compatibility.
- ReusablePool.h: stable-address storage, retirement and reuse for scene/light tracking.

AdvancedGraphics and SkipIntro are separate ASIs. The framerate plugin owns FPS
settings and presentation; it does not own intro bypass or graphics-resolution
menus. Shared declarations do not combine plugin-local hook pointers or settings
transactions.

## Presentation and settings

The FPS menu offers native 30, native 60 and Unlimited, using a 1000 FPS cap.
VSync limits the effective cap to the primary display refresh rate while
preserving the displayed selection. Effective limits above 60 enable the
presentation pipeline. Apply stores TheCrew.FramerateUnlocker.dat beside the
ASI; legacy FPS-only values remain readable. No INI is required.

NativeUploadRouting.h validates the upload descriptor, runtime index, field-copy
layout, exact resource and size. Upload identity is scoped to the native call
and restored on return or exception. Flash and unknown providers retain native
behavior. Verified vehicle and wheel providers can use deformation admission;
other scene providers retain strict matrix checks. Finite poses and valid
clock metadata remain mandatory, and opaque upload metadata is preserved.

Minimap presentation uses immutable native vehicle poses on the physics clock,
with independent identity and camera-override checks. Only a temporary pose
passed to the native consumer changes; source vehicle/widget data stays native.

PresentationCallbacks.h connects earlier collectors to later consumers.
LightPresentation.h composes cone and point handling. LateLightUploads.h shares
pending-buffer storage, COM ownership and D3D11 delivery; cone and point modules
retain their own provenance, clock and admission checks. Missing, stale,
ambiguous or invalid inputs retain native fallback.

## Build and regression coverage

Run the canonical script from the project root:

~~~powershell
cmd /c "build\build-scripts\build_TheCrew.FramerateUnlocker.bat"
~~~

All six framerate regression executables include the production entry point.
The script also builds/tests the standalone plugins and proxy, verifies exact
deployment and performs automatic cleanup. Final binaries, release manifests
and the final framerate PDB remain; intermediates and regression binaries are
removed after successful builds.

Keep ABI layouts, hook signatures, callback behavior and presentation timing
unchanged during structural edits. Reference helpers used only by tests stay
outside the shipped plugin.

See the [project README](../../README.md) for toolchain, privacy, deployment and
cleanup details, [project instructions](../../AGENTS.md) for workflow rules, and
[regression coverage](../../tests/README.md) for the maintained suites.
Passing regressions does not replace gameplay validation.
