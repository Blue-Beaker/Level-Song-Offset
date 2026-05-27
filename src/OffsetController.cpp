#include "OffsetController.hpp"
#include "OffsetStorage.hpp"
#include "negativeOffsetWorkaround.hpp"
#include "AsyncPregenerator.hpp"
#include "LevelUtils.hpp"

using namespace geode::prelude;

// Conditional debug logging — enabled via the "debug-logging" setting
#define LOG_DEBUG(...) \
    do { if (Mod::get()->getSettingValue<bool>("debug-logging")) \
        log::info(__VA_ARGS__); } while(0)

// Stores the effective totalOffset (ms) for the current PlayLayer.
// Set in prepareMusic, read by getAudioFileName, queueStartMusic,
// and setMusicTimeMS hooks.
// totalOffset = original GameManager::m_timeOffset + user offset.
int s_currentTotalOffset = 0;

// ─── Public: start pre-generation for a level ───────────────────────────────
//
// Called from MyPlayLayer::init and OffsetPopup::onApply. Starts async
// generation for all songs in the level that need the negative offset
// workaround. Already-cached files are skipped.
//
// If generation is already running (e.g. from MyPlayLayer::init while the
// user opens the popup and applies a new offset), this call is silently
// ignored to avoid duplicate work.
//
// Also pre-registers paths in s_paddedPathBySongKey so cache cleanup won't
// delete files we're about to generate, and enforces the cache size limit.

void startPregenerateForLevel(GJGameLevel* level) {
    if (!level) return;
    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");
    if (!fixEnabled) return;

    auto* audio = FMODAudioEngine::sharedEngine();
    if (!audio) return;

    int userOffset = OffsetStorage::getOffsetForLevel(getLevelId(level));
    int originalOffset = audio->m_musicOffset;
    int totalOffset = originalOffset + userOffset;

    if (totalOffset >= 0) return;

    // Don't start a new generation if one is already in progress
    auto& pregen = AsyncPregenerator::get();
    if (pregen.isRunning()) {
        LOG_DEBUG("startPregenerateForLevel: generation already in progress, skipping");
        return;
    }

    // Collect tasks (skips already-cached files inside collectPregenerateTasks)
    auto tasks = collectPregenerateTasks(level, totalOffset);

    if (!tasks.empty()) {
        pregen.generate(std::move(tasks), nullptr);
    }

    // Pre-register paths so cache cleanup won't delete them
    {
        int songKey = (level->m_songID != 0) ? level->m_songID
                                              : (-level->m_audioTrack - 1);
        auto registerKey = [&](int key) {
            if (!s_paddedPathBySongKey.count(key)) {
                s_paddedPathBySongKey[key] = getPaddedPath(key, totalOffset);
                LOG_DEBUG("Pre-registered song key {} to protect from cache cleanup", key);
            }
        };
        registerKey(songKey);
        if (!level->m_songIDs.empty()) {
            auto ids = level->m_songIDs;
            size_t pos = 0;
            while ((pos = ids.find(',')) != gd::string::npos) {
                auto idStr = ids.substr(0, pos);
                ids.erase(0, pos + 1);
                try { registerKey(geode::utils::numFromString<int>(idStr).unwrapOr(0)); } catch (...) {}
            }
            if (!ids.empty()) {
                try { registerKey(geode::utils::numFromString<int>(ids).unwrapOr(0)); } catch (...) {}
            }
        }
    }

    enforceCacheSizeLimit();
}

// ─── Hook: PlayLayer ────────────────────────────────────────────────────────

bool MyPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
        return false;

    // Start async pre-generation of padded audio files in the background.
    // If files are already cached, they're skipped. If generation doesn't
    // finish before a song plays, queueStartMusic falls back to the
    // original file with offset=0.
    if (m_level) {
        startPregenerateForLevel(m_level);
    }

    return true;
}

