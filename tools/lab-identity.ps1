# Voicing lab gate (SPEC §6.5): with every Lab control at 50, renders must be bit-identical to
# the committed voicing's. Renders the two reference pieces with the current forte-render and
# compares them byte for byte with the references in build/lab/ref (made once with dabb951's
# forte-render, the same configuration).
#
#   powershell -File tools/lab-identity.ps1 [-Config Debug|Release] [-Voicing <voicing.json>]
#
# With -Voicing, the current build renders that setting instead (for checking a fold).

param(
    [ValidateSet("Debug", "Release")] [string] $Config = "Debug",
    [string] $Voicing = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "build\tools\forte-render\forte_render_artefacts\$Config\forte-render.exe"
$refDir = Join-Path $root "build\lab\ref"
$outDir = Join-Path $root "build\lab\check"
New-Item -ItemType Directory -Force $outDir | Out-Null

$suffix = if ($Config -eq "Debug") { "" } else { "-release" }
$pieces = @(
    @{ Name = "autleave"; Midi = "build\ab\autleave\autleave-clean.mid" },
    @{ Name = "chopin";   Midi = "build\ab\chopin\midi\chopin-prelude.mid" }
)

$failed = $false
foreach ($piece in $pieces) {
    $reference = Join-Path $refDir "$($piece.Name)$suffix.wav"
    $output = Join-Path $outDir "$($piece.Name)$suffix.wav"
    $arguments = @((Join-Path $root $piece.Midi), $output)
    if ($Voicing) { $arguments += @("--voicing", $Voicing) }
    & $exe @arguments | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "forte-render failed on $($piece.Name)" }

    $same = (Get-FileHash $reference).Hash -eq (Get-FileHash $output).Hash
    "{0,-9} {1}" -f $piece.Name, $(if ($same) { "identical" } else { "DIFFERS" })
    if (-not $same) { $failed = $true }
}

if ($failed) { exit 1 }
