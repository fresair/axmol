# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
# Read-only Android SDK/NDK discovery shared by setup and binding generation.

function Get-AndroidNdkRevision([string]$Root) {
    if (-not $Root) { return $null }
    $properties = Join-Path $Root 'source.properties'
    if (Test-Path $properties -PathType Leaf) {
        $match = Select-String -LiteralPath $properties -Pattern '^\s*Pkg\.Revision\s*=\s*([^#\r\n]+?)\s*$' | Select-Object -First 1
        if ($match) { return $match.Matches[0].Groups[1].Value }
    }
    return $null
}

function Get-AndroidNdkRequirement([string]$Version, [switch]$Exact) {
    $allowNewer = $Version.EndsWith('+')
    $versionText = $Version.TrimEnd('+')
    # Exact r27d is a JS boundary requirement; ordinary Axmol keeps 27.3 prefix matching.
    if ($Exact -and $versionText -eq 'r27d' -and -not $allowNewer) { return '27.3.13750724' }
    if ($versionText -match '^r(\d+)([a-z])?$') {
        $minor = if ($Matches[2]) { [int][char]$Matches[2] - [int][char]'a' } else { 0 }
        return "$($Matches[1]).$minor"
    }
    if ($versionText -match '^\d+\.\d+(\.\d+)?$') { return $versionText }
    throw "Invalid Android NDK version: $Version"
}

function Find-AndroidNdk([string]$SdkRoot, [string]$Version, [switch]$Exact) {
    $requirement = Get-AndroidNdkRequirement $Version -Exact:$Exact
    $allowNewer = $Version.EndsWith('+')
    $candidates = foreach ($item in Get-ChildItem (Join-Path $SdkRoot 'ndk') -Directory -ErrorAction SilentlyContinue) {
        $revision = Get-AndroidNdkRevision $item.FullName
        if (-not $revision) { continue }
        $numericRevision = [regex]::Match($revision, '^\d+\.\d+(\.\d+)?').Value
        if (-not $numericRevision) { continue }
        $matchesVersion = if ($Exact) { $revision -eq $requirement }
            elseif ($allowNewer) { [version]$numericRevision -ge [version]$requirement }
            else { $revision.StartsWith($requirement) }
        if ($matchesVersion) { [pscustomobject]@{ Path = $item.FullName; Revision = [version]$numericRevision } }
    }
    $selected = $candidates | Sort-Object Revision -Descending | Select-Object -First 1
    if ($selected) { return $selected.Path }
    return $null
}
