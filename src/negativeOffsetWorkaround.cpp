#include "negativeOffsetWorkaround.hpp"
#include "OffsetStorage.hpp"

#include <Geode/modify/GJGameLevel.hpp>

#include <fmod.hpp>
#include <cmath>

using namespace geode::prelude;

// Conditional debug logging — enabled via the "debug-logging" setting
#define LOG_DEBUG(...) \
    do { if (Mod::get()->getSettingValue<bool>("debug-logging")) \
        log::info(__VA_ARGS__); } while(0)

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
bool createPaddedWavFile(
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

    // Optionally inject periodic beeps across the ENTIRE audio for debugging.
    // This lets us verify whether the padded file is actually being played
    // (beeps audible) or not (no beeps), even after the silence padding ends.
    bool debugBeep = Mod::get()->getSettingValue<bool>("debug-beep-in-padding");
    if (debugBeep && bitsPerSample == 16) {
        // Beep: 100 ms of 1 kHz sine wave, repeated every 500 ms
        unsigned int beepSamples   = (sampleRate * 100) / 1000;
        unsigned int beepInterval  = (sampleRate * 500) / 1000;
        unsigned int intervalBytes = beepInterval * blockAlign;

        // Precompute a single beep waveform (16-bit)
        std::vector<int16_t> beepMono(beepSamples);
        for (unsigned int i = 0; i < beepSamples; i++) {
            double t = static_cast<double>(i) / sampleRate;
            beepMono[i] = static_cast<int16_t>(std::sin(2.0 * M_PI * 1000.0 * t) * 13000.0);
        }

        // Write beeps across the full audio buffer
        unsigned int totalSamples = totalDataSize / blockAlign;
        for (unsigned int sampleOffset = 0;
             sampleOffset + beepSamples <= totalSamples;
             sampleOffset += beepInterval)
        {
            for (unsigned int s = 0; s < beepSamples; s++) {
                unsigned int bytePos = (sampleOffset + s) * blockAlign;
                if (bytePos + blockAlign > totalDataSize) break;
                for (int ch = 0; ch < numChannels; ch++) {
                    int16_t sample = beepMono[s];
                    std::memcpy(paddedData.data() + bytePos + ch * 2, &sample, 2);
                }
            }
        }
        log::info("Debug beep: injected {}ms beeps every {}ms across full audio ({} total)",
                  100, 500, totalDataSize / blockAlign / sampleRate * 1000);
    }

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

std::unordered_map<int, std::filesystem::path> s_paddedPathBySongKey;

/// Get the song key for a GJGameLevel's current song.
int getSongKey(GJGameLevel* level) {
    return (level->m_songID != 0) ? level->m_songID : (-level->m_audioTrack - 1);
}

// @geode-ignore(unknown-resource)
/// Try to extract a numeric song ID from a path like "123456.mp3" or
/// "/full/path/123456.ogg". Returns -1 if no numeric ID found.
int extractSongIdFromPath(std::string_view path) {
    // Get the stem (filename without extension)
    auto pos = path.rfind('/');
    if (pos == std::string_view::npos) pos = path.rfind('\\');
    auto filename = (pos == std::string_view::npos) ? path : path.substr(pos + 1);

    // Remove extension
    auto dot = filename.rfind('.');
    auto stem = (dot == std::string_view::npos) ? filename : filename.substr(0, dot);

    // Try to parse as integer
    int id = 0;
    auto result = std::from_chars(stem.data(), stem.data() + stem.size(), id);
    if (result.ec == std::errc() && result.ptr == stem.data() + stem.size()) {
        return id;
    }
    return -1;
}

/// Resolve the cache directory from settings or fall back to the mod save dir.
/// Under Wine, a Linux-style path like /tmp/lso_cache may not be recognized
/// by std::filesystem. We detect this and prepend Z:\ (Wine's default Z: drive
/// mapping).
std::filesystem::path getCacheDir() {
    auto customPath = Mod::get()->getSettingValue<std::string>("padded-cache-path");
    if (!customPath.empty()) {
        // If the path starts with /, it might be a Linux path under Wine.
        // Map it to Z:\ (Wine's default Z: drive mapping).
        if (customPath[0] == '/') {
            std::string winePath = "Z:";
            for (char c : customPath) {
                if (c == '/') winePath += '\\';
                else winePath += c;
            }
            std::filesystem::path wp(winePath);
            std::error_code wec;
            std::filesystem::create_directories(wp, wec);
            if (!wec) {
                log::debug("getCacheDir: mapped Linux path '{}' to Wine path '{}'",
                           customPath, winePath);
                return wp;
            }
        }

        // Try the path as-is
        std::filesystem::path p(customPath);
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            return p;
        }

        // Try to create the directory
        std::filesystem::create_directories(p, ec);
        if (!ec) return p;
        log::warn("Custom cache path invalid, falling back to save dir: {}", ec.message());
    }
    return Mod::get()->getSaveDir();
}

