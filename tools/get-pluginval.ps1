<#
.SYNOPSIS
    Downloads the pinned pluginval release into .tools/pluginval/ and checks its hash.

.DESCRIPTION
    CMake's configure step finds .tools/pluginval/pluginval.exe and registers the
    "pluginval: VST3 at strictness 10" CTest test. CI runs this script too, so local and CI
    runs use the same pluginval. To upgrade, change both values below in one commit.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$Version = 'v1.0.4'
$Sha256  = 'C08E61CE3B96DB41636F8EC7E76F4C7E2C13EBDAC7FA1B5A1F52B4F32EC715AB'
$Url     = "https://github.com/Tracktion/pluginval/releases/download/$Version/pluginval_Windows.zip"

$root    = Split-Path -Parent $PSScriptRoot
$dest    = Join-Path $root '.tools\pluginval'
$exe     = Join-Path $dest 'pluginval.exe'
$stamp   = Join-Path $dest 'version.txt'

if ((Test-Path $exe) -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $Version)) {
    Write-Host "pluginval $Version already at $exe"
    return
}

$zip = Join-Path ([System.IO.Path]::GetTempPath()) "pluginval-$Version-$([guid]::NewGuid()).zip"
try {
    Write-Host "Downloading pluginval $Version"
    Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing

    $actual = (Get-FileHash -Algorithm SHA256 $zip).Hash
    if ($actual -ne $Sha256) {
        throw "pluginval download hash mismatch: expected $Sha256, got $actual"
    }

    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    New-Item -ItemType Directory -Force $dest | Out-Null
    Expand-Archive -Path $zip -DestinationPath $dest

    if (-not (Test-Path $exe)) {
        throw "pluginval.exe not found in the archive"
    }
    Set-Content -Path $stamp -Value $Version -Encoding ascii
    Write-Host "pluginval $Version -> $exe"
}
finally {
    if (Test-Path $zip) { Remove-Item -Force $zip }
}
