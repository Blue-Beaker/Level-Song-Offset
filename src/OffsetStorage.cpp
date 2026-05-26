#include "OffsetStorage.hpp"

#include <cvolton.level-id-api/include/EditorIDs.hpp>

static constexpr auto SAVE_KEY = "level-offsets";

static std::unordered_map<int, float>& getOffsets() {
    static std::unordered_map<int, float> offsets = []() {
        auto& save = Mod::get()->getSaveContainer();
        auto& data = save[SAVE_KEY];
        std::unordered_map<int, float> result;
        if (data.isObject()) {
            for (auto& [key, val] : data) {
                auto idResult = numFromString<int>(key);
                if (idResult) {
                    result[idResult.unwrap()] = static_cast<float>(val.asDouble().unwrapOr(0.0));
                }
            }
        }
        return result;
    }();
    return offsets;
}

static void flushOffsets() {
    auto& offsets = getOffsets();
    auto& save = Mod::get()->getSaveContainer();
    auto& data = save[SAVE_KEY];
    data = matjson::Value::object();
    for (auto& [levelId, offset] : offsets) {
        data[fmt::format("{}", levelId)] = static_cast<double>(offset);
    }
}

/// Resolve the effective level ID, using EditorIDs for editor levels.
static int resolveLevelId(GJGameLevel* level) {
    if (!level) return 0;
    if (level->m_levelID != 0) return level->m_levelID;
    // Editor level — use EditorIDs API
    return EditorIDs::getID(level);
}

float OffsetStorage::getOffsetForLevel(int levelId) {
    auto& offsets = getOffsets();
    auto it = offsets.find(levelId);
    if (it != offsets.end()) {
        return it->second;
    }
    return 0.f;
}

float OffsetStorage::getOffsetForLevel(GJGameLevel* level) {
    return getOffsetForLevel(resolveLevelId(level));
}

void OffsetStorage::setOffsetForLevel(int levelId, float offset) {
    auto& offsets = getOffsets();
    offsets[levelId] = offset;
    flushOffsets();
}

void OffsetStorage::setOffsetForLevel(GJGameLevel* level, float offset) {
    setOffsetForLevel(resolveLevelId(level), offset);
}
