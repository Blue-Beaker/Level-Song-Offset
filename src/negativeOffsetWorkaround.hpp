#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <filesystem>
#include <unordered_map>

using namespace geode::prelude;

/**
 * Negative offset workaround: silence-prefix approach.
 *
 * When a level has a negative total offset, this module creates a padded
 * WAV file with silence prepended and hooks GJGameLevel::getAudioFileName
 * to return the padded path instead of the original.
 *
 * The padded file is created lazily in getAudioFileName (or eagerly if
 * ensurePaddedFile is called before PlayLayer creation).
 */

/// Ensure a padded WAV exists for the given song key and offset.
/// Call early so the file is ready before PlayLayer::prepareMusic runs.
/// The actual file creation may still happen lazily in getAudioFileName.
void ensurePaddedFile(int songKey, int totalOffset);

/// Registry mapping song key → padded WAV path.
/// Shared with OffsetController for queueStartMusic hook.
extern std::unordered_map<int, std::filesystem::path> s_paddedPathBySongKey;

/// Get the song key for a GJGameLevel's current song.
int getSongKey(GJGameLevel* level);

/// Try to extract a numeric song ID from a path like "123456.mp3".
int extractSongIdFromPath(std::string_view path);

/// Resolve the cache directory from settings or fall back to the mod save dir.
/// Under Wine, auto-maps Linux-style paths to Z:\ drive.
std::filesystem::path getCacheDir();

/// Enforce the max cache size: delete oldest padded files when exceeded.
void enforceCacheSizeLimit();

/// Compute the padded WAV path for a given song key and total offset (ms).
/// e.g. getPaddedPath(837148, -1500) → /cache/padded_837148_2000.wav
std::filesystem::path getPaddedPath(int songKey, int totalOffset);

/// Decode source audio to PCM, prepend silence, write as WAV.
bool createPaddedWavFile(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destPath,
    int padMs
);

class $modify(NegativeOffsetPlayLayer, PlayLayer) {
    void onQuit();
};
