#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

/**
 * Result of applying offset to a time value (usually milliseconds).
 */
struct OffsetResult {
    /// The adjusted time value with offset applied
    int adjustedTime = 0;
    /// The remainder (interval - abs(offset)), used for padded file compensation
    int remainder = 0;
    /// The interval (ceil(abs(offset)/1000)*1000), used for padded file naming
    int intervalMs = 0;
};

/**
 * Applies the current song offset to a given time value (in milliseconds).
 *
 * Behaviour depends on whether the track uses a padded audio file:
 *  - Padded: time += remainder (skip the prepended silence)
 *  - Not padded: time += offset, clamped to 0
 *  - Positive offset (no padding needed): time += offset, clamped to 0
 *
 * @param timeMs   The original time value in milliseconds
 * @param isPadded Whether the track is using a padded audio file
 * @return OffsetResult with the adjusted time
 */
inline OffsetResult applyOffset(int timeMs, bool isPadded = false) {
    extern int s_currentTotalOffset;

    int totalOffset = s_currentTotalOffset;
    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

    OffsetResult result;
    result.adjustedTime = timeMs;

    if (totalOffset < 0 && fixEnabled && isPadded) {
        result.intervalMs = ((std::abs(totalOffset) + 999) / 1000) * 1000;
        result.remainder = result.intervalMs - std::abs(totalOffset);
        result.adjustedTime = timeMs + result.remainder;
    } else if (totalOffset != 0) {
        result.adjustedTime = timeMs + totalOffset;
        if (result.adjustedTime < 0) result.adjustedTime = 0;
    }

    return result;
}

/**
 * Overload for unsigned int time values.
 */
inline OffsetResult applyOffset(unsigned int timeMs, bool isPadded = false) {
    return applyOffset(static_cast<int>(timeMs), isPadded);
}
