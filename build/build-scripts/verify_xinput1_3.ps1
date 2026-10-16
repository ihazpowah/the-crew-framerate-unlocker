param([Parameter(Mandatory = $true)][string] $ProjectRoot)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ArtifactChecks.ps1')
try {
    $path = Join-Path $ProjectRoot 'build\xinput1_3_wrapper\xinput1_3.dll'
    $item = Assert-PluginArtifact $path $ProjectRoot 'xinput1_3.dll' 'TheCrewFramerateUnlocker XInput 1.3 proxy and ASI loader'
    # dumpbin is provided by the same vcvars environment that built this DLL.
    $exports = & dumpbin.exe /nologo /exports $path
    if ($LASTEXITCODE -ne 0) { throw 'dumpbin export verification failed.' }
    $expected = [ordered]@{
        XInputGetState = 2; XInputSetState = 3; XInputGetCapabilities = 4; XInputEnable = 5
        XInputGetDSoundAudioDeviceGuids = 6; XInputGetBatteryInformation = 7
        XInputGetKeystroke = 8; XInputGetStateEx = 100
    }
    $actual = @{}
    foreach ($line in $exports) {
        if ($line -match '^\s+(\d+)\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\w+)\s*$') {
            $actual[$Matches[2]] = [int] $Matches[1]
        }
    }
    if ($actual.Count -ne $expected.Count + 1 -or $actual['DllMain'] -ne 1) { throw 'Unexpected proxy export set.' }
    foreach ($name in $expected.Keys) {
        if ($actual[$name] -ne $expected[$name]) { throw "Missing/changed proxy export: $name" }
    }
    Write-ReleaseManifest $path $ProjectRoot
    'Proxy artifact check: PASS - x64 DLL, ASLR/DEP, no W+X sections, version resources and unchanged exports'
    exit 0
} catch {
    [Console]::Error.WriteLine("PROXY CHECK FAILED: $($_.Exception.Message)")
    exit 1
}