void MyPlayLayer::prepareMusic(bool dontWait) {
    if (auto* audio = FMODAudioEngine::sharedEngine()) {
        LOG_DEBUG("BEFORE prepareMusic: m_musicOffset={}", audio->m_musicOffset);
    }
    if (m_level) {
        int userOffset = OffsetStorage::getOffsetForLevel(getLevelId(m_level));
        int originalOffset = FMODAudioEngine::sharedEngine()->m_musicOffset;
        int totalOffset = originalOffset + userOffset;

        LOG_DEBUG("prepareMusic: level={}, userOffset={}, originalOffset={}, totalOffset={}",
                  getLevelId(m_level), userOffset, originalOffset, totalOffset);

        // Save totalOffset for use by getAudioFileName, queueStartMusic,
        // and setMusicTimeMS hooks.
        s_currentTotalOffset = totalOffset;

        // Wait for async pre-generation to finish so all padded files are
        // ready before music starts playing. The worker threads run in the
        // background while PlayLayer finishes setting up, so by the time
        // prepareMusic is called they should be nearly done or already done.
        if (totalOffset < 0 && Mod::get()->getSettingValue<bool>("negative-offset-fix")) {
            auto& pregen = AsyncPregenerator::get();
            if (pregen.isRunning()) {
                LOG_DEBUG("prepareMusic: waiting for pre-generation...");
                pregen.waitAll();
                LOG_DEBUG("prepareMusic: pre-generation done");
            }
        }
    }

    PlayLayer::prepareMusic(dontWait);
    if (auto* audio = FMODAudioEngine::sharedEngine()) {
        LOG_DEBUG("AFTER prepareMusic: m_musicOffset={}", audio->m_musicOffset);
    }
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
//    Redirect the audio path to a padded file that has silence
//    prepended. The offset is baked into the file, so we do NOT modify
//    the `start` parameter.
//
//    If the padded file doesn't exist yet (async generation still in
//    progress or failed), we fallback to the original file and set
//    offset to 0 — no offset is better than broken audio.

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

    int totalOffset = s_currentTotalOffset;
    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

    // ── Case 1: Negative offset with fix enabled → redirect to padded file ──
    if (totalOffset < 0 && fixEnabled) {
        // If path is already a padded file, pass through directly
        if (audioFilename.find("padded_") != gd::string::npos) {
            FMODAudioEngine::queueStartMusic(
                audioFilename, pitch, unknown, volume, loop, start, end,
                fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
            );
            return;
        }

        // Resolve song key
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
                try { if (geode::utils::numFromString<int>(idStr).unwrapOr(0) == songKey) { relevant = true; break; } }
                catch (...) {}
            }
            if (!relevant && !ids.empty()) {
                try { if (geode::utils::numFromString<int>(ids).unwrapOr(0) == songKey) relevant = true; }
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

        auto paddedPath = getPaddedPath(songKey, totalOffset);
        std::error_code ec;

        if (std::filesystem::exists(paddedPath, ec)) {
            // Padded file is ready — use it with adjusted start
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
            // Padded file not ready — fallback to original with unchanged start,
            // but without the padded-file adjusted offset.
            // This can happen if async generation hasn't finished yet or failed.
            LOG_DEBUG("queueStartMusic: padded file not ready for song {}, "
                      "falling back to original", songKey);
            FMODAudioEngine::queueStartMusic(
                audioFilename, pitch, unknown, volume, loop, start, end,
                fadeIn, fadeOut, musicID, p10, channelID, noPrepare, dontReset
            );
        }
        return;
    }

    // ── Case 2: Positive offset (or negative without fix) → adjust start ──
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

// ─── Hook: FMODAudioEngine::setMusicTimeMS ──────────────────────────────────
// Handles seek operations during gameplay (e.g. practice mode, song trigger
// repositioning). Follows the same pattern as jukebox.

void MyFMODAudioEngine::setMusicTimeMS(unsigned int ms, bool p1, int channel) {
    int totalOffset = s_currentTotalOffset;
    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

    // For negative offset with fix enabled, the padded file has intervalMs
    // of silence prepended. We need to add `remainder` to the seek position
    // so the effective offset equals totalOffset.
    //   intervalMs = ceil(abs(totalOffset)/1000)*1000
    //   remainder = intervalMs - abs(totalOffset)
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
