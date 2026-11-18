#!/usr/bin/env bash
# Builds Steinberg's VST3 validator from a pinned vst3sdk tag into .tools/linux/vst3-validator/.
# The Linux twin of tools/get-vst3-validator.ps1: JUCE's bundled copy of the VST3 SDK leaves the
# validator out, so it's built from the full SDK (MIT), and pluginval gets it with
# --vst3validator. Only the submodules the validator needs are fetched. To upgrade, change both
# values below in one commit (and in the .ps1).
set -euo pipefail

TAG="v3.8.1_build_84"
COMMIT="3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96"
REPO="https://github.com/steinbergmedia/vst3sdk.git"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${root}/.tools/linux/vst3-validator"
exe="${dest}/validator"
stamp="${dest}/version.txt"

if [[ -x "${exe}" && -f "${stamp}" && "$(tr -d '[:space:]' < "${stamp}")" == "${TAG}" ]]; then
    echo "VST3 validator ${TAG} already at ${exe}"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

echo "Fetching vst3sdk ${TAG}"
git -c advice.detachedHead=false clone --quiet --depth 1 --branch "${TAG}" "${REPO}" "${work}/src"
head="$(git -C "${work}/src" rev-parse HEAD)"
if [[ "${head}" != "${COMMIT}" ]]; then
    echo "vst3sdk ${TAG} resolved to ${head}, expected ${COMMIT}. The tag may have moved." >&2
    exit 1
fi
git -C "${work}/src" submodule update --quiet --init --depth 1 base cmake pluginterfaces public.sdk

# The SDK builds every hosting sample it finds, and the editor and audio hosts need GTK 3 on
# Linux. Only the validator is wanted (it borrows a header from the audio host, so the sources
# stay), so in this throwaway clone the hosting samples' CMake adds just that one.
printf 'add_subdirectory(validator)\n' > "${work}/src/public.sdk/samples/vst-hosting/CMakeLists.txt"

echo "Building the VST3 validator"
log="${work}/build.log"
if ! { cmake -S "${work}/src" -B "${work}/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
           -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
           -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON \
           -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
           -DSMTG_CREATE_PLUGIN_LINK=OFF \
       && cmake --build "${work}/build" --target validator --parallel; } > "${log}" 2>&1; then
    tail -n 40 "${log}" >&2
    echo "Building the VST3 validator failed" >&2
    exit 1
fi

built="$(find "${work}/build" -type f -name validator -perm -u+x | head -n 1)"
if [[ -z "${built}" ]]; then
    echo "validator was not produced by the build" >&2
    exit 1
fi

rm -rf "${dest}"
mkdir -p "${dest}"
cp "${built}" "${exe}"
echo "${TAG}" > "${stamp}"
echo "VST3 validator ${TAG} -> ${exe}"
