#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

/**
 * Negative offset workaround: silence-prefix approach.
 *
 * When a level has a negative total offset, this module creates a temporary
 * WAV file with |offset| ms of silence prepended, then hooks into
 * FMODAudioEngine::queueStartMusic to redirect playback to the padded file.
 *
 * This works on every play/respawn because queueStartMusic is called every
 * time music starts, not just on initial level entry.
 *
 * Hook points:
 *   - GJGameLevel::getAudioFileName — create & register the padded WAV file
 *   - FMODAudioEngine::queueStartMusic — redirect to the padded WAV
 *   - PlayLayer::onQuit — clean up temporary files
 */
class $modify(NegativeOffsetPlayLayer, PlayLayer) {
    void onQuit();
};
