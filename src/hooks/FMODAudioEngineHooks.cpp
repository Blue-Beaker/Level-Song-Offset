#include <Geode/modify/FMODAudioEngine.hpp>

#include "../OffsetController.hpp"
#include "../CacheStorage.hpp"
#include "../Utils.hpp"

using namespace geode::prelude;

extern int s_currentTotalOffset;

// ─── Hook: FMODAudioEngine::queueStartMusic ─────────────────────────────────

class $modify(MyFMODAudioEngine, FMODAudioEngine) {
    // Use Late priority so jukebox (and other mods) can process the call first
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost(
            "FMODAudioEngine::queueStartMusic",
            Priority::Late
        );
        (void)self.setHookPriorityPost(
            "FMODAudioEngine::setMusicTimeMS",
            Priority::Late
        );
    }

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
            if (audioFilename.find("padded_") != gd::string::npos) {
                FMODAudioEngine::queueStartMusic(
                    audioFilename, pitch, unknown, volume, loop, start, end,
                    fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
                );
                return;
            }

            int songKey = 0;
            if (musicID > 0) {
                songKey = musicID;
            } else {
                songKey = extractSongIdFromPath(std::string_view(audioFilename));
            }
            if (songKey <= 0) {
                FMODAudioEngine::queueStartMusic(
                    audioFilename, pitch, unknown, volume, loop, start, end,
                    fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
                );
                return;
            }

            int levelSongKey = getSongKey(pl->m_level);
            bool relevant = (songKey == levelSongKey);
            if (!relevant && !pl->m_level->m_songIDs.empty()) {
                auto ids = pl->m_level->m_songIDs;
                size_t p = 0;
                while ((p = ids.find(',')) != gd::string::npos) {
                    auto idStr = ids.substr(0, p);
                    ids.erase(0, p + 1);
                    auto key = geode::utils::numFromString<int>(idStr);
                    if (key && key.unwrap() == songKey) { relevant = true; break; }
                }
                if (!relevant && !ids.empty()) {
                    auto key = geode::utils::numFromString<int>(ids);
                    if (key && key.unwrap() == songKey) relevant = true;
                }
            }
            if (!relevant) {
                FMODAudioEngine::queueStartMusic(
                    audioFilename, pitch, unknown, volume, loop, start, end,
                    fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
                );
                return;
            }

            auto paddedPath = getPaddedPath(songKey, totalOffset, std::filesystem::path(audioFilename));
            std::error_code ec;

            LOG_DEBUG("queueStartMusic: song key {}, source '{}', padded '{}'",
                      songKey, audioFilename, paddedPath.string());

            if (std::filesystem::exists(paddedPath, ec)) {
                int intervalMs = ((std::abs(totalOffset) + 999) / 1000) * 1000;
                int remainder = intervalMs - std::abs(totalOffset);
                int adjustedStart = start + remainder;
                LOG_DEBUG("queueStartMusic: redirect {} -> {}, start {} -> {} (remainder={})",
                          audioFilename, paddedPath.string(), start, adjustedStart, remainder);
                FMODAudioEngine::queueStartMusic(
                    gd::string(paddedPath.string()), pitch, unknown, volume, loop,
                    adjustedStart, end, fadeIn, fadeOut, musicID, p10, channelID,
                    noPrepare, dontReset
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
            int adjustedStart = start + totalOffset;
            if (adjustedStart < 0) adjustedStart = 0;
            LOG_DEBUG("queueStartMusic: applying offset {} to start ({} -> {}), musicID={}",
                      totalOffset, start, adjustedStart, musicID);
            start = adjustedStart;
        }

        FMODAudioEngine::queueStartMusic(
            audioFilename, pitch, unknown, volume, loop, start, end,
            fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
        );
    }

    // ─── Hook: FMODAudioEngine::setMusicTimeMS ──────────────────────────────

    void setMusicTimeMS(unsigned int ms, bool p1, int channel) {
        int totalOffset = s_currentTotalOffset;
        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

        if (totalOffset < 0 && fixEnabled) {
            int intervalMs = ((std::abs(totalOffset) + 999) / 1000) * 1000;
            int remainder = intervalMs - std::abs(totalOffset);
            int adjustedMs = static_cast<int>(ms) + remainder;
            LOG_DEBUG("setMusicTimeMS: negative offset fix, seek {} -> {} (remainder={})",
                      ms, adjustedMs, remainder);
            FMODAudioEngine::setMusicTimeMS(static_cast<unsigned int>(adjustedMs), p1, channel);
            return;
        }

        if (totalOffset != 0) {
            int adjustedMs = static_cast<int>(ms) + totalOffset;
            if (adjustedMs < 0) adjustedMs = 0;
            LOG_DEBUG("setMusicTimeMS: applying offset {} ({} -> {})",
                      totalOffset, ms, adjustedMs);
            FMODAudioEngine::setMusicTimeMS(static_cast<unsigned int>(adjustedMs), p1, channel);
            return;
        }

        FMODAudioEngine::setMusicTimeMS(ms, p1, channel);
    }
};
