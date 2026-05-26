#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "CacheStorage.hpp"

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

/// Inject periodic 1 kHz beeps into the padded audio buffer for debugging.
/// Beeps are 100 ms long, repeated every 500 ms, written across the full audio.
void injectDebugBeeps(
    std::vector<uint8_t>& audioData,
    unsigned int sampleRate,
    unsigned int blockAlign,
    int numChannels,
    int bitsPerSample
);

/// Ensure a padded WAV exists for the given song key and offset.
/// Call early so the file is ready before PlayLayer::prepareMusic runs.
/// The actual file creation may still happen lazily in getAudioFileName.
void ensurePaddedFile(int songKey, int totalOffset);

/// Decode source audio to PCM, prepend silence, write as WAV.
bool createPaddedWavFile(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destPath,
    int padMs
);

class $modify(NegativeOffsetPlayLayer, PlayLayer) {
    void onQuit();
};
