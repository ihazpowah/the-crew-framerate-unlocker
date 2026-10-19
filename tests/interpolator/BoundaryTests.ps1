param([string]$ProjectRoot = (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent))
$ErrorActionPreference='Stop'
$directory=[IO.Path]::GetFullPath((Join-Path $ProjectRoot 'source\interpolator')).TrimEnd('\','/')
$standard=@('algorithm','array','cmath','cstddef','cstdint','cstring','functional','unordered_map','utility','vector')
foreach($file in Get-ChildItem -LiteralPath $directory -Filter *.h) {
    foreach($match in [regex]::Matches([IO.File]::ReadAllText($file.FullName),'(?m)^\s*#include\s*([<"])([^>"\r\n]+)[>"]')) {
        $name=$match.Groups[2].Value
        if($match.Groups[1].Value -eq '<') {
            if($name -notin $standard){throw "Nonportable or unreviewed core include: $name"}
        } else {
            $target=[IO.Path]::GetFullPath((Join-Path $directory $name))
            if(-not $target.StartsWith($directory+'\',[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $target)) {throw "Core dependency escapes portable layer: $name"}
        }
    }
}
'PASS: interpolator includes only C++17 standard headers and its own headers'
