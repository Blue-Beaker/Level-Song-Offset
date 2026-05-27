#include <Geode/modify/GJGameLevel.hpp>

#include "../CacheStorage.hpp"
#include "../AsyncPregenerator.hpp"

using namespace geode::prelude;

// ─── Hook: GJGameLevel::getAudioFileName ─────────────────────────────────────
// Look up the padded file by hashing the original audio file path.
// This correctly handles jukebox nong songs (same song ID, different path).

class $modify(NegativeOffsetGJGameLevel, GJGameLevel) {
    gd::string getAudioFileName() {
        auto original = GJGameLevel::getAudioFileName();
        if (original.empty()) return original;

        auto* fileUtils = CCFileUtils::sharedFileUtils();
        std::string fullPath = fileUtils->fullPathForFilename(original.c_str(), false);
        if (fullPath.empty()) return original;

        auto fileKey = hashSourcePath(std::filesystem::path(fullPath));
        auto it = s_paddedPathByFileKey.find(fileKey);
        if (it != s_paddedPathByFileKey.end()) {
            std::error_code ec;
            if (std::filesystem::exists(it->second, ec)) {
                log::debug("Using padded audio for file key {:x}", fileKey);
                return gd::string(it->second.string());
            }
        }

        return original;
    }
};
