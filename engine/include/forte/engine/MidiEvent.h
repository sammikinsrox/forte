#pragma once

#include <cstdint>

namespace forte
{
/** One short MIDI message, as raw bytes.

    The engine owns all MIDI interpretation (SPEC §4), so the plugin and forte-render hand it the
    same bytes and get the same behaviour.
*/
struct MidiEvent
{
    std::uint8_t bytes[3] {};
    std::uint8_t size = 0;
};
} // namespace forte
