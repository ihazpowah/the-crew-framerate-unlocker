param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('build_TheCrew.FramerateUnlocker.bat', 'build_TheCrew.AdvancedGraphics.bat',
        'build_TheCrew.SkipIntro.bat', 'build_xinput1_3.bat')][string] $ScriptName
)
$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$drive = $null
$result = 1
$previousTemp = $env:TEMP
$previousTmp = $env:TMP
$previousMarker = $env:THECREW_PRIVATE_BUILD
try {
    foreach ($letter in @('Z', 'Y', 'X', 'W', 'V', 'U', 'T')) {
        $candidate = $letter + ':'
        if ([IO.Directory]::Exists($candidate + '\') -or
            (Get-PSDrive -Name $letter -ErrorAction SilentlyContinue)) { continue }
        & subst.exe $candidate $project
        if ($LASTEXITCODE -eq 0) { $drive = $candidate; break }
    }
    if (-not $drive) { throw 'No neutral drive letter is available for the build.' }
    $env:THECREW_PRIVATE_BUILD = '1'
    $env:TEMP = "$drive\build\temporary"
    $env:TMP = $env:TEMP
    [void](New-Item -ItemType Directory -Path $env:TEMP -Force)
    & cmd.exe /c "$drive\build\build-scripts\$ScriptName"
    $result = $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine("PRIVATE BUILD FAILED: $($_.Exception.Message)")
} finally {
    $env:TEMP = $previousTemp
    $env:TMP = $previousTmp
    $env:THECREW_PRIVATE_BUILD = $previousMarker
    if ($drive) {
        & subst.exe $drive /d
        if ($LASTEXITCODE -ne 0) { $result = 1 }
    }
}
exit $result
