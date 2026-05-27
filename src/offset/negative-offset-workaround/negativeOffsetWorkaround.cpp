#include "negativeOffsetWorkaround.hpp"
#include "../OffsetStorage.hpp"

#include <fmod.hpp>
#include <cmath>

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

/// Inject periodic 1 kHz beeps into the padded audio buffer for debugging.
/// Beeps are 100 ms long, repeated every 500 ms, written across the full audio.
/// Only works for 16-bit PCM audio. No-op if the "debug-beep-in-padding" setting
/// is disabled or bitsPerSample != 16.
static void injectDebugBeeps(
    std::vector<uint8_t>& audioData,
    unsigned int sampleRate,
    unsigned int blockAlign,
    int numChannels,
    int bitsPerSample
) {
    bool debugBeep = Mod::get()->getSettingValue<bool>("debug-beep-in-padding");
    if (!debugBeep || bitsPerSample != 16) return;

    unsigned int beepSamples  = (sampleRate * 100) / 1000;  // 100 ms
    unsigned int beepInterval = (sampleRate * 500) / 1000;  // 500 ms

    // Precompute a single beep waveform (16-bit, 1 kHz sine)
    std::vector<int16_t> beepMono(beepSamples);
    for (unsigned int i = 0; i < beepSamples; i++) {
        double t = static_cast<double>(i) / sampleRate;
        beepMono[i] = static_cast<int16_t>(std::sin(2.0 * M_PI * 1000.0 * t) * 13000.0);
    }

    // Write beeps across the full audio buffer
    unsigned int totalSamples = static_cast<unsigned int>(audioData.size()) / blockAlign;
    for (unsigned int sampleOffset = 0;
         sampleOffset + beepSamples <= totalSamples;
         sampleOffset += beepInterval)
    {
        for (unsigned int s = 0; s < beepSamples; s++) {
            unsigned int bytePos = (sampleOffset + s) * blockAlign;
            if (bytePos + blockAlign > audioData.size()) break;
            for (int ch = 0; ch < numChannels; ch++) {
                int16_t sample = beepMono[s];
                std::memcpy(audioData.data() + bytePos + ch * 2, &sample, 2);
            }
        }
    }

    log::info("Debug beep: injected {}ms beeps every {}ms across full audio ({} total ms)",
              100, 500, totalSamples / sampleRate * 1000);
}

/**
 * Decode the source audio file to PCM, prepend |padMs| ms of silence,
 * and write the result as a standard WAV file.
 *
 * Uses FMOD to handle all decoding (supports MP3, OGG, WAV, etc.).
 *
 * @return true on success
 */
bool createPaddedWavFile(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& destPath,
    int padMs
) {
    auto* audio = FMODAudioEngine::sharedEngine();
    if (!audio || !audio->m_system) return false;

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

    // Optionally inject periodic beeps across the ENTIRE audio for debugging.
    injectDebugBeeps(paddedData, sampleRate, blockAlign, numChannels, bitsPerSample);

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
