#!/usr/bin/env bash
# The Linux half of a release (SPEC §9.5): configures, builds Release, runs every test (pluginval
# on the VST3 and LV2, Steinberg's validator) on a virtual display, and packages
# dist/Forte-<version>-linux-x64.tar.gz. tools/release/make-release.ps1 runs it through WSL; on
# Linux it runs as it is.
#
#     tools/release/build-linux.sh [--skip-tests]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "${root}"

skip_tests=0
[[ "${1:-}" == "--skip-tests" ]] && skip_tests=1

echo "== Linux: check tools"
bash tools/get-pluginval.sh
bash tools/get-vst3-validator.sh

echo "== Linux: configure"
cmake --preset linux -DFORTE_REQUIRE_CHECK_TOOLS=ON > /dev/null

echo "== Linux: build Release"
cmake --build --preset linux-release

if (( ! skip_tests )); then
    echo "== Linux: tests (pluginval included)"
    xvfb-run -a ctest --preset linux-release
fi

echo "== Linux: package"
bash tools/release/package.sh
