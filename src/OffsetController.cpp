#include "OffsetController.hpp"
#include "OffsetStorage.hpp"
#include "negativeOffsetWorkaround.hpp"

using namespace geode::prelude;

// Conditional debug logging — enabled via the "debug-logging" setting
#define LOG_DEBUG(...) \
    do { if (Mod::get()->getSettingValue<bool>("debug-logging")) \
        log::info(__VA_ARGS__); } while(0)

// Stores the effective totalOffset for the current PlayLayer.
// Set in prepareMusic, read by getAudioFileName, queueStartMusic,
// and setMusicTimeMS hooks.
// totalOffset = original GameManager::m_timeOffset + user offset.
float s_currentTotalOffset = 0.f;

// ─── Hook: PlayLayer ────────────────────────────────────────────────────────
// We don't modify m_musicOffset anymore. Instead, queueStartMusic and
// setMusicTimeMS hooks apply the offset to the start time parameter,
// following the same pattern as jukebox.

bool MyPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
        return false;

    // Pre-register all song keys for this level in s_paddedPathBySongKey,
    // so cache cleanup won't delete files we're about to use.
    if (m_level && Mod::get()->getSettingValue<bool>("negative-offset-fix")) {
        float userOffset = OffsetStorage::getOffsetForLevel(m_level);
        float originalOffset = static_cast<float>(FMODAudioEngine::sharedEngine()->m_musicOffset);
        float totalOffset = originalOffset + userOffset;
        if (totalOffset < 0) {
            int songKey = (m_level->m_songID != 0) ? m_level->m_songID
                                                   : (-m_level->m_audioTrack - 1);
            auto collectKeys = [&](auto&& cb) {
                cb(songKey);
                if (!m_level->m_songIDs.empty()) {
                    auto ids = m_level->m_songIDs;
                    size_t pos = 0;
                    while ((pos = ids.find(',')) != gd::string::npos) {
                        auto idStr = ids.substr(0, pos);
                        ids.erase(0, pos + 1);
                        try { cb(std::stoi(idStr)); } catch (...) {}
                    }
                    if (!ids.empty()) {
                        try { cb(std::stoi(ids)); } catch (...) {}
                    }
                }
            };
            collectKeys([&](int key) {
                if (!s_paddedPathBySongKey.count(key)) {
                    s_paddedPathBySongKey[key] = getPaddedPath(key, static_cast<int>(totalOffset));
                    LOG_DEBUG("Pre-registered song key {} to protect from cache cleanup", key);
                }
            });
            enforceCacheSizeLimit();
        }
    }

    return true;
}

void MyPlayLayer::prepareMusic(bool dontWait) {
    LOG_DEBUG("BEFORE prepareMusic: m_musicOffset={}",
              FMODAudioEngine::sharedEngine()->m_musicOffset);
    if (m_level) {
        float userOffset = OffsetStorage::getOffsetForLevel(m_level);
        float originalOffset = static_cast<float>(FMODAudioEngine::sharedEngine()->m_musicOffset);
        float totalOffset = originalOffset + userOffset;

        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

        LOG_DEBUG("prepareMusic: level={}, userOffset={}, originalOffset={}, totalOffset={}, fixEnabled={}",
                  m_level->m_levelID, userOffset, originalOffset, totalOffset, fixEnabled);

        // Save totalOffset for use by getAudioFileName, queueStartMusic,
        // and setMusicTimeMS hooks.
        s_currentTotalOffset = totalOffset;

        if (totalOffset < 0 && fixEnabled) {
            int songKey = (m_level->m_songID != 0) ? m_level->m_songID
                                                   : (-m_level->m_audioTrack - 1);
            ensurePaddedFile(songKey, static_cast<int>(totalOffset));

            if (!m_level->m_songIDs.empty()) {
                auto ids = m_level->m_songIDs;
                size_t pos = 0;
                while ((pos = ids.find(',')) != gd::string::npos) {
                    auto idStr = ids.substr(0, pos);
                    ids.erase(0, pos + 1);
                    int extraSongId = std::stoi(idStr);
                    ensurePaddedFile(extraSongId, static_cast<int>(totalOffset));
                }
                if (!ids.empty()) {
                    ensurePaddedFile(std::stoi(ids), static_cast<int>(totalOffset));
                }
            }
        }
    }

    // Don't modify m_musicOffset — queueStartMusic/setMusicTimeMS hooks
    // will apply the offset to the start time parameter instead.
    PlayLayer::prepareMusic(dontWait);
    LOG_DEBUG("AFTER prepareMusic: m_musicOffset={}",
              FMODAudioEngine::sharedEngine()->m_musicOffset);
}

// ─── Hook: FMODAudioEngine::queueStartMusic ─────────────────────────────────
//
// Song Triggers call queueStartMusic to play music during gameplay.
// The `start` parameter (6th int) is the playback start time in ms.
// The `musicID` parameter (10th int) identifies the song being played.
//
// This hook handles TWO concerns:
//
// 1. POSITIVE OFFSET (or negative without fix):
//    Add the per-level offset to the `start` parameter.
//
// 2. NEGATIVE OFFSET WITH FIX ENABLED:
//    Redirect the audio path to a padded WAV file that has silence
//    prepended. The offset is baked into the file, so we do NOT modify
//    the `start` parameter.
//
// Song key resolution uses musicID when available, falling back to
// extractSongIdFromPath for cases where musicID is 0.

