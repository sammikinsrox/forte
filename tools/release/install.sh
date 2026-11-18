#!/usr/bin/env bash
# Installs Forte from the unpacked Linux release (tools/release/package.sh puts this script in it).
#
#     ./install.sh               for this user: ~/.vst3, ~/.lv2, ~/.local/bin (no root)
#     ./install.sh --system      for every user: /usr/local/lib/vst3, /usr/local/lib/lv2, /usr/local/bin (sudo)
#     ./install.sh --uninstall   removes it again (add --system if that's how it was installed)
#
# Presets and settings are never touched.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system=0
uninstall=0
for arg in "$@"; do
    case "${arg}" in
        --system)    system=1 ;;
        --uninstall) uninstall=1 ;;
        -h|--help)   sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)           echo "Unknown option ${arg} (try --help)" >&2; exit 1 ;;
    esac
done

if (( system )); then
    vst3_dir=/usr/local/lib/vst3
    lv2_dir=/usr/local/lib/lv2
    bin_dir=/usr/local/bin
    sudo=sudo
    (( EUID == 0 )) && sudo=
else
    vst3_dir="${HOME}/.vst3"
    lv2_dir="${HOME}/.lv2"
    bin_dir="${HOME}/.local/bin"
    sudo=
fi

if (( uninstall )); then
    ${sudo} rm -rf "${vst3_dir}/Forte.vst3" "${lv2_dir}/Forte.lv2" "${bin_dir}/Forte"
    echo "Removed Forte from ${vst3_dir}, ${lv2_dir} and ${bin_dir}."
    exit 0
fi

for p in Forte.vst3 Forte.lv2 Forte; do
    [[ -e "${here}/${p}" ]] || { echo "Missing ${here}/${p}: run this from the unpacked release" >&2; exit 1; }
done

${sudo} mkdir -p "${vst3_dir}" "${lv2_dir}" "${bin_dir}"
# Replaced whole, so no file of an older version lingers.
${sudo} rm -rf "${vst3_dir}/Forte.vst3" "${lv2_dir}/Forte.lv2"
${sudo} cp -R "${here}/Forte.vst3" "${vst3_dir}/"
${sudo} cp -R "${here}/Forte.lv2" "${lv2_dir}/"
${sudo} install -m 755 "${here}/Forte" "${bin_dir}/Forte"

echo "Installed Forte:"
echo "  VST3        ${vst3_dir}/Forte.vst3"
echo "  LV2         ${lv2_dir}/Forte.lv2"
echo "  Standalone  ${bin_dir}/Forte"
echo "Rescan plugins in your host (Reaper: Options > Preferences > Plug-ins > VST > Re-scan)."
if (( ! system )) && [[ ":${PATH}:" != *":${bin_dir}:"* ]]; then
    echo "Note: ${bin_dir} isn't on your PATH, so start the standalone app as ${bin_dir}/Forte."
fi
