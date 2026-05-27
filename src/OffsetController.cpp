#include "OffsetController.hpp"
#include "OffsetStorage.hpp"
#include "negativeOffsetWorkaround.hpp"
#include "AsyncPregenerator.hpp"
#include "Utils.hpp"

// Stores the effective totalOffset (ms) for the current PlayLayer.
// Set in prepareMusic, read by getAudioFileName, queueStartMusic,
// and setMusicTimeMS hooks.
// totalOffset = original GameManager::m_timeOffset + user offset.
int s_currentTotalOffset = 0;

// ─── Public: start pre-generation for a level ───────────────────────────────

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

    auto& pregen = AsyncPregenerator::get();
    if (pregen.isRunning()) {
        LOG_DEBUG("startPregenerateForLevel: generation already in progress, skipping");
        return;
    }

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
                auto key = geode::utils::numFromString<int>(idStr);
                if (key) registerKey(key.unwrap());
            }
            if (!ids.empty()) {
                auto key = geode::utils::numFromString<int>(ids);
                if (key) registerKey(key.unwrap());
            }
        }
    }

    enforceCacheSizeLimit();
}
