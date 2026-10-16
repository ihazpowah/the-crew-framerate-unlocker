# SkipIntro

TheCrew.SkipIntro.cpp composes the independent TheCrew.SkipIntro.asi for
TheCrewFramerateUnlocker. It does not depend on the framerate ASI.

## Modules and behavior

- IntroSkip.h owns startup movie bypass and includes IntroScreens.h.
- IntroScreens.h handles native GFx intro-screen initialization and updates.
- BootStateHook.h owns the boot-state hook, retained instance tables and bounded startup polling.
- The entry point starts the worker and signals detach without blocking in DllMain.

The worker initializes the boot hook and polls until its startup window expires
or detach is requested. Installed native callbacks remain resident after polling.
Movie handling checks startup eligibility, initialization and decoder activity;
GUI handling preserves the original video entry while skipping static boot screens.

The shared NativeGfx.h and NativeMemory.h modules provide value ownership and
exception-contained memory access. Intro skipping is enabled whenever the ASI
is installed. It reads no INI or DAT settings.

## Build and tests

From the project root, run:

~~~powershell
cmd /c "build\build-scripts\build_TheCrew.SkipIntro.bat"
~~~

The script runs SkipIntroTests against the production entry point, builds the
x64 ASI, validates it and verifies deployment to the configured installation.
Coverage includes boot-state lookup, ABI and exception forwarding, movie
eligibility, once-only seek fallback, deferred release, GFx ownership and
publication rollback.

Successful standalone builds remove their output-directory intermediates and
test executable, retaining the ASI and release manifest. The obsolete
TheCrew.SkipIntro.ini is removed from build and installation locations.
The canonical build also invokes this script and performs final shared cleanup.

The XInput proxy attempts all three ASIs independently. A missing or failed
plugin does not prevent loading the others or forwarding XInput calls.

See the [project README](../../README.md) for toolchain requirements, neutral
build-drive handling, privacy checks and deployment rules, and
[test coverage](../../tests/README.md) for regression details.
