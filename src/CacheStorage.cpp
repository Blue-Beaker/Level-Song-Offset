#include "CacheStorage.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cstring>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>

using namespace geode::prelude;

// ─── Padded file helpers ─────────────────────────────────────────────────────

/// Check if a path points to an original GD song file.
/// Original songs are stored as "<songID>.mp3" or "<songID>.ogg" in the
/// GD songs directory. Nong songs (e.g. from jukebox) are stored elsewhere
/// with arbitrary filenames.
static bool isOriginalSongPath(const std::filesystem::path& sourcePath) {
    auto filename = sourcePath.filename().string();
    // Check for "digits.extension" pattern
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return false;
    auto stem = filename.substr(0, dot);
    auto ext = filename.substr(dot);
    if (ext != ".mp3" && ext != ".ogg") return false;
    // Check if stem is all digits
    return !stem.empty() && stem.find_first_not_of("0123456789") == std::string::npos;
}

size_t hashSourcePath(const std::filesystem::path& sourcePath) {
    // For original GD songs (numeric filename in songs folder), return 0
    // so they use the old songKey-only naming scheme. This keeps cache
    // files compatible and avoids unnecessary path hashing.
    if (isOriginalSongPath(sourcePath)) {
        return 0;
    }
    // For nong songs (jukebox etc.), hash the full path to distinguish
    // different audio files that share the same GD song ID.
    auto p = sourcePath.lexically_normal().string();
    LOG_DEBUG("hashSourcePath: nong song path '{}' -> hash {:x}", p, std::hash<std::string>{}(p));
    return std::hash<std::string>{}(p);
}

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

        std::filesystem::path p(customPath);
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) {
            return p;
        }

        std::filesystem::create_directories(p, ec);
        if (!ec) return p;
        log::warn("Custom cache path invalid, falling back to save dir: {}", ec.message());
    }
    return Mod::get()->getSaveDir();
}

std::filesystem::path getPaddedPath(int songKey, int totalOffset, const std::filesystem::path& sourcePath) {
    int absTotal = std::abs(totalOffset);
    int intervalMs = ((absTotal + 999) / 1000) * 1000;
    auto pathHash = hashSourcePath(sourcePath);
    if (pathHash == 0) {
        // Original GD song — use songKey-only naming for backward compat
        return getCacheDir() / fmt::format("padded_{}_{}.wav", songKey, intervalMs);
    }
    return getCacheDir() / fmt::format("padded_{}_{:x}_{}.wav", songKey, pathHash, intervalMs);
}

/// Internal: songKey-only fallback path.
static std::filesystem::path getPaddedPathFallback(int songKey, int totalOffset) {
    int absTotal = std::abs(totalOffset);
    int intervalMs = ((absTotal + 999) / 1000) * 1000;
    return getCacheDir() / fmt::format("padded_{}_{}.wav", songKey, intervalMs);
}

std::filesystem::path getPaddedPath(int songKey, int totalOffset) {
    auto* mdm = MusicDownloadManager::sharedState();
    if (mdm) {
        auto originalPath = mdm->pathForSong(songKey);
        if (!originalPath.empty()) {
            std::filesystem::path srcPath(originalPath);
            if (std::filesystem::exists(srcPath)) {
                return getPaddedPath(songKey, totalOffset, srcPath);
            }
        }
    }
    return getPaddedPathFallback(songKey, totalOffset);
}

// ─── Cache collection helpers ──────────────────────────────────────────────────

CacheCollection collectRemovableCacheFiles(const std::unordered_set<std::filesystem::path>& excludedFiles) {

    auto cacheDir = getCacheDir();
    LOG_DEBUG("collectRemovableCacheFiles: scanning cache directory: {}", cacheDir.string());

    // Normalize excluded paths for comparison
    std::unordered_set<std::filesystem::path> excludedNorm;
    for (auto& p : excludedFiles) {
        excludedNorm.insert(p.lexically_normal());
    }

    CacheCollection result;
    std::error_code dirEc;

    if (std::filesystem::exists(cacheDir, dirEc)) {
        LOG_DEBUG("collectRemovableCacheFiles: cache dir exists, starting directory scan");
        for (auto& entry : std::filesystem::directory_iterator(cacheDir, dirEc)) {
            if (dirEc) {
                LOG_DEBUG("collectRemovableCacheFiles: directory iterator error: {}", dirEc.message());
                break;
            }
            if (!entry.is_regular_file(dirEc)) continue;
            if (dirEc) break;

            auto& p = entry.path();
            auto name = p.filename().string();
            if (name.find("padded_") != 0 || p.extension() != ".wav") continue;

            result.totalFiles++;

            // Skip excluded files
            if (excludedNorm.count(p.lexically_normal())) {
                result.excludedCount++;
                continue;
            }

            auto ft = entry.last_write_time(dirEc);
            if (dirEc) { dirEc.clear(); continue; }
            auto fs = entry.file_size(dirEc);
            if (dirEc) { dirEc.clear(); continue; }

            result.removable.push_back({entry.path(), ft, fs});
            result.totalSize += fs;
        }
    }

    LOG_DEBUG("collectRemovableCacheFiles: found {} removable files ({:.1f} MB, {} excluded, {} total on disk)",
              result.removable.size(),
              static_cast<double>(result.totalSize) / (1024.0 * 1024.0),
              result.excludedCount, result.totalFiles);

    return result;
}

