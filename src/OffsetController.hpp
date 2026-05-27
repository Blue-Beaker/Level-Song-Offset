#pragma once

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>

using namespace geode::prelude;

/**
 * Manages per-level song offset.
 *
 * Instead of modifying the global m_musicOffset (which only affects initial
 * music), we hook FMODAudioEngine methods directly — like jukebox does —
 * to apply the offset to the start time parameter. This ensures offset
 * works for ALL music playback: initial music, song triggers, and seek.
 *
 * For negative offset with fix enabled, GJGameLevel::getAudioFileName
 * returns a padded file (offset baked in), and queueStartMusic
 * redirects song triggers to the same padded file. If the padded file
 * isn't ready yet, it falls back to the original with offset=0.
 *
 * Padded files are pre-generated asynchronously in MyPlayLayer::init
 * and can also be triggered manually (e.g. from OffsetPopup::onApply).
 */

/// Start async pre-generation of padded audio files for a level's songs.
/// If generation is already running, this call is silently ignored.
/// Safe to call multiple times — already-cached files are skipped.
void startPregenerateForLevel(GJGameLevel* level);

class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects);
    void prepareMusic(bool dontWait);
};

class $modify(MyFMODAudioEngine, FMODAudioEngine) {
    void queueStartMusic(gd::string audioFilename, float pitch, float unknown,
                         float volume, bool loop, int start, int end,
                         int fadeIn, int fadeOut, int musicID, bool p10,
                         int channelID, bool noPrepare, bool dontReset);
    void setMusicTimeMS(unsigned int ms, bool p1, int channel);
};
