<#
.SYNOPSIS
    Packages a Release build into dist\: the portable zip and the Inno Setup installer (issue 0020).

.DESCRIPTION
    Takes the Release VST3 and standalone app from build\, with LICENSE, THIRD-PARTY.md,
    CREDITS.md, a README with install notes and every third-party licence (copied from the exact
    JUCE sources the build used), and writes
        dist\Forte-<version>-windows-x64.zip
        dist\Forte-<version>-windows-x64-setup.exe
    The version is the one in CMakeLists.txt. Shared by tools\release\make-release.ps1 and the
    release workflow. Needs Inno Setup 6 (winget install JRSoftware.InnoSetup) unless -NoInstaller.
#>
param(
    [string]$BuildDir = "build",
    [string]$DistDir = "dist",
    [switch]$NoInstaller
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root

$cmake = Get-Content CMakeLists.txt -Raw
if ($cmake -notmatch 'project\(Forte VERSION (\d+\.\d+\.\d+)') { throw "No version in CMakeLists.txt" }
$version = $Matches[1]

$vst3 = Join-Path $BuildDir "plugin\Forte_artefacts\Release\VST3\Forte.vst3"
$exe = Join-Path $BuildDir "plugin\Forte_artefacts\Release\Standalone\Forte.exe"
foreach ($p in $vst3, $exe) { if (-not (Test-Path $p)) { throw "Missing ${p}: build Release first" } }

$name = "Forte-$version-windows-x64"
$stage = Join-Path $DistDir $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force (Join-Path $stage "licenses") | Out-Null

Copy-Item -Recurse $vst3 (Join-Path $stage "Forte.vst3")
Copy-Item $exe $stage
Copy-Item LICENSE, THIRD-PARTY.md, CREDITS.md $stage
Copy-Item licenses\* (Join-Path $stage "licenses")

# The vendored libraries' own licence texts, from the JUCE sources the build used.
$juce = "external\JUCE\modules"
$thirdParty = [ordered]@{
    "VST3_SDK-MIT.txt"     = "$juce\juce_audio_processors_headless\format_types\VST3_SDK\LICENSE.txt"
    "FLAC.txt"             = "$juce\juce_audio_formats\codecs\flac\Flac Licence.txt"
    "zlib.txt"             = "$juce\juce_core\zip\zlib\LICENSE"
    "libpng.txt"           = "$juce\juce_graphics\image_formats\pnglib\LICENSE"
    "jpeglib-README.txt"   = "$juce\juce_graphics\image_formats\jpglib\README"
    "libwebp.txt"          = "$juce\juce_graphics\image_formats\libwebp\COPYING"
    "HarfBuzz.txt"         = "$juce\juce_graphics\fonts\harfbuzz\COPYING"
    "SheenBidi.txt"        = "$juce\juce_graphics\unicode\sheenbidi\LICENSE"
    "LunaSVG.txt"          = "$juce\juce_graphics\drawables\lunasvg\LICENSE"
    "PlutoVG.txt"          = "$juce\juce_graphics\drawables\lunasvg\plutovg\LICENSE"
}
foreach ($target in $thirdParty.Keys) {
    $source = $thirdParty[$target]
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing third-party licence $source (JUCE submodule checked out?)" }
    Copy-Item -LiteralPath $source (Join-Path $stage "licenses\$target")
}

$commit = (git rev-parse --short HEAD).Trim()
$readme = @"
Forte $version for Windows x64 (built from commit $commit)
https://github.com/sammikinsrox/forte

INSTALL
  Installer: run Forte-$version-windows-x64-setup.exe. It puts the VST3 plugin in
  C:\Program Files\Common Files\VST3\Forte.vst3 and the standalone app in C:\Program Files\Forte,
  and can be uninstalled from Windows Settings > Apps. Your presets (Documents\Forte\Presets)
  are never touched.

  Portable zip: copy the Forte.vst3 folder into C:\Program Files\Common Files\VST3\ and run
  Forte.exe from anywhere. Then rescan plugins in your host.

  Nothing else is needed: the C++ runtime is built in.

WINDOWS SMARTSCREEN
  Forte isn't code-signed yet, so the first time you run the installer or app Windows may say it
  "protected your PC". Click "More info", then "Run anyway". (Where Smart App Control is
  enforcing, unsigned software is blocked with no override.)

LICENCE
  Forte is free software under the GNU General Public License v3 or later (LICENSE). Third-party
  software and fonts: THIRD-PARTY.md and the licenses folder. Credits: CREDITS.md. The complete
  source is the repository above at this version's tag, cloned with --recursive.

VST is a registered trademark of Steinberg Media Technologies GmbH.
"@
[IO.File]::WriteAllText((Join-Path (Resolve-Path $stage) "README.txt"), $readme.Replace("`r`n", "`n").Replace("`n", "`r`n"))

$zip = Join-Path $DistDir "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip
Write-Host "wrote $zip"

if (-not $NoInstaller) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
              "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw "Inno Setup 6 not found: winget install JRSoftware.InnoSetup (or pass -NoInstaller)" }
    & $iscc /Q "/DAppVersion=$version" "/DSourceDir=$(Resolve-Path $stage)" "/DOutputDir=$(Resolve-Path $DistDir)" installer\forte.iss
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed ($LASTEXITCODE)" }
    Write-Host "wrote $(Join-Path $DistDir "$name-setup.exe")"
}

Write-Output $version
