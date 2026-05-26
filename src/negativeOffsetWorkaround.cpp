#include "negativeOffsetWorkaround.hpp"
#include "OffsetStorage.hpp"

#include <Geode/modify/GJGameLevel.hpp>

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

// ─── Audio filename redirection ──────────────────────────────────────────────
//
// Before calling the original prepareMusic, we set this global so that
// GJGameLevel::getAudioFileName() returns the padded WAV path instead of
// the original filename.  GD is single-threaded, so a single global is safe.

static std::optional<std::filesystem::path> s_audioFileNameRedirect;

class $modify(NegativeOffsetGJGameLevel, GJGameLevel) {
    gd::string getAudioFileName() {
        if (s_audioFileNameRedirect.has_value()) {
            auto result = gd::string(s_audioFileNameRedirect->string());
            log::debug("Redirected getAudioFileName to: {}", result);
            s_audioFileNameRedirect.reset();
            return result;
        }
        return GJGameLevel::getAudioFileName();
    }
};

// ─── PlayLayer hooks ─────────────────────────────────────────────────────────

void NegativeOffsetPlayLayer::prepareMusic(bool dontWait) {
    auto* audio = FMODAudioEngine::sharedEngine();
    float userOffset = m_level ? OffsetStorage::getOffsetForLevel(m_level->m_levelID) : 0.f;

    int originalOffset = audio->m_musicOffset;
    int totalOffset    = originalOffset + static_cast<int>(userOffset);

    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");

    if (totalOffset >= 0 || !fixEnabled || !m_level) {
        // Nothing to do — pass through
        PlayLayer::prepareMusic(dontWait);
        return;
    }

    // ── Negative offset: redirect to a padded WAV file ──

    // Clean up any leftover padded file from a previous run (e.g. retry)
    if (!m_fields->m_paddedAudioPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_fields->m_paddedAudioPath, ec);
        m_fields->m_paddedAudioPath.clear();
    }

    // Get the original audio filename (before we set the redirect)
    gd::string audioFileName = m_level->getAudioFileName();
    if (audioFileName.empty()) {
        log::warn("Could not get audio file name, falling back");
        PlayLayer::prepareMusic(dontWait);
        return;
    }

    // Resolve to an absolute path via CCFileUtils
    auto* fileUtils = CCFileUtils::sharedFileUtils();
    std::string fullPath = fileUtils->fullPathForFilename(audioFileName.c_str(), false);

    std::filesystem::path actualSourcePath;
    if (!fullPath.empty()) {
        actualSourcePath = std::filesystem::path(fullPath);
    }
    if (actualSourcePath.empty() || !std::filesystem::exists(actualSourcePath)) {
        log::warn("Could not locate audio file '{}', falling back", audioFileName);
        PlayLayer::prepareMusic(dontWait);
        return;
    }

    // Create the padded WAV file in the mod's runtime directory
    auto runtimeDir = dirs::getModRuntimeDir();
    std::error_code ec;
    std::filesystem::create_directories(runtimeDir, ec);

    int absPadMs = std::abs(totalOffset);
    auto paddedPath = runtimeDir / fmt::format("padded_{}_{}.wav", m_level->m_levelID, absPadMs);

    if (!std::filesystem::exists(paddedPath, ec)) {
        if (!createPaddedWavFile(actualSourcePath, paddedPath, absPadMs)) {
            log::warn("Failed to create padded audio file, falling back");
            PlayLayer::prepareMusic(dontWait);
            return;
        }
    }

    m_fields->m_paddedAudioPath = paddedPath;

    // Set the redirect so GJGameLevel::getAudioFileName() returns our file
    s_audioFileNameRedirect = paddedPath;

    // Silence is baked into the file — no FMOD offset needed
    audio->m_musicOffset = 0;

    // Let the game load the (now redirected) audio file
    PlayLayer::prepareMusic(dontWait);

    // Safety: clear if getAudioFileName wasn't called
    s_audioFileNameRedirect.reset();
}

void NegativeOffsetPlayLayer::startMusic() {
    if (!m_fields->m_paddedAudioPath.empty()) {
        // Silence is already in the file — play at position 0
        FMODAudioEngine::sharedEngine()->m_musicOffset = 0;
    }
    PlayLayer::startMusic();
}

void NegativeOffsetPlayLayer::onQuit() {
    // Delete the temporary padded file
    if (!m_fields->m_paddedAudioPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_fields->m_paddedAudioPath, ec);
        m_fields->m_paddedAudioPath.clear();
    }
    PlayLayer::onQuit();
}
