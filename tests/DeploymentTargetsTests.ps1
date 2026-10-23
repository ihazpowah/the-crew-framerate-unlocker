param([Parameter(Mandatory = $true)][string] $ProjectRoot)
$ErrorActionPreference = 'Stop'
. (Join-Path $ProjectRoot 'build\build-scripts\DeploymentTargets.ps1')

function Assert-Rejected([scriptblock] $Action) {
    $rejected = $false
    try { & $Action } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid deployment configuration was accepted.' }
}

$fixture = [IO.Path]::GetFullPath((Join-Path $ProjectRoot ('build\deployment-tests-' + [Guid]::NewGuid().ToString('N'))))
try {
    $roots = @(Get-TheCrewDeploymentRoots '')
    $defaultRoot = Join-Path $env:SystemDrive 'Games\The Crew'
    if ($roots.Count -ne 1 -or $roots[0] -ne $defaultRoot) { throw 'Default destination changed.' }
    $secondRoot = Join-Path $fixture 'second install'
    $roots = @(Get-TheCrewDeploymentRoots ($secondRoot + '\'))
    if ($roots.Count -ne 2 -or $roots[1] -ne $secondRoot) { throw 'Additional destination missing.' }
    if (@(Get-TheCrewDeploymentRoots ($defaultRoot.ToLowerInvariant() + '\')).Count -ne 1) { throw 'Duplicate destination.' }
    foreach ($invalid in @('relative\game', ($env:SystemDrive + 'relative'), '\root-relative')) {
        Assert-Rejected { Get-TheCrewDeploymentRoots $invalid }
    }
    Assert-Rejected { Get-TheCrewArtifactDestinations $roots 'other.asi' }
    Assert-Rejected { Get-TheCrewArtifactDestinations $roots 'TheCrew.FramerateUnlocker.dat' }
    foreach ($filename in @('TheCrew.FramerateUnlocker.asi', 'TheCrew.AdvancedGraphics.asi',
            'TheCrew.SkipIntro.asi', 'xinput1_3.dll')) {
        $destinations = @(Get-TheCrewArtifactDestinations $roots $filename)
        if ($destinations.Count -ne 2) { throw 'Artifact did not target both installs.' }
        foreach ($index in 0..1) {
            $parent = $roots[$index]
            if ($filename -ne 'xinput1_3.dll') { $parent = Join-Path $parent 'scripts' }
            if ($destinations[$index] -ne (Join-Path $parent $filename)) { throw 'Incorrect artifact destination.' }
        }
    }
    # Isolate process state while testing directory requirements; production uses Get-Process.
    function Get-Process { param($Name, $ErrorAction) return $null }
    Assert-Rejected { Assert-TheCrewDeploymentRoots @($fixture) }
    [void](New-Item -ItemType Directory -Path $fixture)
    Assert-Rejected { Assert-TheCrewDeploymentRoots @($fixture) }
    [void](New-Item -ItemType File -Path (Join-Path $fixture 'TheCrew.exe'))
    Assert-Rejected { Assert-TheCrewDeploymentRoots @($fixture) }
    [void](New-Item -ItemType Directory -Path (Join-Path $fixture 'scripts'))
    Assert-TheCrewDeploymentRoots @($fixture)
    function Get-Process { param($Name, $ErrorAction) return @{ Id = 1 } }
    Assert-Rejected { Assert-TheCrewDeploymentRoots @($fixture) }
    'Deployment targets tests: PASS'
} finally {
    if (Test-Path -LiteralPath $fixture) {
        # The generated fixture is a direct child of this workspace build directory.
        $resolved = [IO.Path]::GetFullPath($fixture)
        $buildRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot 'build'))
        if ((Split-Path -Parent $resolved) -ne $buildRoot) { throw 'Unsafe fixture cleanup path.' }
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
