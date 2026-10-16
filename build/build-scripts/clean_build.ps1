param(
    [Parameter(Mandatory = $true)][string] $ProjectRoot,
    [ValidateSet('All', 'AdvancedGraphics', 'SkipIntro', 'XInput')][string] $Plugin = 'All'
)
$ErrorActionPreference = 'Stop'
try {
    $root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\', '/')
    $build = Join-Path $root 'build'
    # Only known build directories and temporary extensions are eligible.
    $directories = switch ($Plugin) {
        'All' { @('build-scripts', 'FramerateUnlocker', 'FramerateUnlocker\checks',
            'AdvancedGraphics', 'SkipIntro', 'xinput1_3_wrapper',
            'xinput1_3_wrapper\artifact-tests', 'tests', 'temporary') }
        'AdvancedGraphics' { @('AdvancedGraphics') }
        'SkipIntro' { @('SkipIntro') }
        'XInput' { @('xinput1_3_wrapper', 'xinput1_3_wrapper\artifact-tests') }
    }
    $removed = 0
    foreach ($relative in $directories) {
        $directory = [IO.Path]::GetFullPath((Join-Path $build $relative))
        if (-not $directory.StartsWith($build + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Cleanup path outside build directory: $directory"
        }
        if (-not (Test-Path -LiteralPath $directory)) { continue }
        # Reject junctions/symlinks in the entire path before deleting anything.
        $ancestor = Get-Item -LiteralPath $directory
        while ($ancestor) {
            if ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Cleanup refuses reparse point: $($ancestor.FullName)"
            }
            $ancestor = $ancestor.Parent
        }
        $fixtureDirectory = $relative -in @('FramerateUnlocker\checks',
            'xinput1_3_wrapper\artifact-tests', 'tests', 'temporary')
        foreach ($file in Get-ChildItem -LiteralPath $directory -File) {
            $temporary = $file.Extension -in @('.obj', '.lib', '.exp', '.res', '.ilk') -or
                $file.Name -like '*.compile.pdb' -or
                ($relative -ne 'build-scripts' -and $file.Extension -eq '.exe') -or
                ($fixtureDirectory -and $file.Extension -in @('.dll', '.pdb')) -or
                ($relative -eq 'temporary' -and $file.Extension -eq '.tmp')
            if (-not $temporary) { continue }
            if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Cleanup refuses reparse point: $($file.FullName)"
            }
            Remove-Item -LiteralPath $file.FullName -Force
            if (Test-Path -LiteralPath $file.FullName) { throw "Cleanup failed: $($file.FullName)" }
            ++$removed
        }
        if ($fixtureDirectory -and -not (Get-ChildItem -LiteralPath $directory -Force)) {
            Remove-Item -LiteralPath $directory
        }
    }
    "Build cleanup: PASS - removed $removed temporary files ($Plugin)"
    exit 0
} catch {
    [Console]::Error.WriteLine("BUILD CLEANUP FAILED: $($_.Exception.Message)")
    exit 1
}
