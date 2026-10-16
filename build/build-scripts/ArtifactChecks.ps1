# Shared artifact checks used by plugin/proxy verifiers and regression fixtures.
function Get-ArtifactSha256([string] $Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Assert-NoPersonalBuildPath([string] $Path) {
    $account = Split-Path -Leaf $env:USERPROFILE
    if (-not $account) { throw 'Cannot determine the local profile name for artifact privacy checks.' }
    $bytes = [IO.File]::ReadAllBytes($Path)
    foreach ($encoding in @([Text.Encoding]::ASCII, [Text.Encoding]::Unicode)) {
        if ($encoding.GetString($bytes).IndexOf($account, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "Personal profile name embedded in artifact: $([IO.Path]::GetFileName($Path))"
        }
    }
}

function Assert-PluginArtifact([string] $Path, [string] $ProjectRoot, [string] $Filename, [string] $Description) {
    $item = Get-Item -LiteralPath $Path
    $bytes = [System.IO.File]::ReadAllBytes($item.FullName)
    if ($bytes.Length -lt 64 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5a4d) { throw "Missing/truncated MZ header: $Path" }
    $pe = [BitConverter]::ToUInt32($bytes, 0x3c)
    if ($pe -gt $bytes.Length - 264) { throw "Invalid PE header offset: $Path" }
    if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes, $pe + 4) -ne 0x8664 -or
        ([BitConverter]::ToUInt16($bytes, $pe + 22) -band 0x2000) -eq 0 -or
        [BitConverter]::ToUInt16($bytes, $pe + 24) -ne 0x20b) { throw "Expected x64 PE32+ DLL: $Path" }
    $optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
    if ($optionalSize -lt 240) { throw "Truncated PE32+ optional header: $Path" }
    $flags = [BitConverter]::ToUInt16($bytes, $pe + 24 + 70)
    if (($flags -band 0x160) -ne 0x160) { throw "Missing ASLR, high-entropy ASLR or DEP: $Path" }
    $sections = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $table = $pe + 24 + $optionalSize
    if ($sections -eq 0 -or $table + $sections * 40 -gt $bytes.Length) { throw "Invalid PE section table: $Path" }
    for ($index = 0; $index -lt $sections; ++$index) {
        $characteristics = [BitConverter]::ToUInt32($bytes, $table + $index * 40 + 36)
        if (($characteristics -band 0x20000000) -ne 0 -and ($characteristics -band 2147483648L) -ne 0) {
            throw "Writable/executable PE section: $Path"
        }
    }
    $versionHeader = Get-Content -LiteralPath (Join-Path $ProjectRoot 'source\ReleaseVersion.h') -Raw
    if ($versionHeader -notmatch '#define PRODUCT_VERSION_STRING "([0-9.]+)"') { throw 'Missing release version.' }
    $expectedVersion = $Matches[1]
    $version = $item.VersionInfo
    $numericVersion = '{0}.{1}.{2}.{3}' -f $version.FileMajorPart, $version.FileMinorPart, $version.FileBuildPart, $version.FilePrivatePart
    $numericProduct = '{0}.{1}.{2}.{3}' -f $version.ProductMajorPart, $version.ProductMinorPart, $version.ProductBuildPart, $version.ProductPrivatePart
    if ($version.FileVersion -ne $expectedVersion -or $version.ProductVersion -ne $expectedVersion -or
        $numericVersion -ne $expectedVersion -or $numericProduct -ne $expectedVersion -or
        $version.ProductName -ne 'TheCrewFramerateUnlocker' -or $version.OriginalFilename -ne $Filename -or
        $version.FileDescription -ne $Description) { throw "Missing or inconsistent version resources: $Path" }
    Assert-NoPersonalBuildPath $Path
    return $item
}

function Write-ReleaseManifest([string] $ArtifactPath, [string] $ProjectRoot, [string] $DependencyRoot = '') {
    $artifact = Get-Item -LiteralPath $ArtifactPath
    $symbols = [IO.Path]::ChangeExtension($ArtifactPath, '.pdb')
    if (Test-Path -LiteralPath $symbols -PathType Leaf) { Assert-NoPersonalBuildPath $symbols }
    $manifest = [ordered]@{
        product = 'TheCrewFramerateUnlocker'; version = $artifact.VersionInfo.FileVersion
        artifact = $artifact.Name; bytes = $artifact.Length
        sha256 = Get-ArtifactSha256 $artifact.FullName
        dependencies = @()
    }
    if ($DependencyRoot) {
        $manifest.dependencies = @(Assert-MinHookSources $ProjectRoot $DependencyRoot)
        $manifest['dependencyLicense'] = 'MinHook/include/MinHook.h'
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath ($ArtifactPath + '.release.json') -Encoding UTF8
    Write-Host "Release SHA256 ($($artifact.Name)): $($manifest.sha256)"
}

function Assert-MinHookSources([string] $ProjectRoot, [string] $DependencyRoot) {
    $expected = Get-Content -LiteralPath (Join-Path $ProjectRoot 'docs\dependencies\MinHook.sources.json') -Raw | ConvertFrom-Json
    foreach ($entry in $expected) {
        $path = Join-Path $DependencyRoot $entry.path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-ArtifactSha256 $path) -ne $entry.sha256) {
            throw "MinHook provenance mismatch: $path. Use the recorded sources or deliberately update the provenance snapshot after reviewing the dependency."
        }
    }
    return $expected
}
