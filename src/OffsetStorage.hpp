#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

/**
 * Per-level offset storage backed by mod save data.
 * Data is persisted as a JSON object: { "levelId": offset, ... }
 * inside the mod's save container.
 */

class OffsetStorage {
public:
    /**
     * Get the stored offset for a given level.
     * Returns 0 if no offset has been set.
     */
    static float getOffsetForLevel(int levelId);

    /**
     * Set the offset for a given level and persist to save data.
     */
    static void setOffsetForLevel(int levelId, float offset);
};
