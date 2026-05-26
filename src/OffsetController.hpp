#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>

using namespace geode::prelude;

/**
 * Manages per-level song offset.
 *
 * Offset is applied in PlayLayer::prepareMusic (the last chance before
 * GD reads GameManager::m_timeOffset to start music playback).
 * Positive offset skips that many ms; negative offset with fix enabled
 * redirects to a padded WAV file.
 *
 * For song triggers, FMODAudioEngine::queueStartMusic is hooked to apply
 * the user's offset to the start time parameter, ensuring offset works
 * for ALL music changes during gameplay, not just the initial song.
 */
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects);
    void prepareMusic(bool dontWait);
    void startMusic();
    void onQuit();
};

class $modify(MyFMODAudioEngine, FMODAudioEngine) {
    void queueStartMusic(gd::string audioFilename, float pitch, float unknown,
                         float volume, bool loop, int start, int end,
                         int fadeIn, int fadeOut, int musicID, bool p10,
                         int channelID, bool noPrepare, bool dontReset);
};
