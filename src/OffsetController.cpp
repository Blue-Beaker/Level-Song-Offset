#include "OffsetController.hpp"
#include "OffsetStorage.hpp"

using namespace geode::prelude;

// Stores the original FMODAudioEngine::m_musicOffset per PlayLayer instance
static std::unordered_map<PlayLayer*, int> s_originalMusicOffsets;

// Re-entry guard: when set, startMusic() skips the negative-offset delay
// and calls the original PlayLayer::startMusic() directly.
static bool s_isDelayedStart = false;

bool MyPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
        return false;

    // Save the original m_musicOffset (set by GameManager::m_timeOffset)
    // so we can restore it on quit. We don't modify m_musicOffset here
    // anymore — all offset handling is done in startMusic().
    auto* audio = FMODAudioEngine::sharedEngine();
    s_originalMusicOffsets[this] = audio->m_musicOffset;

    return true;
}

void MyPlayLayer::startMusic() {
    auto* audio = FMODAudioEngine::sharedEngine();
    float userOffset = m_level ? OffsetStorage::getOffsetForLevel(m_level->m_levelID) : 0.f;

    // Calculate the total effective offset:
    //   original m_musicOffset (from GameManager::m_timeOffset)
    //   + userOffset (set by this mod)
    int originalOffset = s_originalMusicOffsets[this];
    int totalOffset = originalOffset + static_cast<int>(userOffset);

    bool negativeFixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

    if (totalOffset < 0 && negativeFixEnabled && !s_isDelayedStart) {
        // ── Negative total offset workaround ──
        // Negative offset means: delay the music by |offset| ms.
        // Don't start music now; schedule it after the delay.
        audio->m_musicOffset = totalOffset;

        float delaySec = std::abs(totalOffset) / 1000.f;
        this->runAction(
            CCSequence::create(
                CCDelayTime::create(delaySec),
                CCCallFunc::create(this, callfunc_selector(MyPlayLayer::applyDelayedMusic)),
                nullptr
            )
        );
    } else {
        // ── Zero or positive total offset (or fix disabled, or re-entry) ──
        // Set m_musicOffset so PlayLayer::startMusic() uses the correct start time.
        // On re-entry from applyDelayedMusic, m_musicOffset is already set to 0
        // so the original function won't receive a negative start time.
        if (!s_isDelayedStart) {
            audio->m_musicOffset = totalOffset;
        }
        PlayLayer::startMusic();
    }
}

void MyPlayLayer::applyDelayedMusic() {
    // Re-enter startMusic but bypass the delay logic.
    // Set m_musicOffset = 0 so the original PlayLayer::startMusic() doesn't
    // receive a negative start time (which would break FMOD).
    s_isDelayedStart = true;
    auto* audio = FMODAudioEngine::sharedEngine();
    audio->m_musicOffset = 0;
    PlayLayer::startMusic();
    // Force the music to start from the beginning, in case GD's internal
    // logic restored m_musicOffset from GameManager::m_timeOffset
    audio->setMusicTimeMS(0, false, 0);
    s_isDelayedStart = false;
}

void MyPlayLayer::onQuit() {
    // Restore the original global music offset
    auto it = s_originalMusicOffsets.find(this);
    if (it != s_originalMusicOffsets.end()) {
        FMODAudioEngine::sharedEngine()->m_musicOffset = it->second;
        s_originalMusicOffsets.erase(it);
    }
    PlayLayer::onQuit();
}
