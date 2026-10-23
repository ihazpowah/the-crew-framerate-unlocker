param(
    [Parameter(Mandatory = $true)][string] $ProjectRoot,
    [Parameter(Mandatory = $true)][ValidateSet('SkipIntro', 'AdvancedGraphics', 'XInput')][string] $Plugin,
    [Parameter(Mandatory = $true)][ValidateSet('Preflight', 'Dependency', 'Built', 'Deploy')][string] $Phase,
    [string] $MinHookRoot
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ArtifactChecks.ps1')
. (Join-Path $PSScriptRoot 'DeploymentTargets.ps1')
try {
    if ($Phase -eq 'Dependency') {
        if (-not $MinHookRoot) { throw 'MinHookRoot is required for dependency verification.' }
        [void](Assert-MinHookSources $ProjectRoot $MinHookRoot)
        'MinHook provenance check: PASS'
        exit 0
    }
    if ($Plugin -eq 'SkipIntro') {
        $filename = 'TheCrew.SkipIntro.asi'
        $description = 'TheCrewFramerateUnlocker intro skip plugin for The Crew'
        $source = Join-Path $ProjectRoot "build\SkipIntro\$filename"
    } elseif ($Plugin -eq 'AdvancedGraphics') {
        $filename = 'TheCrew.AdvancedGraphics.asi'
        $description = 'TheCrewFramerateUnlocker advanced graphics plugin for The Crew'
        $source = Join-Path $ProjectRoot "build\AdvancedGraphics\$filename"
    } else {
        $filename = 'xinput1_3.dll'
        $description = 'TheCrewFramerateUnlocker XInput 1.3 proxy and ASI loader'
        $source = Join-Path $ProjectRoot "build\xinput1_3_wrapper\$filename"
    }
    $roots = @(Get-TheCrewDeploymentRoots)
    $destinations = @(Get-TheCrewArtifactDestinations $roots $filename)
    if ($Phase -ne 'Built') { Assert-TheCrewDeploymentRoots $roots }
    if ($Phase -eq 'Preflight') { "$Plugin preflight: PASS"; exit 0 }
    $built = Assert-PluginArtifact $source $ProjectRoot $filename $description
    if ($Phase -eq 'Built') {
        if ($Plugin -in @('SkipIntro', 'AdvancedGraphics')) {
            $obsoleteBuiltIni = Join-Path $ProjectRoot "build\$Plugin\TheCrew.$Plugin.ini"
            if (Test-Path -LiteralPath $obsoleteBuiltIni -PathType Leaf) {
                Remove-Item -LiteralPath $obsoleteBuiltIni
            }
        }
        Write-ReleaseManifest $source $ProjectRoot
        "$Plugin artifact check: PASS"
        exit 0
    }
    foreach ($destination in $destinations) {
        Copy-Item -LiteralPath $source -Destination $destination -Force
        if ($Plugin -in @('SkipIntro', 'AdvancedGraphics')) {
            $obsoleteInstalledIni = Join-Path (Split-Path -Parent $destination) "TheCrew.$Plugin.ini"
            if (Test-Path -LiteralPath $obsoleteInstalledIni -PathType Leaf) {
                Remove-Item -LiteralPath $obsoleteInstalledIni
                "$Plugin obsolete INI: removed"
            }
        }
        $installed = Assert-PluginArtifact $destination $ProjectRoot $filename $description
        if ($built.Length -ne $installed.Length -or
            $built.LastWriteTimeUtc.Ticks -ne $installed.LastWriteTimeUtc.Ticks -or
            (Get-ArtifactSha256 $source) -ne (Get-ArtifactSha256 $destination)) {
            throw 'Built/deployed size, timestamp or SHA256 differs.'
        }
        "$Plugin deployment check: PASS - exact filename, size, timestamp and SHA256 match"
    }
    exit 0
} catch {
    [Console]::Error.WriteLine("PLUGIN CHECK FAILED: $($_.Exception.Message)")
    exit 1
}
