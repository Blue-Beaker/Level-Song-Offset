#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

/**
 * Manages per-level song offset by hooking into FMODAudioEngine's
 * global m_musicOffset for the lifetime of a PlayLayer.
 *
 * On PlayLayer::init:  saves original offset
 * On PlayLayer::onQuit: restores original offset
 * On PlayLayer::startMusic:
 *   - Calculates total offset = original m_musicOffset + user offset
 *   - If total offset is negative and fix is enabled: starts music at
 *     position 0 with volume muted, then restores volume and seeks to
 *     beginning after |totalOffset| ms delay
 *   - Otherwise: sets m_musicOffset = totalOffset before calling original
 */
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects);
    void startMusic();
    void onQuit();

    void applyDelayedMusic(float dt);

    struct Fields {
        float m_savedBgVolume = 1.f;
    };
};
