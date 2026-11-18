#!/usr/bin/env bash
# Packages a Linux Release build into dist/ (issue 0020, SPEC §9.4): the Linux twin of
# tools/release/package.ps1. Takes the Release VST3, LV2 and standalone app from build-linux/,
# with LICENSE, THIRD-PARTY.md, CREDITS.md, a README with install notes, install.sh and every
# third-party licence (copied from the exact JUCE sources the build used), and writes
#     dist/Forte-<version>-linux-x64.tar.gz
# The version is the one in CMakeLists.txt. Used by tools/release/make-release.ps1 (through WSL)
# and the release workflow.
#
#     tools/release/package.sh [build-dir] [dist-dir]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${root}"
build="${1:-build-linux}"
dist="${2:-dist}"

version="$(sed -nE 's/^project\(Forte VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)"
[[ -n "${version}" ]] || { echo "No version in CMakeLists.txt" >&2; exit 1; }

artefacts="${build}/plugin/Forte_artefacts/Release"
vst3="${artefacts}/VST3/Forte.vst3"
lv2="${artefacts}/LV2/Forte.lv2"
app="${artefacts}/Standalone/Forte"
for p in "${vst3}" "${lv2}" "${app}"; do
    [[ -e "${p}" ]] || { echo "Missing ${p}: build linux-release first" >&2; exit 1; }
done

name="Forte-${version}-linux-x64"
stage="${dist}/${name}"
rm -rf "${stage}"
mkdir -p "${stage}/licenses"

cp -R "${vst3}" "${stage}/Forte.vst3"
cp -R "${lv2}" "${stage}/Forte.lv2"
cp "${app}" "${stage}/Forte"
strip --strip-unneeded "${stage}/Forte" "${stage}/Forte.vst3/Contents/x86_64-linux/Forte.so" "${stage}/Forte.lv2/"*.so
cp LICENSE THIRD-PARTY.md CREDITS.md "${stage}/"
cp licenses/* "${stage}/licenses/"
cp tools/release/install.sh "${stage}/install.sh"
chmod +x "${stage}/install.sh" "${stage}/Forte"

# The vendored libraries' own licence texts, from the JUCE sources the build used.
juce="external/JUCE/modules"
while IFS='|' read -r target source; do
    [[ -f "${source}" ]] || { echo "Missing third-party licence ${source} (JUCE submodule checked out?)" >&2; exit 1; }
    cp "${source}" "${stage}/licenses/${target}"
done <<EOF
VST3_SDK-MIT.txt|${juce}/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt
FLAC.txt|${juce}/juce_audio_formats/codecs/flac/Flac Licence.txt
zlib.txt|${juce}/juce_core/zip/zlib/LICENSE
libpng.txt|${juce}/juce_graphics/image_formats/pnglib/LICENSE
jpeglib-README.txt|${juce}/juce_graphics/image_formats/jpglib/README
libwebp.txt|${juce}/juce_graphics/image_formats/libwebp/COPYING
HarfBuzz.txt|${juce}/juce_graphics/fonts/harfbuzz/COPYING
SheenBidi.txt|${juce}/juce_graphics/unicode/sheenbidi/LICENSE
LunaSVG.txt|${juce}/juce_graphics/drawables/lunasvg/LICENSE
PlutoVG.txt|${juce}/juce_graphics/drawables/lunasvg/plutovg/LICENSE
LV2-ISC.txt|${juce}/juce_audio_processors_headless/format_types/LV2_SDK/lv2/COPYING
EOF

commit="$(git rev-parse --short HEAD)"
cat > "${stage}/README.txt" <<EOF
Forte ${version} for Linux x86-64 (built from commit ${commit})
https://github.com/sammikinsrox/forte

INSTALL
  Run ./install.sh. It copies the VST3 plugin to ~/.vst3/Forte.vst3, the LV2 plugin to
  ~/.lv2/Forte.lv2 and the standalone app to ~/.local/bin/Forte, for your user only, with no
  root needed. Then rescan plugins in your host. ./install.sh --system installs for every user
  (/usr/local/lib/vst3, /usr/local/lib/lv2, /usr/local/bin), with sudo. ./install.sh --uninstall
  (with --system if that's how you installed) removes them again. Your presets
  (~/Documents/Forte/Presets, or your XDG Documents folder) are never touched.

  Or by hand: copy Forte.vst3 into ~/.vst3/ and Forte.lv2 into ~/.lv2/.

  Reaper, Bitwig and Ardour load the VST3; LMMS, Ardour, Carla and other LV2 hosts load the
  LV2. Both are the same instrument and sound the same.

NEEDS
  An x86-64 CPU (AVX2 is used when the CPU has it) and a recent desktop Linux: glibc 2.38 or
  newer (Arch, Fedora 39+, Ubuntu 24.04+, Debian 13+), ALSA, FreeType and fontconfig, and X11
  (Wayland desktops run the editor through XWayland). JACK or PipeWire-JACK for the standalone
  app's JACK output.

LICENCE
  Forte is free software under the GNU General Public License v3 or later (LICENSE). Third-party
  software and fonts: THIRD-PARTY.md and the licenses folder. Credits: CREDITS.md. The complete
  source is the repository above at this version's tag, cloned with --recursive.

VST is a registered trademark of Steinberg Media Technologies GmbH.
EOF

tarball="${dist}/${name}.tar.gz"
rm -f "${tarball}"
tar -C "${dist}" -czf "${tarball}" "${name}"
echo "wrote ${tarball}"
echo "${version}"
