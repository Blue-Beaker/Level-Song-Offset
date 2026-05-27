#include <Geode/modify/GJGameLevel.hpp>

#include "../offset/negative-offset-workaround/CacheStorage.hpp"
#include "../offset/negative-offset-workaround/AsyncPregenerator.hpp"

using namespace geode::prelude;

extern int s_currentTotalOffset;

// ─── Hook: GJGameLevel::getAudioFileName ─────────────────────────────────────
// Compute the padded file path on the fly from the original audio path.
// If the padded file exists, return it; otherwise return the original.

class $modify(NegativeOffsetGJGameLevel, GJGameLevel) {
    gd::string getAudioFileName() {
        auto original = GJGameLevel::getAudioFileName();
        if (original.empty()) return original;

        auto* fileUtils = CCFileUtils::sharedFileUtils();
        std::string fullPath = fileUtils->fullPathForFilename(original.c_str(), false);
        if (fullPath.empty()) return original;

        auto srcPath = std::filesystem::path(fullPath);
        int songKey = getSongKey(this);

        auto paddedPath = getPaddedPath(songKey, s_currentTotalOffset, srcPath);
        std::error_code ec;
        if (std::filesystem::exists(paddedPath, ec)) {
            log::debug("Using padded audio: {}", paddedPath.string());
            return gd::string(paddedPath.string());
        }

        return original;
    }
};
