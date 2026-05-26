#include "OffsetController.hpp"
#include "OffsetStorage.hpp"

using namespace geode::prelude;

// Stores the original FMODAudioEngine::m_musicOffset per PlayLayer instance
static std::unordered_map<PlayLayer*, int> s_originalMusicOffsets;

bool MyPlayLayer::init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    if (!PlayLayer::init(level, useReplay, dontCreateObjects))
        return false;

    // Save the original m_musicOffset (set by GameManager::m_timeOffset)
    // so we can restore it on quit.
    auto* audio = FMODAudioEngine::sharedEngine();
    s_originalMusicOffsets[this] = audio->m_musicOffset;

    return true;
}

void MyPlayLayer::startMusic() {
    auto* audio = FMODAudioEngine::sharedEngine();
    float userOffset = m_level ? OffsetStorage::getOffsetForLevel(m_level->m_levelID) : 0.f;

    int originalOffset = s_originalMusicOffsets[this];
    int totalOffset = originalOffset + static_cast<int>(userOffset);

    // For negative offsets, the NegativeOffsetWorkaround module handles
    // everything via the silence-prefix approach — it sets m_musicOffset = 0
    // and redirects the audio file.  We just need to avoid overriding that.
    //
    // For zero or positive offsets, set m_musicOffset so the music starts
    // at the correct position (skipping |offset| ms from the beginning).
    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");
    if(fixEnabled){
        audio->m_musicOffset = std::max(0, totalOffset);
    }else{
        audio->m_musicOffset = totalOffset;
    }
    PlayLayer::startMusic();
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
