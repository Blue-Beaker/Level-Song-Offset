#include <Geode/modify/FMODAudioEngine.hpp>

#include "../offset/OffsetController.hpp"
#include "../offset/OffsetCalculator.hpp"
#include "../offset/PaddedTrackTracker.hpp"
#include "../offset/negative-offset-workaround/CacheStorage.hpp"
#include "../utils/Utils.hpp"

using namespace geode::prelude;

extern int s_currentTotalOffset;

// ─── Hook: FMODAudioEngine ─────────────────────────────────────────────────

class $modify(MyFMODAudioEngine, FMODAudioEngine) {
    // Use Late priority so jukebox (and other mods) can process the call first
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost(
            "FMODAudioEngine::queueStartMusic",
            Priority::Late
        );
        (void)self.setHookPriorityPost(
            "FMODAudioEngine::startMusic",
            Priority::Late
        );
        (void)self.setHookPriorityPost(
            "FMODAudioEngine::loadAndPlayMusic",
            Priority::Late
        );
        (void)self.setHookPriorityPost(
            "FMODAudioEngine::triggerQueuedMusic",
            Priority::Late
        );
        (void)self.setHookPriorityPost(
            "FMODAudioEngine::setMusicTimeMS",
            Priority::Late
        );
    }

    // ─── queueStartMusic ────────────────────────────────────────────────────
    // Primary entry point for initial level music loading.
    //
    // When noPrepare=true:  GD plays the music immediately → we adjust start here.
    // When noPrepare=false: GD queues the music, then triggerQueuedMusic plays it
    //                       → we skip start adjustment here (triggerQueuedMusic handles it).
    //
    // Padded file redirect always needs remainder adjustment regardless of noPrepare,
    // because the padded file itself has leading silence that must be accounted for.

    void queueStartMusic(gd::string audioFilename, float pitch,
                         float unknown, float volume, bool loop,
                         int start, int end, int fadeIn,
                         int fadeOut, int musicID, bool p10,
                         int channelID, bool noPrepare,
                         bool dontReset) {
        auto* pl = PlayLayer::get();
        if (!pl || !pl->m_level) {
            s_paddedTracks.setOriginal(musicID, channelID);
            FMODAudioEngine::queueStartMusic(
                audioFilename, pitch, unknown, volume, loop, start, end,
                fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
            );
            return;
        }

        int totalOffset = s_currentTotalOffset;
        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

        // ── Negative offset with fix enabled → redirect to padded file ──
        if (totalOffset < 0 && fixEnabled) {
            // Already a padded file — pass through (keep padded state)
            if (audioFilename.find("padded_") != gd::string::npos) {
                FMODAudioEngine::queueStartMusic(
                    audioFilename, pitch, unknown, volume, loop, start, end,
                    fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
                );
                return;
            }

            int songKey = musicID > 0 ? musicID
                         : extractSongIdFromPath(std::string_view(audioFilename));
            if (songKey <= 0) {
                s_paddedTracks.setOriginal(musicID, channelID);
                FMODAudioEngine::queueStartMusic(
                    audioFilename, pitch, unknown, volume, loop, start, end,
                    fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
                );
                return;
            }

            auto paddedPath = getPaddedPath(songKey, totalOffset,
                                            std::filesystem::path(audioFilename));
            std::error_code ec;

            LOG_DEBUG("queueStartMusic: song key {}, source '{}', padded '{}'",
                      songKey, audioFilename, paddedPath.string());

            if (std::filesystem::exists(paddedPath, ec)) {
                s_paddedTracks.setPadded(musicID, channelID);
                // Padded file has leading silence — always adjust start by remainder
                auto offset = applyOffset(start, true);
                LOG_DEBUG("queueStartMusic: redirect {} -> {}, start {} -> {} (remainder={}, noPrepare={})",
                          audioFilename, paddedPath.string(), start,
                          offset.adjustedTime, offset.remainder, noPrepare);
                FMODAudioEngine::queueStartMusic(
                    gd::string(paddedPath.string()), pitch, unknown, volume, loop,
                    offset.adjustedTime, end, fadeIn, fadeOut, musicID, p10,
                    channelID, noPrepare, dontReset
                );
            } else {
                s_paddedTracks.setOriginal(musicID, channelID);
                LOG_DEBUG("queueStartMusic: padded file not ready for song {}, "
                          "falling back to original", songKey);
                FMODAudioEngine::queueStartMusic(
                    audioFilename, pitch, unknown, volume, loop, start, end,
                    fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
                );
            }
            return;
        }

        // ── Positive offset (or negative without fix) → adjust start ──
        s_paddedTracks.setOriginal(musicID, channelID);
        if (totalOffset != 0 && noPrepare) {
            // Only adjust when playing directly; triggerQueuedMusic handles queued case
            auto offset = applyOffset(start, false);
            LOG_DEBUG("queueStartMusic: applying offset {} to start ({} -> {}), musicID={}",
                      totalOffset, start, offset.adjustedTime, musicID);
            start = offset.adjustedTime;
        }

        FMODAudioEngine::queueStartMusic(
            audioFilename, pitch, unknown, volume, loop, start, end,
            fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
        );
    }

    // ─── startMusic ─────────────────────────────────────────────────────────
    // Called by PlayLayer::startMusic() on practice mode respawn / resetLevel.
    // queueStartMusic is NOT called in that path, so we need this hook.

    void startMusic(int start, int end, int fadeIn, int fadeOut,
                    bool loop, int musicID, bool noResume, bool dontReset) {
        bool isPadded = s_paddedTracks.isPaddedByMusicID(musicID);
        auto offset = applyOffset(start, isPadded);
        if (offset.adjustedTime != start) {
            LOG_DEBUG("startMusic: applying offset ({} -> {}), musicID={}, padded={}",
                      start, offset.adjustedTime, musicID, isPadded);
        }
        FMODAudioEngine::startMusic(
            offset.adjustedTime, end, fadeIn, fadeOut, loop,
            musicID, noResume, dontReset
        );
    }

    // ─── loadAndPlayMusic ───────────────────────────────────────────────────
    // Loads and immediately starts playback at a given time position.
    // Called by song triggers mid-level to switch music.
    // Needs to check for padded files just like queueStartMusic.

    void loadAndPlayMusic(gd::string path, unsigned int time, int musicID) {
        auto* pl = PlayLayer::get();
        if (!pl || !pl->m_level) {
            s_paddedTracks.setOriginal(musicID, 0);
            FMODAudioEngine::loadAndPlayMusic(path, time, musicID);
            return;
        }

        int totalOffset = s_currentTotalOffset;
        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

        // ── Negative offset with fix enabled → redirect to padded file ──
        if (totalOffset < 0 && fixEnabled) {
            if (path.find("padded_") != gd::string::npos) {
                FMODAudioEngine::loadAndPlayMusic(path, time, musicID);
                return;
            }

            int songKey = musicID > 0 ? musicID
                         : extractSongIdFromPath(std::string_view(path));
            if (songKey > 0) {
                auto paddedPath = getPaddedPath(songKey, totalOffset,
                                                std::filesystem::path(path));
                std::error_code ec;

                if (std::filesystem::exists(paddedPath, ec)) {
                    s_paddedTracks.setPadded(musicID, 0);
                    auto offset = applyOffset(static_cast<int>(time), true);
                    LOG_DEBUG("loadAndPlayMusic: redirect {} -> {}, time {} -> {} (remainder={})",
                              path, paddedPath.string(), time, offset.adjustedTime, offset.remainder);
                    FMODAudioEngine::loadAndPlayMusic(
                        gd::string(paddedPath.string()),
                        static_cast<unsigned int>(offset.adjustedTime),
                        musicID
                    );
                    return;
                }
            }
        }

        // ── Normal path: apply offset ──
        bool isPadded = s_paddedTracks.isPaddedByMusicID(musicID);
        auto offset = applyOffset(static_cast<int>(time), isPadded);
        if (offset.adjustedTime != static_cast<int>(time)) {
            LOG_DEBUG("loadAndPlayMusic: applying offset ({} -> {}), path='{}', padded={}",
                      time, offset.adjustedTime, path, isPadded);
        }
        FMODAudioEngine::loadAndPlayMusic(
            path,
            static_cast<unsigned int>(offset.adjustedTime),
            musicID
        );
    }

    // ─── triggerQueuedMusic ─────────────────────────────────────────────────
    // Activates a queued music entry. Called when:
    //   a) queueStartMusic(noPrepare=false) finishes prep → m_start already adjusted
    //   b) Song trigger directly constructs FMODQueuedMusic → m_start is raw
    //
    // We cannot distinguish (a) from (b), so we always apply offset.
    // For case (a), queueStartMusic set padded=true and did NOT adjust start
    // (because noPrepare=false), so this is the only adjustment — correct.
    // For case (b), the raw trigger start gets adjusted — also correct.

    void triggerQueuedMusic(FMODQueuedMusic music) {
        bool isPadded = s_paddedTracks.isPaddedByChannel(music.m_channelID);
        auto offset = applyOffset(music.m_start, isPadded);
        if (offset.adjustedTime != music.m_start) {
            LOG_DEBUG("triggerQueuedMusic: applying offset to m_start ({} -> {}), channel={}, padded={}",
                      music.m_start, offset.adjustedTime, music.m_channelID, isPadded);
        }
        music.m_start = offset.adjustedTime;
        FMODAudioEngine::triggerQueuedMusic(music);
    }

    // ─── setMusicTimeMS ─────────────────────────────────────────────────────
    // Seeks music to a given time. Used by checkpoint restoration, pause, etc.

    void setMusicTimeMS(unsigned int ms, bool p1, int channel) {
        bool isPadded = s_paddedTracks.isPaddedByChannel(channel);
        auto offset = applyOffset(ms, isPadded);
        if (offset.adjustedTime != static_cast<int>(ms)) {
            LOG_DEBUG("setMusicTimeMS: applying offset ({} -> {}), channel={}, padded={}",
                      ms, offset.adjustedTime, channel, isPadded);
        }
        FMODAudioEngine::setMusicTimeMS(
            static_cast<unsigned int>(offset.adjustedTime), p1, channel
        );
    }
};
