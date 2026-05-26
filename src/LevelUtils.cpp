#include "LevelUtils.hpp"

#include <cvolton.level-id-api/include/EditorIDs.hpp>

int getLevelId(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_levelID != 0) return level->m_levelID;
    // Editor level — use EditorIDs API
    return EditorIDs::getID(level);
}
