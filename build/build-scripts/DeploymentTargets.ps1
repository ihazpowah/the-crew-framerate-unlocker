function Get-TheCrewDeploymentRoots {
    param([string] $AdditionalGameDirectory = $env:THECREW_ADDITIONAL_GAME_DIR)
    $roots = @((Join-Path $env:SystemDrive 'Games\The Crew'))
    if (-not [string]::IsNullOrWhiteSpace($AdditionalGameDirectory)) {
        if ($AdditionalGameDirectory -notmatch '^[A-Za-z]:[\\/]' -and
            $AdditionalGameDirectory -notmatch '^\\\\[^\\]+\\[^\\]+') {
            throw 'THECREW_ADDITIONAL_GAME_DIR must be an absolute game directory.'
        }
        $additional = [IO.Path]::GetFullPath($AdditionalGameDirectory).TrimEnd('\', '/')
        if ($additional -notin $roots) { $roots += $additional }
    }
    return $roots
}

function Assert-TheCrewDeploymentRoots {
    param([string[]] $Roots)
    if (Get-Process -Name TheCrew -ErrorAction SilentlyContinue) {
        throw 'Fully close TheCrew.exe before building or deploying.'
    }
    foreach ($root in $Roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container) -or
            -not (Test-Path -LiteralPath (Join-Path $root 'TheCrew.exe') -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $root 'scripts') -PathType Container)) {
            throw "Game deployment root must contain TheCrew.exe and scripts: $root"
        }
    }
}

function Get-TheCrewArtifactDestinations {
    param([string[]] $Roots, [string] $Filename)
    if ($Filename -notin @('TheCrew.FramerateUnlocker.asi', 'TheCrew.AdvancedGraphics.asi',
            'TheCrew.SkipIntro.asi', 'xinput1_3.dll')) {
        throw "Unapproved deployment filename: $Filename"
    }
    foreach ($root in $Roots) {
        if ($Filename -eq 'xinput1_3.dll') { Join-Path $root $Filename }
        else { Join-Path (Join-Path $root 'scripts') $Filename }
    }
}
