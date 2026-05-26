#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

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

class $modify(NegativeOffsetPlayLayer, PlayLayer) {
    void onQuit();
};
