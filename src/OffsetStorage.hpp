#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

/**
 * Per-level offset storage backed by mod save data.
 * Data is persisted as a JSON object: { "levelId": offset, ... }
 * inside the mod's save container.
 *
 * For editor levels (m_levelID == 0), uses EditorIDs API to get a
 * persistent unique ID so each editor level has its own offset.
 */

class OffsetStorage {
public:
    /**
     * Get the stored offset for a given level.
     * Returns 0 if no offset has been set.
     */
    static float getOffsetForLevel(int levelId);

    /**
     * Get the stored offset for a level object.
     * Automatically resolves editor level IDs via EditorIDs API.
     */
    static float getOffsetForLevel(GJGameLevel* level);

    /**
     * Set the offset for a given level and persist to save data.
     */
    static void setOffsetForLevel(int levelId, float offset);

    /**
     * Set the offset for a level object.
     * Automatically resolves editor level IDs via EditorIDs API.
     */
    static void setOffsetForLevel(GJGameLevel* level, float offset);
};
