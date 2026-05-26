#include "negativeOffsetWorkaround.hpp"
#include "OffsetStorage.hpp"

#include <Geode/modify/GJGameLevel.hpp>
#include <Geode/modify/FMODAudioEngine.hpp>

#include <fmod.hpp>

using namespace geode::prelude;

// ─── WAV file helpers ────────────────────────────────────────────────────────

// Standard 44-byte PCM WAV header
struct WavHeader {
    char riff[4]     = {'R', 'I', 'F', 'F'};
    uint32_t fileSize = 0;  // total file size - 8
    char wave[4]     = {'W', 'A', 'V', 'E'};
    char fmtId[4]    = {'f', 'm', 't', ' '};
    uint32_t fmtSize = 16;  // 16 for PCM
    uint16_t audioFormat  = 1;  // 1 = PCM
    uint16_t numChannels  = 0;
    uint32_t sampleRate   = 0;
    uint32_t byteRate     = 0;
    uint16_t blockAlign   = 0;
    uint16_t bitsPerSample = 0;
    char dataId[4]   = {'d', 'a', 't', 'a'};
    uint32_t dataSize = 0;
};
static_assert(sizeof(WavHeader) == 44, "WavHeader must be exactly 44 bytes");

/**
 * Decode the source audio file to PCM, prepend |padMs| ms of silence,
 * and write the result as a standard WAV file.
 *
 * Uses FMOD to handle all decoding (supports MP3, OGG, WAV, etc.).
 *
 * @return true on success
 */
static bool createPaddedWavFile(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destPath,
    int padMs
) {
    auto* audio = FMODAudioEngine::sharedEngine();
    if (!audio->m_system) return false;

    // Load the original file as a sample (decodes to PCM in memory)
    FMOD::Sound* srcSound = nullptr;
    FMOD_RESULT res = audio->m_system->createSound(
        sourcePath.string().c_str(),
        FMOD_CREATESAMPLE | FMOD_LOOP_OFF,
        nullptr,
        &srcSound
    );
    if (res != FMOD_OK || !srcSound) {
        log::error("Failed to load source audio for padding: {}", sourcePath.string());
        return false;
    }

    // Get format info
    FMOD_SOUND_FORMAT soundFormat;
    int numChannels, bitsPerSample;
    unsigned int srcLengthBytes;
    srcSound->getFormat(nullptr, &soundFormat, &numChannels, &bitsPerSample);
    srcSound->getLength(&srcLengthBytes, FMOD_TIMEUNIT_PCMBYTES);

    // Calculate sample rate from PCM samples and duration
    unsigned int srcLengthPCM = 0;
    unsigned int srcLengthMs  = 0;
    srcSound->getLength(&srcLengthPCM, FMOD_TIMEUNIT_PCM);
    srcSound->getLength(&srcLengthMs,  FMOD_TIMEUNIT_MS);
    unsigned int sampleRate = 44100; // fallback
    if (srcLengthMs > 0) {
        sampleRate = static_cast<unsigned int>(
            static_cast<uint64_t>(srcLengthPCM) * 1000ULL / srcLengthMs
        );
    }

    unsigned int bytesPerSample = bitsPerSample / 8;
    unsigned int blockAlign     = bytesPerSample * numChannels;

    // Number of bytes of silence to prepend
    unsigned int padBytes = static_cast<unsigned int>(
        static_cast<uint64_t>(sampleRate) * padMs * blockAlign / 1000ULL
    );

    // Lock the source sound to get raw PCM data
    void* srcPtr1 = nullptr, *srcPtr2 = nullptr;
    unsigned int srcLen1 = 0, srcLen2 = 0;
    res = srcSound->lock(0, srcLengthBytes, &srcPtr1, &srcPtr2, &srcLen1, &srcLen2);
    if (res != FMOD_OK || !srcPtr1) {
        log::error("Failed to lock source audio for padding");
        srcSound->release();
        return false;
    }

    // Build padded buffer: [silence (zeroes)] [original PCM]
    unsigned int totalDataSize = padBytes + srcLengthBytes;
    std::vector<uint8_t> paddedData(totalDataSize, 0);
    std::memcpy(paddedData.data() + padBytes, srcPtr1, srcLen1);
    if (srcPtr2 && srcLen2 > 0) {
        std::memcpy(paddedData.data() + padBytes + srcLen1, srcPtr2, srcLen2);
    }

    srcSound->unlock(srcPtr1, srcPtr2, srcLen1, srcLen2);
    srcSound->release();

    // Write WAV header + data
    WavHeader header;
    header.numChannels   = static_cast<uint16_t>(numChannels);
    header.sampleRate    = sampleRate;
    header.bitsPerSample = static_cast<uint16_t>(bitsPerSample);
    header.blockAlign    = static_cast<uint16_t>(blockAlign);
    header.byteRate      = sampleRate * blockAlign;
    header.dataSize      = totalDataSize;
    header.fileSize      = sizeof(WavHeader) - 8 + totalDataSize;

    std::ofstream outFile(destPath, std::ios::binary);
    if (!outFile) {
        log::error("Failed to create padded audio file: {}", destPath.string());
        return false;
    }
    outFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
    outFile.write(reinterpret_cast<const char*>(paddedData.data()), totalDataSize);
    outFile.close();

    log::info(
        "Created padded audio: {} ms silence + original ({} ch, {} Hz, {} bps, {} bytes)",
        padMs, numChannels, sampleRate, bitsPerSample, totalDataSize
    );
    return true;
}

