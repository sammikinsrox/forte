<#
.SYNOPSIS
    Builds Steinberg's VST3 validator from a pinned vst3sdk tag into .tools/vst3-validator/.

.DESCRIPTION
    JUCE's bundled copy of the VST3 SDK leaves the validator out, so it's built from the full
    SDK (MIT). CMake's configure step finds .tools/vst3-validator/validator.exe and passes it to
    pluginval with --vst3validator. CI runs this script too and caches the result.

    Only the submodules the validator needs are fetched (no VSTGUI, docs or tutorials).
    To upgrade, change both values below in one commit.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$Tag    = 'v3.8.1_build_84'
$Commit = '3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96'
$Repo   = 'https://github.com/steinbergmedia/vst3sdk.git'

$root  = Split-Path -Parent $PSScriptRoot
$dest  = Join-Path $root '.tools\vst3-validator'
$exe   = Join-Path $dest 'validator.exe'
$stamp = Join-Path $dest 'version.txt'

if ((Test-Path $exe) -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $Tag)) {
    Write-Host "VST3 validator $Tag already at $exe"
    return
}

function Invoke-Checked {
    param([string]$What, [scriptblock]$Command)
    # Native tools write progress to stderr, which Windows PowerShell 5.1 would turn into a
    # terminating error under 'Stop'. Judge them by exit code instead.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command 2>&1 | ForEach-Object { "$_" } }
    finally { $ErrorActionPreference = $previous }
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit $LASTEXITCODE)" }
}

$work = Join-Path $root ".tools\vst3sdk-build-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
$src  = Join-Path $work 'src'
$bin  = Join-Path $work 'build'

try {
    Write-Host "Fetching vst3sdk $Tag"
    Invoke-Checked 'git clone' { git -c advice.detachedHead=false clone --quiet --depth 1 --branch $Tag $Repo $src }

    $head = (git -C $src rev-parse HEAD).Trim()
    if ($head -ne $Commit) {
        throw "vst3sdk $Tag resolved to $head, expected $Commit. The tag may have moved."
    }

    Invoke-Checked 'git submodule update' {
        git -C $src submodule update --quiet --init --depth 1 base cmake pluginterfaces public.sdk
    }

    Write-Host 'Building the VST3 validator'
    Invoke-Checked 'cmake configure' {
        cmake -S $src -B $bin -G 'Visual Studio 18 2026' -A x64 `
            -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF `
            -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON `
            -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF `
            -DSMTG_CREATE_PLUGIN_LINK=OFF
    }
    Invoke-Checked 'cmake build' { cmake --build $bin --config Release --target validator --parallel }

    $built = Get-ChildItem -Path $bin -Recurse -Filter 'validator.exe' |
        Where-Object { $_.FullName -match '\\Release\\' } |
        Select-Object -First 1
    if (-not $built) { throw 'validator.exe was not produced by the build' }

    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item $built.FullName $exe
    Set-Content -Path $stamp -Value $Tag -Encoding ascii
    Write-Host "VST3 validator $Tag -> $exe"
}
finally {
    if (Test-Path $work) { Remove-Item -Recurse -Force $work }
}
