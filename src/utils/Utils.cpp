#include "Utils.hpp"
#include "../ui/OffsetPopup.hpp"

#include <cvolton.level-id-api/include/EditorIDs.hpp>

int getLevelId(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_levelID != 0) return level->m_levelID;
    return EditorIDs::getID(level);
}

std::vector<int> getLevelSongKeys(GJGameLevel* level) {
    std::vector<int> keys;
    if (!level) return keys;

    int mainKey = (level->m_songID != 0) ? level->m_songID
                                          : (-level->m_audioTrack - 1);
    keys.push_back(mainKey);

    if (!level->m_songIDs.empty()) {
        std::string ids(level->m_songIDs);
        size_t pos = 0;
        while ((pos = ids.find(',')) != std::string::npos) {
            auto idStr = ids.substr(0, pos);
            ids.erase(0, pos + 1);
            auto key = geode::utils::numFromString<int>(idStr);
            if (key) keys.push_back(key.unwrap());
        }
        if (!ids.empty()) {
            auto key = geode::utils::numFromString<int>(ids);
            if (key) keys.push_back(key.unwrap());
        }
    }

    return keys;
}