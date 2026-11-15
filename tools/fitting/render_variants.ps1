<#
.SYNOPSIS
Renders one MIDI file through every forte-render in a folder, in parallel, level-matches the
results to one of them, and prints their levels: an A/B set for listening.

.EXAMPLE
tools\fitting\render_variants.ps1 -Midi piece.mid -Renderers C:\tmp\bins -OutDir C:\tmp\piece -Reference current

Each renderer's file name (without .exe) names its WAV. Build one per variant first: change the
model or the IR generator, rebuild the forte_render target, copy the .exe aside.
#>
param(
    [Parameter(Mandatory)] [string]$Midi,
    [Parameter(Mandatory)] [string]$Renderers,
    [Parameter(Mandatory)] [string]$OutDir,
    [Parameter(Mandatory)] [string]$Reference,
    [double]$Tail = 6
)

$tools = $PSScriptRoot
New-Item -ItemType Directory -Force $OutDir | Out-Null
# The jobs start elsewhere, so every path they get must be absolute.
$OutDir = (Resolve-Path $OutDir).Path
$Midi = (Resolve-Path $Midi).Path
$jobs = foreach ($exe in Get-ChildItem "$Renderers\*.exe") {
    Start-Job -ArgumentList $exe.FullName, $Midi, (Join-Path $OutDir "$($exe.BaseName).wav"), $Tail -ScriptBlock {
        param($e, $m, $w, $t)
        & $e $m $w --tail $t | Out-Null
        if ($LASTEXITCODE -ne 0) { "FAILED: $w ($LASTEXITCODE)" }
    }
}
$jobs | Wait-Job | Receive-Job
$jobs | Remove-Job
python (Join-Path $tools "piece_levels.py") $OutDir --match $Reference
