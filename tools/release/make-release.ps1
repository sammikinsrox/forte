<#
.SYNOPSIS
    Makes a draft GitHub Release of the current commit from this PC (issue 0020).

.DESCRIPTION
    1. Checks the working tree is clean and the commit is pushed, and that v<version> (from
       CMakeLists.txt) isn't released yet.
    2. Configures and builds Release, runs every test, pluginval included (close Reaper first:
       it locks the VST3 in build\).
    3. Packages the zip and the installer (tools\release\package.ps1).
    4. The Linux half in WSL (tools/release/build-linux.sh): builds, tests and packages the
       Linux tarball. Needs WSL with the packages README.md lists; -NoLinux skips it.
    5. Creates a DRAFT release v<version> on GitHub at this commit with all of them attached. A
       draft is visible only to the repository's owner and collaborators; nothing is public, and
       the tag isn't created, until you press Publish on GitHub.

    Bump the version in CMakeLists.txt (0.9.0, 0.9.1, ...) and commit before each release.
    The release workflow (.github/workflows/release.yml) does the same from a pushed tag.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\release\make-release.ps1
#>
param(
    [switch]$SkipTests,
    [switch]$NoLinux,
    [string]$WslDistro = "Ubuntu-24.04"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root

function Run([string]$what, [scriptblock]$block) {
    Write-Host "== $what"
    & $block
    if ($LASTEXITCODE -ne 0) { throw "$what failed ($LASTEXITCODE)" }
}

# 1. What's being released
if (git status --porcelain) { throw "The working tree has changes: commit them first" }
$sha = (git rev-parse HEAD).Trim()
git fetch --quiet origin
if (-not (git branch -r --contains $sha)) { throw "Commit $sha isn't pushed: push it first" }
$cmake = Get-Content CMakeLists.txt -Raw
if ($cmake -notmatch 'project\(Forte VERSION (\d+\.\d+\.\d+)') { throw "No version in CMakeLists.txt" }
$version = $Matches[1]
$tag = "v$version"

# Native commands whose failure is an answer, not an error (Windows PowerShell turns their
# stderr into a terminating error under "Stop").
function Quietly([scriptblock]$block) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $out = & $block 2>$null; $ok = ($LASTEXITCODE -eq 0) } finally { $ErrorActionPreference = $saved }
    $global:LASTEXITCODE = 0
    return @{ ok = $ok; out = $out }
}

if ((Quietly { gh release view $tag }).ok) { throw "Release $tag already exists: bump the version in CMakeLists.txt" }
Write-Host "Releasing Forte $version from $sha"

# 2. Build and test
Run "configure" { cmake --preset vs2026 }
Run "build Release" { cmake --build --preset release -- /v:q /nologo }
if (-not $SkipTests) { Run "tests (pluginval included)" { ctest --preset release } }

# 3. Package
$out = & (Join-Path $PSScriptRoot "package.ps1")
$name = "Forte-$version-windows-x64"
$assets = @("dist\$name-setup.exe", "dist\$name.zip")

# 4. The Linux half, in WSL, on this same checkout
if (-not $NoLinux) {
    $wslRoot = "/mnt/" + $root.Path.Substring(0, 1).ToLower() + $root.Path.Substring(2).Replace("\", "/")
    $linuxArgs = if ($SkipTests) { "--skip-tests" } else { "" }
    Run "Linux build, tests and package (WSL $WslDistro)" { wsl.exe -d $WslDistro -- bash "$wslRoot/tools/release/build-linux.sh" $linuxArgs }
    $assets += "dist\Forte-$version-linux-x64.tar.gz"
}

# 5. Draft release
$described = Quietly { git describe --tags --abbrev=0 }
$previous = if ($described.ok) { "$($described.out)".Trim() } else { $null }
$range = if ($previous) { "$previous..$sha" } else { "-15 $sha" }
$log = (git log --format="- %s" $range.Split(" ")) -join "`n"
$notes = @"
Forte $version for Windows x64$(if (-not $NoLinux) { " and Linux x86-64" })$(if ($version.StartsWith("0.")) { ", a preview build for experimenting" }).

**Windows:** run ``$name-setup.exe`` (VST3 to ``C:\Program Files\Common Files\VST3``, the standalone app to ``C:\Program Files\Forte``), or unzip the portable zip and copy ``Forte.vst3`` there yourself. Forte isn't code-signed yet: if Windows SmartScreen stops it, choose *More info*, then *Run anyway*.
$(if (-not $NoLinux) { "
**Linux:** unpack ``Forte-$version-linux-x64.tar.gz`` and run ``./install.sh``: the VST3 to ``~/.vst3``, the LV2 to ``~/.lv2`` and the standalone app to ``~/.local/bin``, no root needed (``--system`` for every user, ``--uninstall`` to remove). Needs glibc 2.38 or newer.
" })
Built from $sha; every test passed, pluginval at strictness 10 included$(if (-not $NoLinux) { " (VST3 on both, and LV2 on Linux)" }).

**Changes:**
$log

VST is a registered trademark of Steinberg Media Technologies GmbH.
"@
$notesFile = Join-Path $env:TEMP "forte-release-notes.md"
[IO.File]::WriteAllText($notesFile, $notes)
$flags = @("--draft", "--target", $sha, "--title", "Forte $version", "--notes-file", $notesFile)
if ($version.StartsWith("0.")) { $flags += "--prerelease" }
Run "draft release $tag" { gh release create $tag @flags @assets }
Write-Host "Draft $tag is on GitHub (only you can see it): gh release view $tag --web"
