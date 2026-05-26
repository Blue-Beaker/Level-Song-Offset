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
        //
        // We use the same approach as GD's internal "Edit Song" trigger:
        //   1. Start music normally with m_musicOffset = 0 so all GD state
        //      (level sync, checkpoint restore) is set up correctly
        //   2. Immediately set background music channel volume to 0 at the
        //      FMOD mixer level — this silences audio even if the mixer
        //      has already processed a frame (unlike setPaused which may
        //      let a brief audio snippet through)
        //   3. After |totalOffset| ms delay, restore volume and seek to
        //      the beginning with dontWait=true to avoid stutter

        // Cancel any pending delayed-resume from a previous attempt
        // (retry/reset doesn't call onQuit, so the old schedule may still be alive).
        this->unschedule(schedule_selector(MyPlayLayer::applyDelayedMusic));

        audio->m_musicOffset = 0;
        PlayLayer::startMusic();

        // Immediately mute at the FMOD channel-group level.
        // This is more reliable than setPaused(true) because FMOD's mixer
        // runs asynchronously — setVolume(0) guarantees zero audio output
        // even if the mixer has already processed audio for this frame.
        float savedBgVolume = 1.f;
        if (audio->m_backgroundMusicChannel) {
            audio->m_backgroundMusicChannel->getVolume(&savedBgVolume);
            audio->m_backgroundMusicChannel->setVolume(0.f);
        }

        // Store the total offset so applyDelayedMusic can use it
        audio->m_musicOffset = totalOffset;

        // Schedule the resume after the delay
        m_fields->m_savedBgVolume = savedBgVolume;
        float delaySec = std::abs(totalOffset) / 1000.f;
        this->scheduleOnce(
            schedule_selector(MyPlayLayer::applyDelayedMusic),
            delaySec
        );
    } else {
        // ── Zero or positive total offset (or fix disabled) ──
        // Set m_musicOffset so PlayLayer::startMusic() uses the correct start time
        audio->m_musicOffset = totalOffset;
        PlayLayer::startMusic();
    }
}

void MyPlayLayer::applyDelayedMusic(float dt) {
    auto* audio = FMODAudioEngine::sharedEngine();

    // Restore the background music channel volume
    if (audio->m_backgroundMusicChannel) {
        audio->m_backgroundMusicChannel->setVolume(m_fields->m_savedBgVolume);
    }

    // Seek to cancel out the negative offset.
    // setMusicTimeMS applies m_musicOffset internally (actualPos = time - m_musicOffset),
    // so to land at actual time 0 we seek to time = -m_musicOffset = |totalOffset|.
    // This avoids temporarily mutating m_musicOffset.
    // Use dontWait=true to avoid stutter from waiting on FMOD async loading.
    audio->setMusicTimeMS(-audio->m_musicOffset, true, 0);
}

void MyPlayLayer::onQuit() {
    // Cancel any pending delayed-resume so it doesn't fire after
    // this PlayLayer is gone (e.g. on rapid retry).
    this->unschedule(schedule_selector(MyPlayLayer::applyDelayedMusic));

    // Restore the background music channel volume if we muted it
    // (in case we quit while the music was still muted)
    auto* audio = FMODAudioEngine::sharedEngine();
    if (audio->m_backgroundMusicChannel) {
        audio->m_backgroundMusicChannel->setVolume(m_fields->m_savedBgVolume);
    }

    // Restore the original global music offset
    auto it = s_originalMusicOffsets.find(this);
    if (it != s_originalMusicOffsets.end()) {
        audio->m_musicOffset = it->second;
        s_originalMusicOffsets.erase(it);
    }
    PlayLayer::onQuit();
}