/// Enforce the max cache size: delete oldest padded files when exceeded.
/// Uses the global cache registry (s_cacheRegistry) instead of
/// directory_iterator to avoid filesystem compatibility issues (e.g. wine).
/// Files currently registered in s_paddedPathBySongKey are excluded from deletion.
void enforceCacheSizeLimit() {
    int maxSizeMB = Mod::get()->getSettingValue<int>("padded-cache-max-size");
    if (maxSizeMB <= 0) {
        LOG_DEBUG("Cache limit disabled (maxSizeMB={})", maxSizeMB);
        return;
    }

    auto cacheDir = getCacheDir();
    LOG_DEBUG("Scanning cache directory: {}", cacheDir.string());

    // Build a set of in-use paths so we never delete the current level's cache
    std::unordered_set<std::filesystem::path> inUse;
    for (auto& [_, p] : s_paddedPathBySongKey) {
        inUse.insert(p.lexically_normal());
    }

    // Collect removable files via directory scan
    struct FileEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type time;
        uintmax_t size;
    };
    std::vector<FileEntry> removable;
    uintmax_t totalSize = 0;
    int totalFiles = 0;

    std::error_code dirEc;
    if (std::filesystem::exists(cacheDir, dirEc)) {
        LOG_DEBUG("Cache dir exists, starting directory scan");
        for (auto& entry : std::filesystem::directory_iterator(cacheDir, dirEc)) {
            if (dirEc) {
                LOG_DEBUG("Directory iterator error: {}", dirEc.message());
                break;
            }
            if (!entry.is_regular_file(dirEc)) continue;
            if (dirEc) break;

            auto& p = entry.path();
            auto name = p.filename().string();
            if (name.find("padded_") != 0 || p.extension() != ".wav") continue;

            totalFiles++;

            // Skip in-use
            if (inUse.count(p.lexically_normal())) continue;

            auto ft = entry.last_write_time(dirEc);
            if (dirEc) { dirEc.clear(); continue; }
            auto fs = entry.file_size(dirEc);
            if (dirEc) { dirEc.clear(); continue; }

            removable.push_back({entry.path(), ft, fs});
            totalSize += fs;
        }
    }

    uintmax_t maxSizeBytes = static_cast<uintmax_t>(maxSizeMB) * 1024ULL * 1024ULL;
    LOG_DEBUG("Cache cleanup: {} removable files, {:.1f} MB / {} MB ({} in-use, {} total on disk)",
              removable.size(),
              static_cast<double>(totalSize) / (1024.0 * 1024.0),
              maxSizeMB, inUse.size(), totalFiles);

    if (totalSize <= maxSizeBytes) {
        LOG_DEBUG("Cache size OK, no cleanup needed");
        return;
    }

    // Sort oldest-first
    std::sort(removable.begin(), removable.end(),
        [](const FileEntry& a, const FileEntry& b) { return a.time < b.time; });

    // Delete oldest files until under limit
    uintmax_t target = totalSize - maxSizeBytes;
    uintmax_t freed = 0;
    int deleted = 0;
    for (auto& entry : removable) {
        std::error_code rmEc;
        std::filesystem::remove(entry.path, rmEc);
        if (!rmEc) {
            freed += entry.size;
            deleted++;
            LOG_DEBUG("  Deleted {} ({:.1f} MB)", entry.path.filename().string(),
                      static_cast<double>(entry.size) / (1024.0 * 1024.0));
        } else {
            log::warn("Failed to delete {}: {}", entry.path.string(), rmEc.message());
        }
        if (freed >= target) break;
    }

    LOG_DEBUG("Cache cleanup done: {:.1f} MB over limit, freed {:.1f} MB, deleted {} file(s)",
              static_cast<double>(target) / (1024.0 * 1024.0),
              static_cast<double>(freed) / (1024.0 * 1024.0),
              deleted);
}

