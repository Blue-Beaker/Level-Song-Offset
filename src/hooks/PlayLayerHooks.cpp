#include <Geode/modify/PlayLayer.hpp>

#include "../OffsetController.hpp"
#include "../OffsetStorage.hpp"
#include "../negativeOffsetWorkaround.hpp"
#include "../AsyncPregenerator.hpp"
#include "../Utils.hpp"
#include "../CacheStorage.hpp"

using namespace geode::prelude;

// ─── Shared: total offset for current PlayLayer ─────────────────────────────
extern int s_currentTotalOffset;

// ─── Hook: PlayLayer ────────────────────────────────────────────────────────

class $modify(MyPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects))
            return false;

        // Start async pre-generation of padded audio files in the background.
        if (m_level) {
            startPregenerateForLevel(m_level);
        }

        return true;
    }

    void prepareMusic(bool dontWait) {
        if (auto* audio = FMODAudioEngine::sharedEngine()) {
            LOG_DEBUG("BEFORE prepareMusic: m_musicOffset={}", audio->m_musicOffset);
        }
        if (m_level) {
            int userOffset = OffsetStorage::getOffsetForLevel(getLevelId(m_level));
            int originalOffset = FMODAudioEngine::sharedEngine()->m_musicOffset;
            int totalOffset = originalOffset + userOffset;

            LOG_DEBUG("prepareMusic: level={}, userOffset={}, originalOffset={}, totalOffset={}",
                      getLevelId(m_level), userOffset, originalOffset, totalOffset);

            s_currentTotalOffset = totalOffset;

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
};

// ─── Hook: PlayLayer::onQuit (cleanup padded file registry) ─────────────────

class $modify(NegativeOffsetPlayLayer, PlayLayer) {
    void onQuit() {
        if (m_level) {
            int mainSongKey = getSongKey(m_level);
            s_paddedPathBySongKey.erase(mainSongKey);
            if (!m_level->m_songIDs.empty()) {
                auto ids = m_level->m_songIDs;
                size_t pos = 0;
                while ((pos = ids.find(',')) != gd::string::npos) {
                    auto idStr = ids.substr(0, pos);
                    ids.erase(0, pos + 1);
                    auto key = geode::utils::numFromString<int>(idStr);
                    if (key) s_paddedPathBySongKey.erase(key.unwrap());
                }
                if (!ids.empty()) {
                    auto key = geode::utils::numFromString<int>(ids);
                    if (key) s_paddedPathBySongKey.erase(key.unwrap());
                }
            }
        }

        PlayLayer::onQuit();
    }
};
