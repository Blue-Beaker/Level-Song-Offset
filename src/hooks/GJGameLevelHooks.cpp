#include <Geode/modify/GJGameLevel.hpp>

#include "../CacheStorage.hpp"
#include "../AsyncPregenerator.hpp"

using namespace geode::prelude;

// ─── Hook: GJGameLevel::getAudioFileName ─────────────────────────────────────

class $modify(NegativeOffsetGJGameLevel, GJGameLevel) {
    gd::string getAudioFileName() {
        int songKey = getSongKey(this);

        auto it = s_paddedPathBySongKey.find(songKey);
        if (it != s_paddedPathBySongKey.end()) {
            std::error_code ec;
            if (std::filesystem::exists(it->second, ec)) {
                log::debug("Using padded audio for song key {}", songKey);
                return gd::string(it->second.string());
            }
        }

        return GJGameLevel::getAudioFileName();
    }
};