/// Delete files from \c collection (sorted oldest-first) until \p target bytes are freed.
/// Returns the number of bytes actually freed.
static uintmax_t deleteOldestFiles(std::vector<FileEntry>& files, uintmax_t target) {
    // Sort oldest-first
    std::sort(files.begin(), files.end(),
        [](const FileEntry& a, const FileEntry& b) { return a.time < b.time; });

    uintmax_t freed = 0;
    int deleted = 0;
    for (auto& entry : files) {
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

    LOG_DEBUG("deleteOldestFiles: freed {:.1f} MB, deleted {} file(s)",
              static_cast<double>(freed) / (1024.0 * 1024.0), deleted);
    return freed;
}

void reduceCacheToSize(int maxSizeMB, std::unordered_set<std::filesystem::path> excludedFiles) {

    auto collection = collectRemovableCacheFiles(excludedFiles);

    uintmax_t maxSizeBytes = static_cast<uintmax_t>(maxSizeMB) * 1024ULL * 1024ULL;
    LOG_DEBUG("reduceCacheToSize: {:.1f} MB / {} MB limit",
              static_cast<double>(collection.totalSize) / (1024.0 * 1024.0),
              maxSizeMB);

    if (collection.totalSize <= maxSizeBytes) {
        LOG_DEBUG("reduceCacheToSize: cache size OK, no cleanup needed");
        return;
    }

    uintmax_t target = collection.totalSize - maxSizeBytes;
    uintmax_t freed = deleteOldestFiles(collection.removable, target);

    LOG_DEBUG("reduceCacheToSize: done, {:.1f} MB over limit, freed {:.1f} MB",
              static_cast<double>(target) / (1024.0 * 1024.0),
              static_cast<double>(freed) / (1024.0 * 1024.0));
}

void enforceCacheSizeLimit(const std::vector<int>& songKeys, int totalOffset) {
    int maxSizeMB = Mod::get()->getSettingValue<int>("padded-cache-max-size");
    if (maxSizeMB < 0) {
        LOG_DEBUG("reduceCacheToSize: limit disabled (maxSizeMB={})", maxSizeMB);
        return;
    }

    // Build excluded set: only the padded files for the currently active songs.
    // Compute the expected padded file path directly from the source path,
    // without consulting any registry.
    std::unordered_set<std::filesystem::path> excluded;

    for (int songKey : songKeys) {
        auto paddedPath = getPaddedPath(songKey, totalOffset);
        excluded.insert(paddedPath.lexically_normal());
    }

    reduceCacheToSize(maxSizeMB, std::move(excluded));
}

void promptClearAllCache() {
    auto collection = collectRemovableCacheFiles({});

    if (collection.totalFiles <= 0) {
        FLAlertLayer::create(
            "Clear Audio Cache",
            "No negative offset audio cache found.",
            "OK"
        )->show();
        return;
    }

    createQuickPopup(
        "Clear Audio Cache",
        fmt::format("Delete all {} cached audio files ({:.2f} MB)?\nOriginal song files won't be deleted.",
            collection.totalFiles,
            static_cast<double>(collection.totalSize) / (1024.0 * 1024.0)),
        "Cancel", "Delete",
        [collection](auto*, bool btn2) mutable {
            if (btn2) {
                deleteOldestFiles(collection.removable, collection.totalSize);
                Notification::create(
                    fmt::format("Cleared {} audio cache ({:.2f} MB)",
                        collection.totalFiles,
                        static_cast<double>(collection.totalSize) / (1024.0 * 1024.0)),
                    NotificationIcon::Success
                )->show();
            }
        }
    );
}
