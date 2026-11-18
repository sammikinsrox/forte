#!/usr/bin/env bash
# Downloads the pinned pluginval release for Linux into .tools/linux/pluginval/ and checks its
# hash. The Linux twin of tools/get-pluginval.ps1: CMake's configure step finds
# .tools/linux/pluginval/pluginval and registers the pluginval CTest tests. To upgrade, change
# both values below in one commit (and in the .ps1).
set -euo pipefail

VERSION="v1.0.4"
SHA256="c01c49d8063965c4c2dea8324468336768f5c9139e0b1caebde14c2400b55352"
URL="https://github.com/Tracktion/pluginval/releases/download/${VERSION}/pluginval_Linux.zip"

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${root}/.tools/linux/pluginval"
exe="${dest}/pluginval"
stamp="${dest}/version.txt"

if [[ -x "${exe}" && -f "${stamp}" && "$(tr -d '[:space:]' < "${stamp}")" == "${VERSION}" ]]; then
    echo "pluginval ${VERSION} already at ${exe}"
    exit 0
fi

zip="$(mktemp --suffix=.zip)"
trap 'rm -f "${zip}"' EXIT

echo "Downloading pluginval ${VERSION}"
curl --fail --location --silent --show-error --output "${zip}" "${URL}"

actual="$(sha256sum "${zip}" | cut -d' ' -f1)"
if [[ "${actual}" != "${SHA256}" ]]; then
    echo "pluginval download hash mismatch: expected ${SHA256}, got ${actual}" >&2
    exit 1
fi

rm -rf "${dest}"
mkdir -p "${dest}"
python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "${zip}" "${dest}"
chmod +x "${exe}"
echo "${VERSION}" > "${stamp}"
echo "pluginval ${VERSION} -> ${exe}"
