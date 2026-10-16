# AdvancedGraphics

TheCrew.AdvancedGraphics.cpp builds the independent TheCrew.AdvancedGraphics.asi.
It does not depend on the framerate ASI being installed.

## Modules

| Module | Responsibility |
| --- | --- |
| ShadowMapResolution.h | Validated sun-shadow builder hook, Native/2X/4X scaling and three-cascade atlas limits. |
| CubemapResolution.h | Reflection frame scope and matching resource/viewport resolution hooks. |
| NativeMenu.h | Plugin-local hook pointers and menu state, using the shared native menu ABI. |
| AdvancedGraphicsMenu.h | Resolution rows, choice creation, staged settings and DAT persistence. |
| MenuHooks.h | First-open rendering, native event isolation, preview, Apply and discard. |

The plugin reads its own adjacent TheCrew.AdvancedGraphics.dat. Missing settings
default to 4X shadows and Native cubemaps. Apply persists selections atomically.
Scaled sun-shadow maps are limited to 4096 per cascade; larger native sizes are
left unchanged. No INI or another plugin's DAT is read.

Shared GFx ownership, native menu declarations, profile lookup, safe memory
access and settings storage live in the shared modules. Hook coordination
serializes installation and preserves original native callers across independent
MinHook instances. Hook pointers and mutable menu state remain plugin-local.

## Build and tests

From the project root, run:

~~~powershell
cmd /c "build\build-scripts\build_TheCrew.AdvancedGraphics.bat"
~~~

The script verifies MinHook provenance, runs AdvancedGraphicsTests, builds the
ASI, validates it and verifies deployment to the configured installation.
The tests cover shadow and cubemap scaling, hook rollback, first-open rendering,
staged settings, Apply/discard, preview restoration and persistence failures.

Successful standalone builds remove their output-directory intermediates and
test executable, retaining the ASI and release manifest. DAT files are preserved;
only the obsolete TheCrew.AdvancedGraphics.ini is removed from build and
installation locations. The canonical build also runs this script and performs
final shared cleanup.

See the [project README](../../README.md) for toolchain requirements, neutral
build-drive handling, privacy checks and deployment rules, and
[test coverage](../../tests/README.md) for regression details.
