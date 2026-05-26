#include "OffsetController.hpp"
#include "OffsetStorage.hpp"

using namespace geode::prelude;

// Stores the original FMODAudioEngine::m_musicOffset per PlayLayer instance
static std::unordered_map<PlayLayer*, int> s_originalMusicOffsets;

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

    if (totalOffset < 0 && negativeFixEnabled) {
        // ── Negative total offset workaround ──
        // Negative offset means: delay the music by |offset| ms.
        // Start the music normally so all GD state is set up, then
        // pause it immediately. After the delay, seek to beginning and resume.
        // Stop any pending delayed-start action from a previous attempt
        // (retry/reset doesn't call onQuit, so the old action may still be alive).
        this->stopAllActions();

        audio->m_musicOffset = 0;
        PlayLayer::startMusic();
        audio->pauseAllMusic(true);

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
        // Set m_musicOffset so PlayLayer::startMusic() uses the correct start time
        audio->m_musicOffset = totalOffset;
        PlayLayer::startMusic();
    }
}

void MyPlayLayer::applyDelayedMusic() {
    auto* audio = FMODAudioEngine::sharedEngine();
    // setMusicTimeMS applies m_musicOffset internally, so temporarily set
    // it to 0 to seek to the true beginning, then restore it.
    int savedOffset = audio->m_musicOffset;
    audio->m_musicOffset = 0;
    audio->setMusicTimeMS(0, false, 0);
    audio->m_musicOffset = savedOffset;
    audio->resumeAllMusic();
}

void MyPlayLayer::onQuit() {
    // Stop any pending delayed-start action so it doesn't fire after
    // this PlayLayer is gone (e.g. on rapid retry).
    this->stopAllActions();

    // Restore the original global music offset
    auto it = s_originalMusicOffsets.find(this);
    if (it != s_originalMusicOffsets.end()) {
        FMODAudioEngine::sharedEngine()->m_musicOffset = it->second;
        s_originalMusicOffsets.erase(it);
    }
    PlayLayer::onQuit();
}
