#include "Utils.hpp"
#include "../offset/OffsetStorage.hpp"
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
        auto ids = level->m_songIDs;
        size_t pos = 0;
        while ((pos = ids.find(',')) != gd::string::npos) {
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

// ─── Shared helpers ──────────────────────────────────────────────────────────

/// Show the offset popup for a level.
void showOffsetPopup(GJGameLevel* level) {
    if (!level) return;
    int currentOffset = OffsetStorage::getOffsetForLevel(getLevelId(level));
    auto popup = OffsetPopup::create(level, currentOffset);
    popup->show();
}

/// Create an offset button with the standard icon.
CCMenuItemSpriteExtra* createOffsetBtn(CCObject* target, SEL_MenuHandler selector, int levelId) {
    auto circleSprite = CircleButtonSprite::createWithSprite(
        "offset-icon.png"_spr,
        1.0f,
        CircleBaseColor::Green,
        CircleBaseSize::Tiny
    );
    int offset = OffsetStorage::getOffsetForLevel(levelId);
    if(offset!=0){
        auto offsetLabel = CCLabelBMFont::create(geode::utils::numToString(offset).c_str(),"bigFont.fnt");
        offsetLabel->setAnchorPoint(ccp(0.5,0));
        offsetLabel->setScale(std::min(0.3f,35/offsetLabel->getContentWidth()));
        offsetLabel->setPosition(circleSprite->getContentWidth()/2,0);
        circleSprite->addChild(offsetLabel);
    }
    

    return CCMenuItemSpriteExtra::create(circleSprite, target, selector);
}
