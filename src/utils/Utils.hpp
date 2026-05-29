#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

// ─── Common macros ──────────────────────────────────────────────────────────

/// Conditional debug logging — only prints when the "debug-logging" setting is enabled.
/// Usage: LOG_DEBUG("some value: {}", val);
#define LOG_DEBUG(...) \
    do { if (Mod::get()->getSettingValue<bool>("debug-logging")) \
        log::info(__VA_ARGS__); } while(0)

// ─── Level ID resolution ────────────────────────────────────────────────────

/// Resolve the effective level ID for a GJGameLevel.
///
/// For published levels (m_levelID != 0), returns m_levelID directly.
/// For editor levels (m_levelID == 0), uses the EditorIDs API to get a
/// persistent unique ID so each editor level can be identified reliably.
///
/// @param level The level to resolve. Returns 0 if null.
int getLevelId(GJGameLevel* level);

// ─── Song key collection ────────────────────────────────────────────────────

/// Collect all song keys for a level into a vector.
/// Includes the main song key (m_songID or -m_audioTrack - 1) plus any
/// additional song IDs from m_songIDs (comma-separated string).
///
/// @param level The level to collect song keys from.
/// @return A vector of all song keys for the level.
std::vector<int> getLevelSongKeys(GJGameLevel* level);