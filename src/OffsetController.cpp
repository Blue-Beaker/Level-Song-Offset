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

// ─── Hook: PlayLayer::prepareMusic ───────────────────────────────────────────
// This is called during PlayLayer::init, right before the audio file is
// loaded.  At this point m_level is available and we can set the offset.
// This is the LAST chance to set the offset before GD reads it.

bool MyPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
        return false;

    s_originalMusicOffset[this] = FMODAudioEngine::sharedEngine()->m_musicOffset;
    return true;
}

void MyPlayLayer::prepareMusic(bool dontWait) {
    LOG_DEBUG("BEFORE prepareMusic: m_musicOffset={}",
              FMODAudioEngine::sharedEngine()->m_musicOffset);
    if (m_level) {
        float userOffset = OffsetStorage::getOffsetForLevel(m_level->m_levelID);
        float originalOffset = static_cast<float>(s_originalMusicOffset[this]);
        float totalOffset = originalOffset + userOffset;

        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

        LOG_DEBUG("prepareMusic: level={}, userOffset={}, originalOffset={}, totalOffset={}, fixEnabled={}",
                  m_level->m_levelID, userOffset, originalOffset, totalOffset, fixEnabled);

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
