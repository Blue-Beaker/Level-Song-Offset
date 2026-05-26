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
// Maps: level ID → padded WAV path
// Populated in GJGameLevel::getAudioFileName hook (called during prepareMusic)
// Used in FMODAudioEngine::queueStartMusic hook (called every time music plays)
//
// We key by level ID because getAudioFileName() returns the padded WAV path
// directly (like jukebox does), and queueStartMusic receives that same path.
// When the level is re-entered or retried, getAudioFileName is called again
// and the same padded path is returned, so queueStartMusic always gets it.

static std::unordered_map<int, std::filesystem::path> s_paddedPathByLevelId;

// ─── Hook: GJGameLevel::getAudioFileName ─────────────────────────────────────
// When the game asks for the level's audio filename, if this level has a
// negative offset, we create a padded WAV and return ITS path directly.
// This follows the same pattern as jukebox — the filename returned here is
// what ends up being passed to queueStartMusic.

class $modify(NegativeOffsetGJGameLevel, GJGameLevel) {
    gd::string getAudioFileName() {
        // Check if this level has a negative offset that needs the workaround
        float userOffset = OffsetStorage::getOffsetForLevel(m_levelID);
        int totalOffset = FMODAudioEngine::sharedEngine()->m_musicOffset
                          + static_cast<int>(userOffset);

        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");
        if (totalOffset >= 0 || !fixEnabled) {
            return GJGameLevel::getAudioFileName();
        }

        // Check if we already have a padded file for this level
        {
            auto it = s_paddedPathByLevelId.find(m_levelID);
            if (it != s_paddedPathByLevelId.end()) {
                // File exists on disk? If so, reuse it
                std::error_code ec;
                if (std::filesystem::exists(it->second, ec)) {
                    log::debug("Reusing padded audio for level {}", m_levelID);
                    // Ensure m_musicOffset = 0
                    FMODAudioEngine::sharedEngine()->m_musicOffset = 0;
                    return gd::string(it->second.string());
                }
                // File was deleted (e.g. onQuit cleanup), remove stale entry
                s_paddedPathByLevelId.erase(it);
            }
        }

        // First time: get the original filename to locate the source file
        auto original = GJGameLevel::getAudioFileName();
        if (original.empty()) return original;

        // Resolve the original file path
        auto* fileUtils = CCFileUtils::sharedFileUtils();
        std::string fullPath = fileUtils->fullPathForFilename(original.c_str(), false);
        if (fullPath.empty()) return original;

        std::filesystem::path actualSourcePath(fullPath);
        if (!std::filesystem::exists(actualSourcePath)) return original;

        // Create the padded WAV file in the mod's save directory.
        // Using getSaveDir() (persistent across sessions) instead of
        // getModRuntimeDir() (ephemeral, changes every launch) so the
        // padded file is cached and reused on subsequent plays.
        auto saveDir = Mod::get()->getSaveDir();
        std::error_code ec;
        std::filesystem::create_directories(saveDir, ec);

        int absPadMs = std::abs(totalOffset);
        auto paddedPath = saveDir / fmt::format("padded_{}_{}.wav", m_levelID, absPadMs);

        if (!std::filesystem::exists(paddedPath, ec)) {
            if (!createPaddedWavFile(actualSourcePath, paddedPath, absPadMs)) {
                return original;
            }
        }

        // Register the mapping
        s_paddedPathByLevelId[m_levelID] = paddedPath;

        // Ensure m_musicOffset = 0 — silence is baked into the file
        FMODAudioEngine::sharedEngine()->m_musicOffset = 0;

        log::info(
            "Redirecting audio for level {} to padded file: {}",
            m_levelID, paddedPath.string()
        );

        // Return the padded WAV path directly (like jukebox does)
        return gd::string(paddedPath.string());
    }
};

// ─── Hook: FMODAudioEngine::queueStartMusic ──────────────────────────────────
// No longer needed for redirect — getAudioFileName already returns the padded
// path.  But we still need to ensure m_musicOffset = 0.

class $modify(NegativeOffsetFMOD, FMODAudioEngine) {
    void queueStartMusic(
        gd::string audioFilename, float p1, float p2, float p3,
        bool p4, int ms, int p6, int p7, int p8, int p9,
        bool p10, int p11, bool p12, bool p13
    ) {
        // Check if this audioFilename is one of our padded WAV files
        // by looking for ".wav" and checking the registry
        if (audioFilename.size() > 4 &&
            audioFilename.rfind(".wav") == audioFilename.size() - 4) {
            // This might be our padded file — ensure m_musicOffset = 0
            this->m_musicOffset = 0;
        }

        FMODAudioEngine::queueStartMusic(
            audioFilename, p1, p2, p3, p4,
            ms, p6, p7, p8, p9, p10, p11, p12, p13
        );
    }
};

// ─── PlayLayer hooks ─────────────────────────────────────────────────────────

void NegativeOffsetPlayLayer::onQuit() {
    // Remove from registry
    if (m_level) {
        s_paddedPathByLevelId.erase(m_level->m_levelID);
    }

    PlayLayer::onQuit();
}
