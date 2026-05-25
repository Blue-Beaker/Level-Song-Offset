#include "OffsetController.hpp"
#include "OffsetStorage.hpp"

using namespace geode::prelude;

// Stores the original FMODAudioEngine::m_musicOffset per PlayLayer instance
static std::unordered_map<PlayLayer*, int> s_originalMusicOffsets;

bool MyPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
        return false;

    // Apply user offset to the global music offset for the entire
    // lifetime of this PlayLayer (survives death restarts, etc.)
    float userOffset = OffsetStorage::getOffsetForLevel(level->m_levelID);
    if (userOffset != 0.f) {
        auto* audio = FMODAudioEngine::sharedEngine();
        s_originalMusicOffsets[this] = audio->m_musicOffset;
        audio->m_musicOffset += static_cast<int>(userOffset);
    }

    return true;
}

void MyPlayLayer::startMusic() {
    float userOffset = m_level ? OffsetStorage::getOffsetForLevel(m_level->m_levelID) : 0.f;

    if (userOffset < 0.f && Mod::get()->getSettingValue<bool>("negative-offset-fix")) {
        // Negative offset with fix: start normally, then pause and delay
        PlayLayer::startMusic();

        auto* audio = FMODAudioEngine::sharedEngine();
        audio->setMusicTimeMS(0, false, 0);
        audio->pauseAllMusic(true);

        float delaySec = std::abs(userOffset) / 1000.f;
        this->runAction(
            CCSequence::create(
                CCDelayTime::create(delaySec),
                CCCallFunc::create(this, callfunc_selector(MyPlayLayer::applyDelayedMusic)),
                nullptr
            )
        );
    } else {
        PlayLayer::startMusic();
    }
}

void MyPlayLayer::applyDelayedMusic() {
    auto* audio = FMODAudioEngine::sharedEngine();
    audio->setMusicTimeMS(0, false, 0);
    audio->resumeAllMusic();
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
