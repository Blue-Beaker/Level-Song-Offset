#pragma once

#include <Geode/Geode.hpp>

#include <filesystem>
#include <unordered_map>

using namespace geode::prelude;

/**
 * Manages the padded WAV file cache for the negative offset workaround.
 *
 * Padded files are created by prepending silence to the original audio so
 * that negative offsets can be handled by shifting playback start time.
 *
 * Cache files are named: padded_{songKey}_{intervalMs}.wav
 *   - songKey = m_songID (custom song) or -m_audioTrack - 1 (built-in)
 *   - intervalMs = ceil(abs(totalOffset) / 1000) * 1000
 *
 * Responsibilities:
 *   - Registry: maps song key → padded WAV path (s_paddedPathBySongKey)
 *   - Path computation: getPaddedPath()
 *   - Cache directory resolution: getCacheDir()
 *   - Cache size enforcement: enforceCacheSizeLimit()
 *   - Song key helpers: getSongKey(), extractSongIdFromPath()
 */

/// Registry mapping song key → padded WAV path.
/// Shared with OffsetController and negativeOffsetWorkaround hooks.
extern std::unordered_map<int, std::filesystem::path> s_paddedPathBySongKey;

/// Get the song key for a GJGameLevel's current song.
int getSongKey(GJGameLevel* level);

// @geode-ignore(unknown-resource)
/// Try to extract a numeric song ID from a path like "123456.mp3".
/// Returns -1 if no numeric ID found.
int extractSongIdFromPath(std::string_view path);

/// Resolve the cache directory from settings or fall back to the mod save dir.
/// Under Wine, auto-maps Linux-style paths to Z:\ drive.
std::filesystem::path getCacheDir();

/// Compute the padded WAV path for a given song key and total offset (ms).
/// e.g. getPaddedPath(837148, -1500) → /cache/padded_837148_2000.wav
std::filesystem::path getPaddedPath(int songKey, int totalOffset);

/// Enforce the max cache size: delete oldest padded files when exceeded.
/// Files currently registered in s_paddedPathBySongKey are excluded.
void enforceCacheSizeLimit();

void reduceCacheToSize(int maxSizeMB, std::unordered_set<std::filesystem::path> excludedFiles);
