#include "CacheStorage.hpp"

#include <algorithm>
#include <cstring>
#include <system_error>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

// Conditional debug logging — enabled via the "debug-logging" setting
#define LOG_DEBUG(...) \
    do { if (Mod::get()->getSettingValue<bool>("debug-logging")) \
        log::info(__VA_ARGS__); } while(0)

// ─── Padded file registry ────────────────────────────────────────────────────
//
// Maps: song key → padded WAV path
//   song key = m_songID (custom song) or -m_audioTrack - 1 (built-in)
//
// Using song key instead of level ID because a level can have multiple
// songs (m_songIDs).  Each song needs its own padded file.

std::unordered_map<int, std::filesystem::path> s_paddedPathBySongKey;

int getSongKey(GJGameLevel* level) {
    return (level->m_songID != 0) ? level->m_songID : (-level->m_audioTrack - 1);
}

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
                LOG_DEBUG("getCacheDir: mapped Linux path '{}' to Wine path '{}'",
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

std::filesystem::path getPaddedPath(int songKey, int totalOffset) {
    int absTotal = std::abs(totalOffset);
    int intervalMs = ((absTotal + 999) / 1000) * 1000;
    return getCacheDir() / fmt::format("padded_{}_{}.wav", songKey, intervalMs);
}

void reduceCacheToSize(int maxSizeMB, std::unordered_set<std::filesystem::path> excludedFiles) {

    auto cacheDir = getCacheDir();
    LOG_DEBUG("reduceCacheToSize: scanning cache directory: {}", cacheDir.string());

    // Normalize excluded paths for comparison
    std::unordered_set<std::filesystem::path> excludedNorm;
    for (auto& p : excludedFiles) {
        excludedNorm.insert(p.lexically_normal());
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
        LOG_DEBUG("reduceCacheToSize: cache dir exists, starting directory scan");
        for (auto& entry : std::filesystem::directory_iterator(cacheDir, dirEc)) {
            if (dirEc) {
                LOG_DEBUG("reduceCacheToSize: directory iterator error: {}", dirEc.message());
                break;
            }
            if (!entry.is_regular_file(dirEc)) continue;
            if (dirEc) break;

            auto& p = entry.path();
            auto name = p.filename().string();
            if (name.find("padded_") != 0 || p.extension() != ".wav") continue;

            totalFiles++;

            // Skip excluded files
            if (excludedNorm.count(p.lexically_normal())) continue;

            auto ft = entry.last_write_time(dirEc);
            if (dirEc) { dirEc.clear(); continue; }
            auto fs = entry.file_size(dirEc);
            if (dirEc) { dirEc.clear(); continue; }

            removable.push_back({entry.path(), ft, fs});
            totalSize += fs;
        }
    }

    uintmax_t maxSizeBytes = static_cast<uintmax_t>(maxSizeMB) * 1024ULL * 1024ULL;
    LOG_DEBUG("reduceCacheToSize: {} removable files, {:.1f} MB / {} MB ({} excluded, {} total on disk)",
              removable.size(),
              static_cast<double>(totalSize) / (1024.0 * 1024.0),
              maxSizeMB, excludedNorm.size(), totalFiles);

    if (totalSize <= maxSizeBytes) {
        LOG_DEBUG("reduceCacheToSize: cache size OK, no cleanup needed");
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

    LOG_DEBUG("reduceCacheToSize: done, {:.1f} MB over limit, freed {:.1f} MB, deleted {} file(s)",
              static_cast<double>(target) / (1024.0 * 1024.0),
              static_cast<double>(freed) / (1024.0 * 1024.0),
              deleted);
}

void enforceCacheSizeLimit() {
    int maxSizeMB = Mod::get()->getSettingValue<int>("padded-cache-max-size");
    // Build excluded set from the in-use registry
    std::unordered_set<std::filesystem::path> excluded;
    for (auto& [_, p] : s_paddedPathBySongKey) {
        excluded.insert(p.lexically_normal());
    }
    if (maxSizeMB <= 0) {
        LOG_DEBUG("reduceCacheToSize: limit disabled (maxSizeMB={})", maxSizeMB);
        return;
    }else{
        reduceCacheToSize(maxSizeMB, std::move(excluded));
    }
}
