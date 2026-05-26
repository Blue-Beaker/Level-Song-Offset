#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

/**
 * Manages per-level song offset.
 *
 * Offset is applied in PlayLayer::prepareMusic (the last chance before
 * GD reads GameManager::m_timeOffset to start music playback).
 * Positive offset skips that many ms; negative offset with fix enabled
 * redirects to a padded WAV file.
 */
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects);
    void prepareMusic(bool dontWait);
    void startMusic();
    void onQuit();
};