// ─── Padded file registry ────────────────────────────────────────────────────
//
// Maps: song key → padded WAV path
//   song key = m_songID (custom song) or -m_audioTrack - 1 (built-in)
//
// Using song key instead of level ID because a level can have multiple
// songs (m_songIDs).  Each song needs its own padded file.

static std::unordered_map<int, std::filesystem::path> s_paddedPathBySongKey;

/// Get the song key for a GJGameLevel's current song.
static int getSongKey(GJGameLevel* level) {
    return (level->m_songID != 0) ? level->m_songID : (-level->m_audioTrack - 1);
}

/// Ensure a padded WAV exists for the given song key and offset.
void ensurePaddedFile(int songKey, int totalOffset) {
    if (totalOffset >= 0) return;

    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");
    if (!fixEnabled) return;

    int absTotal   = std::abs(totalOffset);
    int intervalMs = ((absTotal + 999) / 1000) * 1000;

    auto saveDir = Mod::get()->getSaveDir();
    std::error_code ec;
    std::filesystem::create_directories(saveDir, ec);

    auto paddedPath = saveDir / fmt::format("padded_{}_{}.wav", songKey, intervalMs);

    if (std::filesystem::exists(paddedPath, ec)) {
        s_paddedPathBySongKey[songKey] = paddedPath;
        return;
    }

    // File will be created lazily in getAudioFileName when we have
    // access to the original source path.
    s_paddedPathBySongKey[songKey] = paddedPath;
}

// ─── Hook: GJGameLevel::getAudioFileName ─────────────────────────────────────
// Return the padded WAV path if one has been registered for this level.

class $modify(NegativeOffsetGJGameLevel, GJGameLevel) {
    gd::string getAudioFileName() {
        int songKey = getSongKey(this);

        // Check if we have a padded file registered for this song
        auto it = s_paddedPathBySongKey.find(songKey);
        if (it != s_paddedPathBySongKey.end()) {
            std::error_code ec;
            if (std::filesystem::exists(it->second, ec)) {
                log::debug("Using padded audio for song key {}", songKey);
                return gd::string(it->second.string());
            }
        }

        // Check if this level has a negative offset that needs the workaround
        float userOffset = OffsetStorage::getOffsetForLevel(m_levelID);
        int totalOffset = FMODAudioEngine::sharedEngine()->m_musicOffset
                          + static_cast<int>(userOffset);

        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");
        if (totalOffset >= 0 || !fixEnabled) {
            return GJGameLevel::getAudioFileName();
        }

        int absTotal   = std::abs(totalOffset);
        int intervalMs = ((absTotal + 999) / 1000) * 1000;

        // Locate the original audio file
        auto original = GJGameLevel::getAudioFileName();
        if (original.empty()) return original;

        auto* fileUtils = CCFileUtils::sharedFileUtils();
        std::string fullPath = fileUtils->fullPathForFilename(original.c_str(), false);
        if (fullPath.empty()) return original;

        std::filesystem::path actualSourcePath(fullPath);
        if (!std::filesystem::exists(actualSourcePath)) return original;

        // Create the padded WAV file
        auto saveDir = Mod::get()->getSaveDir();
        std::error_code ec;
        std::filesystem::create_directories(saveDir, ec);

        auto paddedPath = saveDir / fmt::format("padded_{}_{}.wav", songKey, intervalMs);

        if (!std::filesystem::exists(paddedPath, ec)) {
            if (!createPaddedWavFile(actualSourcePath, paddedPath, intervalMs)) {
                return original;
            }
        }

        // Register and return
        s_paddedPathBySongKey[songKey] = paddedPath;

        log::info(
            "Redirecting song key {} to padded file: {}",
            songKey, paddedPath.string()
        );

        return gd::string(paddedPath.string());
    }
};

// ─── PlayLayer hooks ─────────────────────────────────────────────────────────

void NegativeOffsetPlayLayer::onQuit() {
    // Remove from registry (all entries for this level's songs)
    if (m_level) {
        int mainSongKey = getSongKey(m_level);
        s_paddedPathBySongKey.erase(mainSongKey);
        // Also clear any other song keys that might have been registered
        // (multi-song levels store additional IDs in m_songIDs)
    }

    PlayLayer::onQuit();
}
