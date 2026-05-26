#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

/**
 * Manages per-level song offset for the *positive* offset case.
 *
 * Positive offset: music starts at |offset| ms into the song (skips the
 * beginning).  Implemented by setting FMODAudioEngine::m_musicOffset before
 * PlayLayer::startMusic() is called — this is GD's native mechanism.
 *
 * Negative offset handling has been moved to NegativeOffsetWorkaround.
 */
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects);
    void startMusic();
    void onQuit();
};
