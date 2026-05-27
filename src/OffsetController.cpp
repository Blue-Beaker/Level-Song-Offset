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

    // collectPregenerateTasks already registers cached files in
    // s_paddedPathByFileKey and enforceCacheSizeLimit is called inside it.
    enforceCacheSizeLimit();
}
