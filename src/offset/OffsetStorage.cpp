#include "OffsetStorage.hpp"

static constexpr auto SAVE_KEY = "level-offsets";

static std::unordered_map<int, int>& getOffsets() {
    static std::unordered_map<int, int> offsets = []() {
        auto& save = Mod::get()->getSaveContainer();
        auto& data = save[SAVE_KEY];
        std::unordered_map<int, int> result;
        if (data.isObject()) {
            for (auto& [key, val] : data) {
                auto idResult = numFromString<int>(key);
                if (idResult) {
                    result[idResult.unwrap()] = static_cast<int>(std::round(val.asDouble().unwrapOr(0.0)));
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
        data[fmt::format("{}", levelId)] = offset;
    }
}

int OffsetStorage::getOffsetForLevel(int levelId) {
    auto& offsets = getOffsets();
    auto it = offsets.find(levelId);
    if (it != offsets.end()) {
        return it->second;
    }
    return 0;
}

void OffsetStorage::setOffsetForLevel(int levelId, int offset) {
    auto& offsets = getOffsets();
    if (offset == 0) {
        // Remove entry if offset==0
        offsets.erase(levelId);
    } else {
        offsets[levelId] = offset;
    }
    flushOffsets();
}
