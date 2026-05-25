#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

/**
 * Manages per-level song offset by hooking into FMODAudioEngine's
 * global m_musicOffset for the lifetime of a PlayLayer.
 *
 * On PlayLayer::init:  saves original offset, adds user offset
 * On PlayLayer::onQuit: restores original offset
 * On PlayLayer::startMusic: handles negative offset workaround (delay)
 */
class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects);
    void startMusic();
    void onQuit();

    void applyDelayedMusic();
};
