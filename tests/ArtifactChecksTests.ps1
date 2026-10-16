param([Parameter(Mandatory = $true)][string] $ProjectRoot)
$ErrorActionPreference = 'Stop'
. (Join-Path $ProjectRoot 'build\build-scripts\ArtifactChecks.ps1')
$source = Join-Path $ProjectRoot 'build\xinput1_3_wrapper\xinput1_3.dll'
$directory = Join-Path $ProjectRoot 'build\xinput1_3_wrapper\artifact-tests'
[void] (New-Item -ItemType Directory -Path $directory -Force)
try {
    foreach ($case in @('aslr', 'high-entropy', 'dep', 'wx', 'version', 'sections', 'truncated')) {
        $bytes = [IO.File]::ReadAllBytes($source)
        $pe = [BitConverter]::ToInt32($bytes, 0x3c)
        $table = $pe + 24 + [BitConverter]::ToUInt16($bytes, $pe + 20)
        switch ($case) {
            'aslr' { $bytes[$pe + 24 + 70] = $bytes[$pe + 24 + 70] -band 0xbf; $message = 'Missing ASLR' }
            'high-entropy' { $bytes[$pe + 24 + 70] = $bytes[$pe + 24 + 70] -band 0xdf; $message = 'Missing ASLR' }
            'dep' { $bytes[$pe + 24 + 71] = $bytes[$pe + 24 + 71] -band 0xfe; $message = 'Missing ASLR' }
            'wx' { $bytes[$table + 39] = $bytes[$table + 39] -bor 0xa0; $message = 'Writable/executable' }
            'version' { [Array]::Clear($bytes, $pe + 24 + 112 + 2 * 8, 8); $message = 'version resources' }
            'sections' { $bytes[$pe + 6] = 0xff; $bytes[$pe + 7] = 0xff; $message = 'section table' }
            'truncated' { $bytes = [byte[]]@(0x4d, 0x5a); $message = 'MZ header' }
        }
        $path = Join-Path $directory ($case + '.dll')
        [IO.File]::WriteAllBytes($path, $bytes)
        $rejected = $false
        try { [void] (Assert-PluginArtifact $path $ProjectRoot 'xinput1_3.dll' 'TheCrewFramerateUnlocker XInput 1.3 proxy and ASI loader') }
        catch {
            if ($_.Exception.Message -notlike "*$message*") { throw }
            $rejected = $true
        }
        if (-not $rejected) { throw "Artifact verifier accepted invalid $case fixture." }
        Remove-Item -LiteralPath $path
    }
    'PASS: artifact verifier rejects missing ASLR, W+X sections, missing version resources, invalid sections and truncated PE'
    exit 0
} catch {
    [Console]::Error.WriteLine("ARTIFACT TEST FAILED: $($_.Exception.Message)")
    exit 1
}
