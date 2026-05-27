#include "AsyncPregenerator.hpp"
#include "CacheStorage.hpp"
#include "negativeOffsetWorkaround.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>

using namespace geode::prelude;

// ─── Singleton ──────────────────────────────────────────────────────────────

AsyncPregenerator& AsyncPregenerator::get() {
    static AsyncPregenerator instance;
    return instance;
}

AsyncPregenerator::~AsyncPregenerator() {
    cancel();
    waitAll();
}

// ─── Public API ─────────────────────────────────────────────────────────────

void AsyncPregenerator::generate(std::vector<PregenerateTask> tasks, PregenerateCallback callback) {
    // If already running, wait for previous run to finish first
    waitAll();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cancelled = false;
        m_running = true;
        m_completedCount = 0;
        m_failedCount = 0;
        m_totalCount = static_cast<int>(tasks.size());

        m_progress.clear();
        m_progress.reserve(tasks.size());
        for (auto& t : tasks) {
            std::string label = fmt::format("{}", t.songKey);
            m_progress.push_back({t.songKey, PregenerateStatus::Pending, label, 0.0f, "Waiting..."});
        }
    }

    // Launch coordinator in a separate thread
    m_coordinatorThread = std::thread(&AsyncPregenerator::runCoordinator, this,
                                       std::move(tasks), std::move(callback));
}

void AsyncPregenerator::waitAll() {
    if (m_coordinatorThread.joinable()) {
        m_coordinatorThread.join();
    }
    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();
}

void AsyncPregenerator::cancel() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelled = true;
}

bool AsyncPregenerator::isRunning() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_running;
}

bool AsyncPregenerator::isSongInProgress(int songKey) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inProgressKeys.count(songKey) > 0;
}

PregenerateProgress AsyncPregenerator::getProgress() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    PregenerateProgress p;
    p.songs = m_progress;
    p.completedCount = m_completedCount;
    p.failedCount = m_failedCount;
    p.totalCount = m_totalCount;
    return p;
}

// ─── Internal: Coordinator ──────────────────────────────────────────────────
//
// The coordinator:
//   1. For each task, spawns a worker thread.
//   2. Periodically calls the progress callback while workers are running.
//   3. Joins all worker threads when done.
//   4. Calls the callback one final time, then marks the run as finished.

void AsyncPregenerator::runCoordinator(std::vector<PregenerateTask> tasks,
                                        PregenerateCallback callback) {
    auto notify = [&]() {
        if (callback) {
            auto progress = getProgress();
            callback(progress);
        }
    };

    // Initial notification: all "Pending"
    notify();

    // Spawn one thread per task
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < tasks.size(); i++) {
            if (m_cancelled) break;
            m_progress[i].status = PregenerateStatus::Generating;
            m_progress[i].message = "Starting...";
        }
    }
    notify();

    // Launch threads
    for (size_t i = 0; i < tasks.size(); i++) {
        if (m_cancelled) break;
        m_threads.emplace_back(&AsyncPregenerator::processTask, this, tasks[i]);
    }

    // Poll for completion while workers are running
    const auto pollInterval = std::chrono::milliseconds(100);
    bool allDone = false;
    while (!allDone && !m_cancelled) {
        std::this_thread::sleep_for(pollInterval);
        notify();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            allDone = (m_completedCount + m_failedCount >= m_totalCount);
        }
    }

    // Join all worker threads
    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
    m_threads.clear();

    // Final notification
    notify();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
    }
}

// ─── Internal: Process one task ─────────────────────────────────────────────
//
// This runs on a worker thread. It creates the padded file and updates
// progress.