/// Compute the padded WAV path for a given song key and total offset (ms).
std::filesystem::path getPaddedPath(int songKey, int totalOffset) {
    int absTotal = std::abs(totalOffset);
    int intervalMs = ((absTotal + 999) / 1000) * 1000;
    return getCacheDir() / fmt::format("padded_{}_{}.wav", songKey, intervalMs);
}

/// Ensure a padded WAV exists for the given song key and offset.
/// Creates the padded file eagerly if it doesn't exist yet, using
/// MusicDownloadManager to locate the original source file.
/// This avoids runtime lag when Song Triggers fire during gameplay.
void ensurePaddedFile(int songKey, int totalOffset) {
    if (totalOffset >= 0) return;

    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");
    if (!fixEnabled) return;

    auto paddedPath = getPaddedPath(songKey, totalOffset);

    std::error_code ec;
    if (std::filesystem::exists(paddedPath, ec)) {
        s_paddedPathBySongKey[songKey] = paddedPath;
        return;
    }

    // File doesn't exist — create it now using MusicDownloadManager to
    // locate the original source file.
    auto* mdm = MusicDownloadManager::sharedState();
    if (!mdm) return;

    auto originalPath = mdm->pathForSong(songKey);
    if (originalPath.empty()) return;

    std::filesystem::path actualSourcePath(originalPath);
    if (!std::filesystem::exists(actualSourcePath)) return;

    int intervalMs = ((std::abs(totalOffset) + 999) / 1000) * 1000;
    if (createPaddedWavFile(actualSourcePath, paddedPath, intervalMs)) {
        s_paddedPathBySongKey[songKey] = paddedPath;
        log::info("Eagerly created padded file for song key {}: {}", songKey, paddedPath.string());
    }
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
        // s_currentTotalOffset is set by MyPlayLayer::prepareMusic before
        // PlayLayer::prepareMusic calls getAudioFileName internally.
        extern float s_currentTotalOffset;
        float totalOffset = s_currentTotalOffset;
        bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");
        if (totalOffset >= 0 || !fixEnabled) {
            return GJGameLevel::getAudioFileName();
        }

        int absTotal   = static_cast<int>(std::abs(totalOffset));

        // Locate the original audio file
        auto original = GJGameLevel::getAudioFileName();
        if (original.empty()) return original;

        auto* fileUtils = CCFileUtils::sharedFileUtils();
        std::string fullPath = fileUtils->fullPathForFilename(original.c_str(), false);
        if (fullPath.empty()) return original;

        std::filesystem::path actualSourcePath(fullPath);
        if (!std::filesystem::exists(actualSourcePath)) return original;

        // Create the padded WAV file in the configured cache directory
        auto paddedPath = getPaddedPath(songKey, static_cast<int>(totalOffset));
        int intervalMs = ((absTotal + 999) / 1000) * 1000;
        std::error_code ec;

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
        if (!m_level->m_songIDs.empty()) {
            auto ids = m_level->m_songIDs;
            size_t pos = 0;
            while ((pos = ids.find(',')) != gd::string::npos) {
                auto idStr = ids.substr(0, pos);
                ids.erase(0, pos + 1);
                try { s_paddedPathBySongKey.erase(geode::utils::numFromString<int>(idStr).unwrapOr(0)); } catch (...) {}
            }
            if (!ids.empty()) {
                try { s_paddedPathBySongKey.erase(geode::utils::numFromString<int>(ids).unwrapOr(0)); } catch (...) {}
            }
        }
    }

    PlayLayer::onQuit();
}
