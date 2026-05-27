#pragma once

#include <Geode/Geode.hpp>

#include "CacheStorage.hpp"

using namespace geode::prelude;

/**
 * Negative offset workaround: silence-prefix approach — pure logic, no hooks.
 *
 * Creates padded WAV files with silence prepended so that negative offsets
 * can be handled by shifting playback start time.
 *
 * Hooks that use these functions are in src/hooks/:
 *   GJGameLevelHooks.cpp — NegativeOffsetGJGameLevel
 *   PlayLayerHooks.cpp   — NegativeOffsetPlayLayer
 */

/// Decode source audio to PCM, prepend silence, write as WAV.
bool createPaddedWavFile(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destPath,
    int padMs
);
