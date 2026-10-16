param([Parameter(Mandatory = $true)][string] $ProjectRoot)
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\', '/')
$fixture = Join-Path $workspace ('build\cleanup-test-' + [guid]::NewGuid().ToString('N'))
if (-not $fixture.StartsWith($workspace + '\build\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Invalid cleanup fixture path'
}
try {
    $temporary = @('build-scripts\fixture.obj', 'build-scripts\fixture.res',
        'FramerateUnlocker\TheCrew.FramerateUnlocker.compile.pdb',
        'FramerateUnlocker\checks\fixture.dll', 'FramerateUnlocker\checks\fixture.exe',
        'AdvancedGraphics\fixture.obj', 'AdvancedGraphics\fixture.exe',
        'SkipIntro\fixture.res', 'SkipIntro\fixture.exe',
        'xinput1_3_wrapper\fixture.obj', 'xinput1_3_wrapper\fixture.exe',
        'xinput1_3_wrapper\artifact-tests\invalid.dll', 'tests\fixture.lib')
    $retained = @('FramerateUnlocker\TheCrew.FramerateUnlocker.asi',
        'FramerateUnlocker\TheCrew.FramerateUnlocker.pdb',
        'FramerateUnlocker\TheCrew.FramerateUnlocker.asi.release.json',
        'AdvancedGraphics\TheCrew.AdvancedGraphics.asi', 'SkipIntro\TheCrew.SkipIntro.asi',
        'xinput1_3_wrapper\xinput1_3.dll', 'build-scripts\build.bat',
        'FramerateUnlocker\notes.txt', 'unrelated\keep.obj')
    foreach ($relative in $temporary + $retained) {
        $path = Join-Path "$fixture\build" $relative
        [void](New-Item -ItemType Directory -Path (Split-Path $path) -Force)
        [IO.File]::WriteAllText($path, 'sentinel')
    }
    foreach ($plugin in @('AdvancedGraphics', 'SkipIntro', 'XInput', 'All', 'All')) {
        & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (
            Join-Path $workspace 'build\build-scripts\clean_build.ps1') -ProjectRoot $fixture -Plugin $plugin
        if ($LASTEXITCODE -ne 0) { throw "Cleanup failed for $plugin" }
        if ($plugin -ne 'All' -and -not (Test-Path -LiteralPath "$fixture\build\build-scripts\fixture.obj")) {
            throw 'Standalone cleanup removed parent-build intermediates'
        }
        foreach ($relative in $retained) {
            if ((Get-Content -LiteralPath (Join-Path "$fixture\build" $relative) -Raw) -ne 'sentinel') {
                throw "Cleanup changed retained file: $relative"
            }
        }
    }
    foreach ($relative in $temporary) {
        if (Test-Path -LiteralPath (Join-Path "$fixture\build" $relative)) {
            throw "Temporary file remains: $relative"
        }
    }
    'PASS: build cleanup preserves deliverables and unrelated files, scopes standalone cleanup, and is repeatable'
} finally {
    if (Test-Path -LiteralPath $fixture) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
