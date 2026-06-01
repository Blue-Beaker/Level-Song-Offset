#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

/**
 * Result of applying offset to a time value (usually milliseconds).
 */
struct OffsetResult {
    /// Whether the offset should redirect to a padded file
    /// (only relevant for negative offset + fix enabled + queueStartMusic path)
    bool usePaddedFile = false;
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
 * Handles both positive and negative offsets:
 *  - Positive offset: time += offset
 *  - Negative offset (fix enabled): computes remainder for padded file compensation
 *  - Negative offset (fix disabled): time += offset (same as positive)
 *
 * @param timeMs The original time value in milliseconds
 * @return OffsetResult with the adjusted time
 */
inline OffsetResult applyOffset(int timeMs) {
    extern int s_currentTotalOffset;

    int totalOffset = s_currentTotalOffset;
    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

    OffsetResult result;
    result.adjustedTime = timeMs;

    if (totalOffset < 0 && fixEnabled) {
        result.intervalMs = ((std::abs(totalOffset) + 999) / 1000) * 1000;
        result.remainder = result.intervalMs - std::abs(totalOffset);
        result.adjustedTime = timeMs + result.remainder;
        result.usePaddedFile = true;
    } else if (totalOffset != 0) {
        result.adjustedTime = timeMs + totalOffset;
        if (result.adjustedTime < 0) result.adjustedTime = 0;
    }

    return result;
}

/**
 * Overload for unsigned int time values.
 */
inline OffsetResult applyOffset(unsigned int timeMs) {
    return applyOffset(static_cast<int>(timeMs));
}