void MyFMODAudioEngine::queueStartMusic(gd::string audioFilename, float pitch,
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

    float totalOffset = s_currentTotalOffset;
    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

    // ── Case 1: Negative offset with fix enabled → redirect to padded WAV ──
    if (totalOffset < 0 && fixEnabled) {
        // If path is already a padded file, pass through directly
        if (audioFilename.find("padded_") != gd::string::npos) {
            FMODAudioEngine::queueStartMusic(
                audioFilename, pitch, unknown, volume, loop, start, end,
                fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
            );
            return;
        }

        // Resolve song key: prefer musicID parameter, fall back to path parsing
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

        // Verify this song key is relevant to the current level
        int levelSongKey = getSongKey(pl->m_level);
        bool relevant = (songKey == levelSongKey);
        if (!relevant && !pl->m_level->m_songIDs.empty()) {
            auto ids = pl->m_level->m_songIDs;
            size_t p = 0;
            while ((p = ids.find(',')) != gd::string::npos) {
                auto idStr = ids.substr(0, p);
                ids.erase(0, p + 1);
                try { if (std::stoi(idStr) == songKey) { relevant = true; break; } }
                catch (...) {}
            }
            if (!relevant && !ids.empty()) {
                try { if (std::stoi(ids) == songKey) relevant = true; }
                catch (...) {}
            }
        }
        if (!relevant) {
            FMODAudioEngine::queueStartMusic(
                audioFilename, pitch, unknown, volume, loop, start, end,
                fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
            );
            return;
        }

        auto paddedPath = getPaddedPath(songKey, static_cast<int>(totalOffset));

        // Ensure the padded file exists
        std::error_code ec;
        if (!std::filesystem::exists(paddedPath, ec)) {
            auto it = s_paddedPathBySongKey.find(songKey);
            if (it != s_paddedPathBySongKey.end() && std::filesystem::exists(it->second, ec)) {
                paddedPath = it->second;
            } else {
                auto* fileUtils = CCFileUtils::sharedFileUtils();
                std::string fullPath = fileUtils->fullPathForFilename(audioFilename.c_str(), false);
                if (!fullPath.empty()) {
                    std::filesystem::path actualSourcePath(fullPath);
                    if (std::filesystem::exists(actualSourcePath)) {
                        int intervalMs = ((static_cast<int>(std::abs(totalOffset)) + 999) / 1000) * 1000;
                        if (createPaddedWavFile(actualSourcePath, paddedPath, intervalMs)) {
                            s_paddedPathBySongKey[songKey] = paddedPath;
                        }
                    }
                }
            }
        }

        if (std::filesystem::exists(paddedPath, ec)) {
            // Padded file has intervalMs of silence. We need to skip
            // `remainder` ms so the effective offset equals totalOffset.
            //   remainder = intervalMs - abs(totalOffset)
            int intervalMs = ((static_cast<int>(std::abs(totalOffset)) + 999) / 1000) * 1000;
            int remainder = intervalMs - static_cast<int>(std::abs(totalOffset));
            int adjustedStart = start + remainder;
            LOG_DEBUG("queueStartMusic: negative offset, redirect {} -> {}, start {} -> {} (remainder={})",
                      audioFilename, paddedPath.string(), start, adjustedStart, remainder);
            FMODAudioEngine::queueStartMusic(
                gd::string(paddedPath.string()), pitch, unknown, volume, loop,
                adjustedStart, end, fadeIn, fadeOut, musicID, p10, channelID,
                noPrepare, dontReset
            );
        } else {
            FMODAudioEngine::queueStartMusic(
                audioFilename, pitch, unknown, volume, loop, start, end,
                fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
            );
        }
        return;
    }

    // ── Case 2: Positive offset (or negative without fix) → adjust start ──
    if (totalOffset != 0.f) {
        int adjustedStart = start + static_cast<int>(totalOffset);
        if (adjustedStart < 0) adjustedStart = 0;
        LOG_DEBUG("queueStartMusic: applying offset {} to start ({} -> {}), musicID={}",
                  static_cast<int>(totalOffset), start, adjustedStart, musicID);
        start = adjustedStart;
    }

    FMODAudioEngine::queueStartMusic(
        audioFilename, pitch, unknown, volume, loop, start, end,
        fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
    );
}

// ─── Hook: FMODAudioEngine::setMusicTimeMS ──────────────────────────────────
// Handles seek operations during gameplay (e.g. practice mode, song trigger
// repositioning). Follows the same pattern as jukebox.

void MyFMODAudioEngine::setMusicTimeMS(unsigned int ms, bool p1, int channel) {
    float totalOffset = s_currentTotalOffset;
    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

    // For negative offset with fix enabled, the padded file has intervalMs
    // of silence prepended. We need to add `remainder` to the seek position
    // so the effective offset equals totalOffset.
    //   intervalMs = ceil(abs(totalOffset)/1000)*1000
    //   remainder = intervalMs - abs(totalOffset)
    if (totalOffset < 0 && fixEnabled) {
        int intervalMs = ((static_cast<int>(std::abs(totalOffset)) + 999) / 1000) * 1000;
        int remainder = intervalMs - static_cast<int>(std::abs(totalOffset));
        int adjustedMs = static_cast<int>(ms) + remainder;
        LOG_DEBUG("setMusicTimeMS: negative offset fix, seek {} -> {} (remainder={})",
                  ms, adjustedMs, remainder);
        FMODAudioEngine::setMusicTimeMS(static_cast<unsigned int>(adjustedMs), p1, channel);
        return;
    }

    if (totalOffset != 0.f) {
        int adjustedMs = static_cast<int>(ms) + static_cast<int>(totalOffset);
        if (adjustedMs < 0) adjustedMs = 0;
        LOG_DEBUG("setMusicTimeMS: applying offset {} ({} -> {})",
                  static_cast<int>(totalOffset), ms, adjustedMs);
        FMODAudioEngine::setMusicTimeMS(static_cast<unsigned int>(adjustedMs), p1, channel);
        return;
    }

    FMODAudioEngine::setMusicTimeMS(ms, p1, channel);
}
