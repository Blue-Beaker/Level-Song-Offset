#include <Geode/modify/LevelInfoLayer.hpp>

#include "../ui/OffsetPopup.hpp"
#include "../offset/OffsetController.hpp"
#include "../utils/Utils.hpp"
#include "../offset/OffsetStorage.hpp"

using namespace geode::prelude;

// ─── Shared helpers ──────────────────────────────────────────────────────────

/// Show the offset popup for a level.
static void showOffsetPopup(GJGameLevel* level) {
    if (!level) return;
    int currentOffset = OffsetStorage::getOffsetForLevel(getLevelId(level));
    auto popup = OffsetPopup::create(level, currentOffset);
    popup->show();
}

/// Create an offset button with the standard icon.
static CCMenuItemSpriteExtra* createOffsetBtn(CCObject* target, SEL_MenuHandler selector) {
    auto circleSprite = CircleButtonSprite::createWithSprite(
        "offset-icon.png"_spr,
        1.0f,
        CircleBaseColor::Green,
        CircleBaseSize::Tiny
    );
    return CCMenuItemSpriteExtra::create(circleSprite, target, selector);
}

// ─── LevelInfoLayer hook ────────────────────────────────────────────────────

class $modify(OffsetLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        auto offsetBtn = createOffsetBtn(this, menu_selector(OffsetLevelInfoLayer::onOffsetButton));
        offsetBtn->setPosition(80, 0);

        auto menu = this->getChildByID("other-menu");
        menu->addChild(offsetBtn);
        offsetBtn->setID("offset-button"_spr);
        menu->updateLayout();

        return true;
    }

    void onOffsetButton(CCObject*) {
        showOffsetPopup(this->m_level);
    }
};
