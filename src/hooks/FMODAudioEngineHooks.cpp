#include <Geode/modify/FMODAudioEngine.hpp>

#include "../offset/OffsetController.hpp"
#include "../offset/OffsetCalculator.hpp"
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

    void queueStartMusic(gd::string audioFilename, float pitch,
                         float unknown, float volume, bool loop,
                         int start, int end, int fadeIn,
                         int fadeOut, int musicID, bool p10,
                         int channelID, bool noPrepare,
                         bool dontReset) {
        auto* pl = PlayLayer::get();
        if (!pl || !pl->m_level) {
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
            // Already a padded file — pass through
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
                auto offset = applyOffset(start);
                LOG_DEBUG("queueStartMusic: redirect {} -> {}, start {} -> {} (remainder={})",
                          audioFilename, paddedPath.string(), start,
                          offset.adjustedTime, offset.remainder);
                FMODAudioEngine::queueStartMusic(
                    gd::string(paddedPath.string()), pitch, unknown, volume, loop,
                    offset.adjustedTime, end, fadeIn, fadeOut, musicID, p10,
                    channelID, noPrepare, dontReset
                );
            } else {
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
        if (totalOffset != 0) {
            auto offset = applyOffset(start);
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
        auto offset = applyOffset(start);
        if (offset.adjustedTime != start) {
            LOG_DEBUG("startMusic: applying offset ({} -> {}), musicID={}",
                      start, offset.adjustedTime, musicID);
        }
        FMODAudioEngine::startMusic(
            offset.adjustedTime, end, fadeIn, fadeOut, loop,
            musicID, noResume, dontReset
        );
    }

    // ─── loadAndPlayMusic ───────────────────────────────────────────────────
    // Loads and immediately starts playback at a given time position.
    // Has an unsigned int time parameter that needs offset applied.

    void loadAndPlayMusic(gd::string path, unsigned int time, int musicID) {
        auto offset = applyOffset(time);
        if (offset.adjustedTime != static_cast<int>(time)) {
            LOG_DEBUG("loadAndPlayMusic: applying offset ({} -> {}), path='{}'",
                      time, offset.adjustedTime, path);
        }
        FMODAudioEngine::loadAndPlayMusic(
            path,
            static_cast<unsigned int>(offset.adjustedTime),
            musicID
        );
    }

    // ─── triggerQueuedMusic ─────────────────────────────────────────────────
    // Activates a queued music entry (created by queueStartMusic).
    // The FMODQueuedMusic struct contains m_start which needs offset applied.

    void triggerQueuedMusic(FMODQueuedMusic music) {
        auto offset = applyOffset(music.m_start);
        if (offset.adjustedTime != music.m_start) {
            LOG_DEBUG("triggerQueuedMusic: applying offset to m_start ({} -> {})",
                      music.m_start, offset.adjustedTime);
        }
        music.m_start = offset.adjustedTime;
        FMODAudioEngine::triggerQueuedMusic(music);
    }

    // ─── setMusicTimeMS ─────────────────────────────────────────────────────
    // Seeks music to a given time. Used by checkpoint restoration, pause, etc.

    void setMusicTimeMS(unsigned int ms, bool p1, int channel) {
        auto offset = applyOffset(ms);
        if (offset.adjustedTime != static_cast<int>(ms)) {
            LOG_DEBUG("setMusicTimeMS: applying offset ({} -> {})",
                      ms, offset.adjustedTime);
        }
        FMODAudioEngine::setMusicTimeMS(
            static_cast<unsigned int>(offset.adjustedTime), p1, channel
        );
    }
};
