#pragma once

#include <unordered_set>
#include <mutex>

/**
 * Tracks which audio tracks are currently using padded audio files.
 *
 * Tracks by both musicID and channelID so that any hook method can query
 * regardless of which identifier it has available:
 *  - queueStartMusic  → sets by musicID and channelID
 *  - startMusic       → queries by musicID
 *  - loadAndPlayMusic → queries by musicID
 *  - triggerQueuedMusic → queries by channelID (via m_channelID)
 *  - setMusicTimeMS   → queries by channelID (via channel param)
 *
 * Thread-safe for concurrent access from audio/pregen threads.
 */
struct PaddedTrackTracker {
    /// Mark a track as using padded audio (by both musicID and channelID).
    void setPadded(int musicID, int channelID) {
        std::lock_guard lock(m_mutex);
        if (musicID > 0)  m_byMusicID.insert(musicID);
        if (channelID > 0) m_byChannelID.insert(channelID);
    }

    /// Mark a track as NOT using padded audio (by both musicID and channelID).
    void setOriginal(int musicID, int channelID) {
        std::lock_guard lock(m_mutex);
        if (musicID > 0)  m_byMusicID.erase(musicID);
        if (channelID > 0) m_byChannelID.erase(channelID);
    }

    /// Check by musicID.
    bool isPaddedByMusicID(int musicID) const {
        std::lock_guard lock(m_mutex);
        return musicID > 0 && m_byMusicID.contains(musicID);
    }

    /// Check by channelID.
    bool isPaddedByChannel(int channelID) const {
        std::lock_guard lock(m_mutex);
        return channelID > 0 && m_byChannelID.contains(channelID);
    }

    /// Clear all tracking (e.g. on level exit).
    void clear() {
        std::lock_guard lock(m_mutex);
        m_byMusicID.clear();
        m_byChannelID.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::unordered_set<int> m_byMusicID;
    std::unordered_set<int> m_byChannelID;
};

/// Global instance.
inline PaddedTrackTracker s_paddedTracks;
