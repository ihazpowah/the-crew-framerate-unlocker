# TheCrewFramerateUnlocker

An x64 D3D11 ASI plugin for The Crew that unlocks the framerate and smooths
vehicle, camera and related scene presentation between physics updates.
The project also builds independent intro-skipping and advanced-graphics plugins.

## Plugins and settings

| Artifact | Purpose |
| --- | --- |
| TheCrew.FramerateUnlocker.asi | FPS selection, frame limiting, vehicle and camera interpolation, minimap presentation, lighting, flares and shadow-camera presentation. |
| TheCrew.AdvancedGraphics.asi | Native, 2X and 4X shadow-map and cubemap resolution settings with Apply/discard. |
| TheCrew.SkipIntro.asi | Startup movie and intro-screen skipping. |
| xinput1_3.dll | XInput proxy that independently loads each optional ASI and forwards XInput calls. |

The proxy belongs beside the game executable; ASIs belong in the installation's
scripts directory. Each ASI is optional. A missing or failed plugin does not
prevent the proxy from trying the others or forwarding XInput calls.

The FPS menu offers **30**, **60**, supported Windows primary-display refresh
rates above 60 Hz supported at the current Windows resolution in ascending
order, then **Unlimited**. Fractional timings are rounded to the nearest whole
FPS and grouped (for example, 119.88/120 share one 120 option). Duplicate rates
are shown once; rates at or above the existing 1000 FPS safety cap are omitted.
Display selection, current resolution and mode timings are queried through DXGI.
Unlimited uses a 1000 FPS
cap, including with VSync enabled, so native VSync controls presentation pacing.
The displayed selection stays unchanged.
Native 30/60 retain native presentation; effective limits above 60 enable the
interpolation pipeline. With VSync enabled, activation uses the primary display
refresh ceiling independently of Unlimited's software cap.

Apply saves the FPS selection in TheCrew.FramerateUnlocker.dat beside the ASI.
Legacy FPS-only DAT files remain readable and upgrade on save without changing
the stored FPS. Graphics settings use TheCrew.AdvancedGraphics.dat. Without
saved settings, shadow resolution defaults to 4X and cubemap resolution to
Native. Scaled sun-shadow maps are capped at 4096 per cascade; existing larger
native maps are not reduced.

None of the plugins require an INI. SkipIntro has no settings file.

## Build and deploy

Requires Windows, PowerShell, and Visual Studio with the **Desktop development
with C++** workload and x64 tools. The scripts discover Visual Studio using
vswhere, with fallback locations for Visual Studio 2022 and 2026. The compiler
must support the deterministic compilation and path-mapping options used by
setup_msvc.bat; the current build has been verified with MSVC 19.51 from
Visual Studio 2026.

MinHook sources are included locally. The build verifies them against
[the dependency checksum record](docs/dependencies/MinHook.sources.json).
MINHOOK_ROOT can select another local copy with matching checksums. Dependencies
are not downloaded automatically.

Close TheCrew.exe completely, then run from the project root:

~~~powershell
cmd /c "build\build-scripts\build_TheCrew.FramerateUnlocker.bat"
~~~

The canonical script performs this workflow:

1. Checks the project, installation destination and game process.
2. Initializes the x64 toolchain, verifies MinHook and compiles its C sources.
3. Runs cleanup regression checks and six framerate suites using the same
   MinHook objects as the ASI: ShadowCameraTests, ScreenSpaceFlareTests,
   SceneRenderAnimationTests, MatrixInterpolationTests,
   RepeatedSceneUploadTests and HookSafetyTests.
4. Builds and verifies the framerate ASI, then deploys it.
5. Runs the standalone AdvancedGraphics, SkipIntro and XInput builds, including
   their regressions, artifact checks and verified deployment.
6. Verifies the framerate deployment and removes temporary build files.

Deployment targets the installation configured in the build and verification
scripts. It overwrites only the three ASI filenames listed above and xinput1_3.dll.
The destination directories must already exist. To use a different installation,
update the destination definitions consistently before building.

Artifact checks validate x64 PE32+ DLL structure, ASLR and DEP, absence of
writable/executable sections, version resources and product metadata.
Deployment checks require matching size, timestamp and SHA256. DAT settings are
preserved. Standalone plugin builds remove only their obsolete
TheCrew.SkipIntro.ini and TheCrew.AdvancedGraphics.ini.

Success requires **exit code 0 and BUILD CHECK: PASS**. The main script prompts
for Enter before exiting. On failure, fix the reported stage and rerun the same
command. Deployment is sequential, so a later failure can leave earlier plugins
already updated. Do not replace failed deployment steps with manual copies.

The standalone entry points are build_TheCrew.AdvancedGraphics.bat,
build_TheCrew.SkipIntro.bat and build_xinput1_3.bat in the same script directory.

## Build privacy and automatic cleanup

Every build entry point uses invoke_private_build.ps1 to temporarily map the
project to a free drive letter from Z through T and place temporary files inside
the build directory. Compiler path mapping and a filename-only PDB reference
keep local source locations out of deliverables. Artifact checks reject the
local profile name in binaries and the final framerate PDB. The wrapper restores
the temporary-directory environment and removes its drive mapping on exit,
including after a build failure.

After successful deployment, clean_build.ps1 removes known compiler/linker
intermediates, regression executables, fixture DLLs and the compiler PDB.
The canonical build also cleans shared intermediates and known temporary files.
Empty fixture and temporary directories are removed. Cleanup is restricted to
known build directories and file types and rejects junctions and symlinks.

The following outputs remain in their respective build directories:

- The three final ASIs and xinput1_3.dll.
- A .release.json manifest beside each artifact.
- TheCrew.FramerateUnlocker.pdb for debugging.

Standalone cleanup covers that plugin's output directories. The canonical
build performs the final shared cleanup. A cleanup failure fails the build.
Failed stages retain their remaining files for diagnosis; earlier successful
stages may already have cleaned their own intermediates.

Generated outputs are ignored by Git. Build scripts and verification code remain
eligible for version control.

## Source and regression documentation

| Location | Contents |
| --- | --- |
| [Interpolator](source/interpolator/README.md) | Portable header-only C++17 math, histories, evaluation and indexed sessions. |
| [The Crew adapter](source/FramerateUnlocker/README.md) | Native capture, validation, clocks, hooks and private delivery, composed into one translation unit. |
| [AdvancedGraphics](source/AdvancedGraphics/README.md) | Independent graphics menu and resolution backends. |
| [SkipIntro](source/SkipIntro/README.md) | Independent startup hooks and intro handling. |
| source/shared | GFx ownership, menu ABI, native memory, DAT storage, reusable pools and coordinated hook chaining. |
| source/xinput1_3_wrapper | Optional-plugin loader and XInput forwarding. |
| build/build-scripts | Build entry points, shared toolchain setup, privacy wrapper, artifact verification and cleanup. |
| [Tests](tests/README.md) | Regression coverage and reference helpers. |
| [Architecture](docs/Architecture.md) | CPU composition, module boundaries and native fixture layout. |
| [Project instructions](AGENTS.md) | Build, deployment and gameplay-task rules. |

Passing regressions does not establish that every gameplay visual issue is
resolved. Synthetic CPU timings do not establish measured gameplay FPS or 1% lows.

## License

Original project code is licensed under the [MIT License](LICENSE).
Vendored MinHook and its bundled components retain their own copyright and
license notices in their source files; the project license does not replace them.
