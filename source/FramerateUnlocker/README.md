# The Crew adapter

TheCrew.FramerateUnlocker.cpp composes the production headers into one C++17
translation unit. Implementation headers are not separate translation units.

The adapter composes the portable [interpolator](../interpolator/README.md).
It owns native discovery, validation, synchronization, clocks, conversions and
private delivery. Numeric algorithms and retained history/storage implementations
live in the core; the adapter supplies game-specific policies and pure codecs
for its owned numeric samples. No core operation reads native pointers.

## Module ownership

| Directory | Responsibility |
| --- | --- |
| runtime | Plugin/controller state, FPS enforcement, stage resolution and network-worker repair. |
| hooks | Near allocation and generated x64 callback stubs. |
| clock | Immutable physics-time intervals and render-frame publication. |
| config | Adjacent DAT path and saved FPS selection. |
| menu | FPS choices, preview, Apply and discard. |
| menu/DisplayRefreshRates.h | DXGI display queries shared by menu and runtime through DisplayModes. |
| startup | Feature installation and controller-loop startup. |
| input | Controller map cursor movement, activity detection and conditional height easing. |
| presentation | CPU vehicle, prop, wheel, bone and camera interpolation; ownership discovery; minimap and above-car player tag presentation. |
| presentation/cpu | Ordered CPU state, readability, resource lifetime, pose encoding, binding, capture, delivery and hook implementation headers. |
| presentation/history | Core history aliases, native clock validation and source-light cohort publication. |
| presentation/math | Split-coordinate encoding and padded basis bridges to the portable kernels. |
| presentation/effects | Native vehicle-flare boundary and generic flare projection using the presented CPU camera. |
| presentation/lighting | Native light-source interpolation before the native builder. |
| presentation/shadows | Private CPU-selected shadow-camera inputs and the separate solar-direction timeline. |
| presentation/MinimapHooks.h | Native minimap hook installation. |
| presentation/PlayerTagNative.h | Verified Flash batch layouts, hook signatures and native addresses. |

## Runtime behavior

The FPS menu offers native 30, native 60, unique game-window monitor refresh
rates above 60 and below 1000 supported at the current Windows resolution in
ascending order, then Unlimited, using the existing 1000 FPS cap. DXGI rational
timings are rounded to the nearest whole FPS before deduplication, grouping
119.88/120 and similar fractional variants without changing the display mode.
The game window's monitor selects the DXGI output, desktop mode and available modes;
the current-mode result must match the output's desktop bounds. Rotated outputs
use desktop-oriented dimensions for both the current mode and the mode list.
Unlimited retains that software cap with VSync enabled and leaves
presentation pacing to native VSync. Interpolation activation still uses the
game monitor's refresh ceiling. Native 30/60 and the displayed selection are preserved.
The VSync ceiling follows window moves. Menu rebuilds refresh monitor-specific
choices while pending selections retain their original indices until Apply/discard.
Apply saves the selection in
TheCrew.FramerateUnlocker.dat beside the ASI. Legacy FPS-only DAT files remain
readable. No INI is required.

Controller map cursor movement retains its native 60 Hz response across render
rates and variable frame times, including acceleration and release behavior.
A paired activity fix prevents small high-FPS frame displacements from being
discarded. Mouse input keeps its native path.

When active, cursor world-height easing retains its native 60 Hz response across render
rates. Its fixed 10% step is extended using the render manager's elapsed seconds;
the native activation condition and world-to-screen projection remain in place.

CPU interpolation must install successfully before presentation becomes active.
Startup owns that activation. Shadow, flare, minimap, cursor, menu and light hooks are
optional; a rejected optional target does not stop the core controller loop.
Capture follows the native producer join and validates ownership, native resource
identities, current page protections and the fixed-step clock. Render consumers
receive private frame-owned copies only at verified synchronous consumption
boundaries. Native resource data remains unchanged.

Ownership traversal caches replay every dependency during each lifetime-locked
capture. Readability certificates remain local to that capture. Page queries
use bounded batches, with metadata fallback for cold pages; batch sizes and
index thresholds do not limit object admission.

All supported objects remain eligible for interpolation. Active stationary
objects refresh observation age. Inactive histories expire after both 60 unused
physics ticks and one second, with incremental maintenance and storage
compaction. Confirmed retirement can release histories earlier. Snapshot caps
and identity, finite-data and native-layout guards remain intact.

CPU publication closes native light-source cohorts independently of captured
mesh count. Native light sources interpolate before construction; the game owns
later light derivation, serialization, dirty scheduling and buffer delivery.
There is no later CPU light-correction stage and no D3D interception.

The shadow-builder hook uses the selected CPU camera and lets native code derive
cascades, snapping and receiver data. Solar direction uses its own held-producer
timeline. There is no separate production shadow-camera cohort history.

Vehicle flares pass directly to the native builder within their verified vehicle
call boundary. Generic flare anchors project through the presented CPU camera,
including their camera-dependent depth threshold. There is no flare history
interpolation. Minimap presentation has an independent native pose history.

Above-car player tags retain their native Flash layout, visibility, billboard
basis, scale and lift. The Flash worker associates each owned batch with its
vehicle binding; the final draw builder receives a private position correction
from the same render snapshot as the car. Ambiguous shared batches and invalid
or retired bindings fall back to native inputs. Optional null pose fields are
valid in the supported binding mode.

Clocks use frame generation for render identity, physics tick for simulation identity, and seconds for interval fields. Resource generations, capture IDs and reset epochs remain distinct. Native handles and generated callback offsets preserve their layouts.

The render clock retains its live physics/generation fields at their existing
ABI offsets. Retired data slots remain reserved holes. Legacy player-root capture,
write-only frame stamps, render-active counters and the forwarding-only wheel
writer hook are absent. The wheel resolver caller guard remains active.

## Shared code and tests

Shared code provides native GFx layouts, menu ABI declarations, safe memory
access, coordinated hook chaining and DAT storage. Mutable plugin state remains
independent of AdvancedGraphics, SkipIntro and the XInput proxy.

Historical GPU association, player-root admission, endpoint reconstruction,
shadow-camera fallback, pooled storage and later light-correction implementations
live in tests. They are not included by the shipped ASI. Runtime diagnostics and
benchmark controls are absent; saved captures remain historical evidence.

Run build/build-scripts/build_TheCrew.FramerateUnlocker.bat from the project root.
The canonical build runs independent portable checks and the six regression suites, builds the companion ASIs
and proxy, validates and deploys the exact artifacts, and cleans intermediates.
Preserve native ABI layouts, hook signatures and presentation timing when editing.
Passing regressions does not establish that every gameplay visual issue is resolved.

See the project README, AGENTS.md and tests/README.md for the build workflow and
regression coverage. Historical light-correction fixtures remain under
tests/reference/light-correction.

See [architecture](../../docs/Architecture.md) for CPU composition and test boundaries.

Manual shift commands are retained between render input copies and delivered once
to the next native physics gearbox update above 60 FPS. The native gearbox keeps
its clutch, cooldown and shift acceptance rules. Direct H-shifter inputs and native
30/60 FPS behavior remain native. Pending commands expire after 250 ms and reset
when the vehicle, input source, thread or presentation epoch changes.
