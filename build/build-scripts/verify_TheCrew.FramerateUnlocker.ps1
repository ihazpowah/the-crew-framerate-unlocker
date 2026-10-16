param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Preflight', 'Dependency', 'Built', 'Deployed')]
    [string] $Phase,
    [Parameter(Mandatory = $true)]
    [string] $ProjectRoot,
    [string] $MinHookRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ArtifactChecks.ps1')
$destination = 'C:\Games\The Crew\scripts\TheCrew.FramerateUnlocker.asi'
$output = Join-Path $ProjectRoot 'build\FramerateUnlocker\TheCrew.FramerateUnlocker.asi'

function Assert-X64Asi([string] $Path) {
    return Assert-PluginArtifact $Path $ProjectRoot 'TheCrew.FramerateUnlocker.asi' 'TheCrewFramerateUnlocker framerate and vehicle motion plugin for The Crew'
}

try {
    if (-not (Test-Path -LiteralPath $ProjectRoot -PathType Container)) {
        throw "Project root does not exist: $ProjectRoot"
    }
    if ($Phase -eq 'Dependency') {
        if (-not $MinHookRoot) { throw 'MinHookRoot is required for dependency verification.' }
        [void] (Assert-MinHookSources $ProjectRoot $MinHookRoot)
        'MinHook provenance check: PASS'
        exit 0
    }
    if ($Phase -eq 'Preflight') {
        if (Get-Process -Name TheCrew -ErrorAction SilentlyContinue) {
            throw 'Fully close TheCrew.exe before running the build script.'
        }
        foreach ($parent in @('build', 'build\build-scripts', 'source\FramerateUnlocker', 'tests')) {
            $path = Join-Path $ProjectRoot $parent
            if (-not (Test-Path -LiteralPath $path -PathType Container)) {
                throw "Required project directory is missing: $path"
            }
        }
        if (-not (Test-Path -LiteralPath (Split-Path -Parent $destination) -PathType Container)) {
            throw "Deployment directory is missing: $(Split-Path -Parent $destination)"
        }
        foreach ($source in @(
            'source\FramerateUnlocker\TheCrew.FramerateUnlocker.cpp',
            'tests\ShadowCameraTests.cpp', 'tests\ScreenSpaceFlareTests.cpp'
        )) {
            if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot $source) -PathType Leaf)) {
                throw "Required build/check source is missing: $source"
            }
        }
        'Preflight check: PASS'
        exit 0
    }

    $built = Assert-X64Asi $output
    'Built ASI: {0} bytes; timestamp {1:O}; x64 DLL, ASLR/DEP, no W+X sections and version resources verified' -f $built.Length, $built.LastWriteTime
    if ($Phase -eq 'Built') {
        if (-not $MinHookRoot) { throw 'MinHookRoot is required for release provenance.' }
        Write-ReleaseManifest $output $ProjectRoot $MinHookRoot
    }
    if ($Phase -eq 'Deployed') {
        $installed = Assert-X64Asi $destination
        if ($built.Length -ne $installed.Length) { throw 'Built/deployed ASI sizes differ.' }
        if ($built.LastWriteTimeUtc.Ticks -ne $installed.LastWriteTimeUtc.Ticks) {
            throw 'Built/deployed ASI timestamps differ.'
        }
        $builtHash = Get-ArtifactSha256 $output
        $installedHash = Get-ArtifactSha256 $destination
        if ($builtHash -ne $installedHash) { throw 'Built/deployed ASI SHA256 hashes differ.' }
        'Deployed ASI: {0} bytes; timestamp {1:O}' -f $installed.Length, $installed.LastWriteTime
        "Deployment check: PASS - exact filename, size, timestamp and SHA256 match ($builtHash)"
    }
    exit 0
} catch {
    [Console]::Error.WriteLine("CHECK FAILED: $($_.Exception.Message)")
    exit 1
}