void AsyncPregenerator::processTask(const PregenerateTask& task) {
    // Find our index in the progress array
    int idx = -1;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int i = 0; i < static_cast<int>(m_progress.size()); i++) {
            if (m_progress[i].songKey == task.songKey) {
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) return;

    auto paddedPath = getPaddedPath(task.songKey, task.totalOffset);
    std::error_code ec;

    // Check if padded file already exists (and wasn't created by us this run)
    if (std::filesystem::exists(paddedPath, ec)) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_progress[idx].status = PregenerateStatus::Completed;
            m_progress[idx].progress = 1.0f;
            m_progress[idx].message = "Already cached";
            m_completedCount++;
        }
        // Register in the global registry
        s_paddedPathBySongKey[task.songKey] = paddedPath;
        return;
    }

    // Check if source exists
    if (!std::filesystem::exists(task.sourcePath, ec)) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_progress[idx].status = PregenerateStatus::Failed;
            m_progress[idx].message = "Source file not found";
            m_failedCount++;
        }
        return;
    }

    // Mark this song key as in-progress so callers know the file may be incomplete
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inProgressKeys.insert(task.songKey);
    }

    int intervalMs = ((std::abs(task.totalOffset) + 999) / 1000) * 1000;
    bool created = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_progress[idx].message = "Decoding & writing WAV...";
        m_progress[idx].progress = 0.3f;
    }

    created = createPaddedWavFile(task.sourcePath, paddedPath, intervalMs);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inProgressKeys.erase(task.songKey);

        if (created) {
            m_progress[idx].status = PregenerateStatus::Completed;
            m_progress[idx].progress = 1.0f;
            m_progress[idx].message = "Done";
            m_completedCount++;
        } else {
            m_progress[idx].status = PregenerateStatus::Failed;
            m_progress[idx].progress = 0.0f;
            m_progress[idx].message = "Encoding failed";
            m_failedCount++;
        }
    }

    if (created) {
        s_paddedPathBySongKey[task.songKey] = paddedPath;
        LOG_DEBUG("AsyncPregenerator: created padded file for song key {}: {}",
                  task.songKey, paddedPath.string());
    } else {
        log::warn("AsyncPregenerator: failed to create padded file for song key {}",
                  task.songKey);
    }
}

// ─── Convenience: collect tasks from a level ────────────────────────────────

std::vector<PregenerateTask> collectPregenerateTasks(GJGameLevel* level, int totalOffset) {
    std::vector<PregenerateTask> tasks;
    if (!level || totalOffset >= 0) return tasks;

    bool fixEnabled = Mod::get()->getSettingValue<bool>("negative-offset-fix");
    if (!fixEnabled) return tasks;

    auto* mdm = MusicDownloadManager::sharedState();
    if (!mdm) return tasks;

    // Collect all song keys for this level
    auto collectKeys = [&](auto&& cb) {
        int mainKey = (level->m_songID != 0) ? level->m_songID
                                             : (-level->m_audioTrack - 1);
        cb(mainKey);
        if (!level->m_songIDs.empty()) {
            auto ids = level->m_songIDs;
            size_t pos = 0;
            while ((pos = ids.find(',')) != gd::string::npos) {
                auto idStr = ids.substr(0, pos);
                ids.erase(0, pos + 1);
                try { cb(geode::utils::numFromString<int>(idStr).unwrapOr(0)); } catch (...) {}
            }
            if (!ids.empty()) {
                try { cb(geode::utils::numFromString<int>(ids).unwrapOr(0)); } catch (...) {}
            }
        }
    };

    collectKeys([&](int songKey) {
        if (songKey <= 0) return;

        // Skip if already cached
        auto paddedPath = getPaddedPath(songKey, totalOffset);
        std::error_code ec;
        if (std::filesystem::exists(paddedPath, ec)) {
            s_paddedPathBySongKey[songKey] = paddedPath;
            return;
        }

        // Locate source file
        auto originalPath = mdm->pathForSong(songKey);
        if (originalPath.empty()) return;

        std::filesystem::path sourcePath(originalPath);
        if (originalPath.empty() || !std::filesystem::exists(sourcePath, ec)) return;

        tasks.push_back({songKey, totalOffset, std::move(sourcePath)});
    });

    return tasks;
}
