#pragma once

#include <cstddef>

namespace forte::detail::overrides
{
/** One note's voicing overrides (SPEC §3.2, §6.5): the voicing lab's exports, folded on top of the
    fitted values by tools/fitting/fold_voicing.py. All 1 (scales), 0 (offsets) and < 0 (replaced
    values: keep the fitted one) until something is folded. */
struct NoteOverride
{
    int midiNote;
    double feltStiffnessScale, feltRelaxation, contactSpeedExponent;
    double strikePositionScale, hammerMassScale, knockScale;
    double inharmonicityScale, unisonSpreadScale, sustainRateScale;
    double overtoneDecayExponent, aftersoundRateScale, phantomLevelScale;
};

#include "VoicingOverrides.inc"
} // namespace forte::detail::overrides
