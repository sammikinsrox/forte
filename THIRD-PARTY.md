# Third-party software in Forte

Forte is free software under the GNU General Public License v3 or later (`LICENSE`). It is built
with the JUCE framework and contains the third-party code below. Every release package carries
these licences in its `licenses` folder, copied from the exact sources it was built from.

The list is checked against the Windows binaries at release (issue 0020): code that JUCE vendors
but Forte's binaries don't contain (Ogg Vorbis, Opus, the ASIO SDK, LV2, AAX, AudioUnit, Oboe)
isn't listed.

| Component | Version | Licence | In Forte | Licence text |
|---|---|---|---|---|
| JUCE | 9.0.5 | AGPLv3 (used under it; combined with GPLv3 per GPLv3 §13) | the plugin and app framework | `licenses/AGPL-3.0.txt` |
| VST3 SDK (Steinberg) | 3.8.0 | MIT | the VST3 plugin format | `licenses/VST3_SDK-MIT.txt` |
| FLAC | 1.5.0 | BSD-3-Clause | JUCE's audio formats | `licenses/FLAC.txt` |
| zlib | 1.3.2 | Zlib | JUCE's core | `licenses/zlib.txt` |
| libpng | 1.6.58 | libpng-2.0 | JUCE's image formats | `licenses/libpng.txt` |
| Independent JPEG Group's JPEG software | 10 | IJG | JUCE's image formats | `licenses/jpeglib-README.txt` (LEGAL ISSUES) |
| libwebp | 1.6.0 | BSD-3-Clause | JUCE's image formats | `licenses/libwebp.txt` |
| HarfBuzz | 15.0.0 | MIT (Modern Variant) | JUCE's text shaping | `licenses/HarfBuzz.txt` |
| SheenBidi | 2.9.0 | Apache-2.0 | JUCE's bidirectional text | `licenses/SheenBidi.txt` |
| LunaSVG | 3.5.0 | MIT | JUCE's SVG drawables | `licenses/LunaSVG.txt` |
| PlutoVG | 1.3.2 | MIT | LunaSVG's renderer | `licenses/PlutoVG.txt` |
| Jost (font) | | SIL Open Font License 1.1 | the editor's text, embedded unmodified | `licenses/OFL-1.1-Jost.txt` |
| Italiana (font) | | SIL Open Font License 1.1, Reserved Font Name "Italiana" | the editor's title, embedded unmodified | `licenses/OFL-1.1-Italiana.txt` |

The complete corresponding source is this repository at the release's tag, checked out with its
submodules (`git clone --recursive`), which includes JUCE and everything above.

VST is a registered trademark of Steinberg Media Technologies GmbH.
