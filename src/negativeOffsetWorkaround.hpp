#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

/**
 * Negative offset workaround: silence-prefix approach.
 *
 * When a level has a negative total offset (GameManager timeOffset + user offset),
 * instead of using runtime hacks (volume mute + seek), this module creates a
 * temporary WAV file that has |offset| ms of silence prepended to the original
 * audio, then redirects the game to load that padded file instead.
 *
 * This is the cleanest possible approach because:
 *   - The game loads a modified audio file as if it were the original
 *   - All runtime state (pause/resume, checkpoints, retry, sync) works normally
 *   - No FMOD hacks, no scheduled callbacks, no volume manipulation
 *
 * Hook points:
 *   - PlayLayer::prepareMusic — earliest point to intercept and create the
 *     padded file, before any audio loading occurs
 *   - GJGameLevel::getAudioFileName — redirect the filename to our padded WAV
 *   - PlayLayer::startMusic — ensure m_musicOffset = 0 (silence is in the file)
 *   - PlayLayer::onQuit — clean up temporary files and restore state
 */
class $modify(NegativeOffsetPlayLayer, PlayLayer) {
    void prepareMusic(bool dontWait);
    void startMusic();
    void onQuit();

    struct Fields {
        // Path to the temporary padded audio file, for cleanup
        std::filesystem::path m_paddedAudioPath;
    };
};
