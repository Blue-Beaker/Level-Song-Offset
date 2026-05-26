#include "OffsetController.hpp"
#include "OffsetStorage.hpp"
#include "negativeOffsetWorkaround.hpp"

using namespace geode::prelude;

// Conditional debug logging — enabled via the "debug-logging" setting
#define LOG_DEBUG(...) \
    do { if (Mod::get()->getSettingValue<bool>("debug-logging")) \
        log::info(__VA_ARGS__); } while(0)

// Stores the original FMODAudioEngine::m_musicOffset per PlayLayer instance
static std::unordered_map<PlayLayer*, int> s_originalMusicOffset;

// Stores the effective totalOffset for the current PlayLayer.
// Set in prepareMusic, read by getAudioFileName and queueStartMusic hooks.
// This is needed because prepareMusic modifies m_musicOffset to remainder,
// so we can't rely on m_musicOffset to reconstruct totalOffset later.
float s_currentTotalOffset = 0.f;

// ─── Hook: PlayLayer::prepareMusic ───────────────────────────────────────────
// This is called during PlayLayer::init, right before the audio file is
// loaded.  At this point m_level is available and we can set the offset.
// This is the LAST chance to set the offset before GD reads it.

bool MyPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
        return false;

    s_originalMusicOffset[this] = FMODAudioEngine::sharedEngine()->m_musicOffset;

    // Pre-register all song keys for this level in s_paddedPathBySongKey,
    // so cache cleanup won't delete files we're about to use.
    if (m_level && Mod::get()->getSettingValue<bool>("negative-offset-fix")) {
        float userOffset = OffsetStorage::getOffsetForLevel(m_level);
        float originalOffset = static_cast<float>(s_originalMusicOffset[this]);
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
        float originalOffset = static_cast<float>(s_originalMusicOffset[this]);
        float totalOffset = originalOffset + userOffset;

        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

        LOG_DEBUG("prepareMusic: level={}, userOffset={}, originalOffset={}, totalOffset={}, fixEnabled={}",
                  m_level->m_levelID, userOffset, originalOffset, totalOffset, fixEnabled);

        // Save totalOffset for use by getAudioFileName and queueStartMusic hooks
        s_currentTotalOffset = totalOffset;

        if (totalOffset < 0 && fixEnabled) {
            int absTotal = static_cast<int>(std::abs(totalOffset));

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

            // Run cache cleanup once per PlayLayer. Before that, ensure all
            // song keys used by this level are registered in s_paddedPathBySongKey
            // (with whatever path they already have or will have), so the cleanup
            // doesn't delete files this level is about to use.
            int intervalMs = ((absTotal + 999) / 1000) * 1000;

            int remainder  = intervalMs - absTotal;
            // Set m_musicOffset directly — this is what PlayLayer::startMusic
            // reads.  Leave GameManager::m_timeOffset untouched so the
            // game setting isn't affected.
            FMODAudioEngine::sharedEngine()->m_musicOffset = remainder;
            LOG_DEBUG("prepareMusic: negative offset fix, intervalMs={}, remainder={}, m_musicOffset={}",
                      intervalMs, remainder,
                      FMODAudioEngine::sharedEngine()->m_musicOffset);
        } else {
            FMODAudioEngine::sharedEngine()->m_musicOffset = static_cast<int>(totalOffset);
            LOG_DEBUG("prepareMusic: set m_musicOffset={}", static_cast<int>(totalOffset));
        }
    }

    PlayLayer::prepareMusic(dontWait);
    LOG_DEBUG("AFTER prepareMusic: m_musicOffset={}",
              FMODAudioEngine::sharedEngine()->m_musicOffset);
}

void MyPlayLayer::startMusic() {
    LOG_DEBUG("BEFORE startMusic: m_musicOffset={}",
              FMODAudioEngine::sharedEngine()->m_musicOffset);

    PlayLayer::startMusic();

    LOG_DEBUG("AFTER startMusic: m_musicOffset={}",
              FMODAudioEngine::sharedEngine()->m_musicOffset);
}

void MyPlayLayer::onQuit() {
    auto it = s_originalMusicOffset.find(this);
    if (it != s_originalMusicOffset.end()) {
        FMODAudioEngine::sharedEngine()->m_musicOffset = it->second;
        s_originalMusicOffset.erase(it);
    }
    PlayLayer::onQuit();
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
//    Add the user's per-level offset to the `start` parameter so the
//    offset applies to ALL song trigger music changes.
//
// 2. NEGATIVE OFFSET WITH FIX ENABLED:
//    Redirect the audio path to a padded WAV file that has silence
//    prepended. The offset is baked into the file, so we do NOT modify
//    the `start` parameter. This logic was moved from
//    PaddedQueueMusicFMODAudioEngine to avoid hook conflicts.
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

    float userOffset = OffsetStorage::getOffsetForLevel(pl->m_level);
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
            // Check if there's already a padded file registered from prepareMusic
            auto it = s_paddedPathBySongKey.find(songKey);
            if (it != s_paddedPathBySongKey.end() && std::filesystem::exists(it->second, ec)) {
                paddedPath = it->second;
            } else {
                // Locate the original source file via MusicDownloadManager
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
            LOG_DEBUG("queueStartMusic: negative offset, redirect {} -> {}", audioFilename, paddedPath.string());
            FMODAudioEngine::queueStartMusic(
                gd::string(paddedPath.string()), pitch, unknown, volume, loop,
                start, end, fadeIn, fadeOut, musicID, p10, channelID,
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
