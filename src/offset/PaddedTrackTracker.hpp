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
    /// channelID may be 0 (default channel) — still tracked so that hooks
    /// like setMusicTimeMS(channel=0) can correctly detect padded state.
    void setPadded(int musicID, int channelID) {
        std::lock_guard lock(m_mutex);
        if (musicID > 0)  m_byMusicID.insert(musicID);
        m_byChannelID.insert(channelID);
    }

    /// Mark a track as using padded audio by musicID only.
    /// Used when only the musicID is available (e.g. in getAudioFileName hook).
    void setPaddedByMusicID(int musicID) {
        std::lock_guard lock(m_mutex);
        if (musicID > 0) m_byMusicID.insert(musicID);
    }

    /// Mark a track as NOT using padded audio (by both musicID and channelID).
    void setOriginal(int musicID, int channelID) {
        std::lock_guard lock(m_mutex);
        if (musicID > 0)  m_byMusicID.erase(musicID);
        m_byChannelID.erase(channelID);
    }

    /// Check by musicID.
    bool isPaddedByMusicID(int musicID) const {
        std::lock_guard lock(m_mutex);
        return musicID > 0 && m_byMusicID.contains(musicID);
    }

    /// Check by channelID.
    bool isPaddedByChannel(int channelID) const {
        std::lock_guard lock(m_mutex);
        return m_byChannelID.contains(channelID);
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
