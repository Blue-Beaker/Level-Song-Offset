#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

/**
 * Resolve the effective level ID for a GJGameLevel.
 *
 * For published levels (m_levelID != 0), returns m_levelID directly.
 * For editor levels (m_levelID == 0), uses the EditorIDs API to get a
 * persistent unique ID so each editor level can be identified reliably.
 *
 * @param level The level to resolve. Returns 0 if null.
 */
int getLevelId(GJGameLevel* level);
