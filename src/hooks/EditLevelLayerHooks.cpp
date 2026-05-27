#include <Geode/modify/EditLevelLayer.hpp>

#include "../OffsetPopup.hpp"
#include "../OffsetController.hpp"
#include "../Utils.hpp"
#include "../OffsetStorage.hpp"

using namespace geode::prelude;

// ─── Shared helpers (duplicated from LevelInfoLayerHooks for independence) ──

static void showOffsetPopup(GJGameLevel* level) {
    if (!level) return;
    int currentOffset = OffsetStorage::getOffsetForLevel(getLevelId(level));
    auto popup = OffsetPopup::create(level, currentOffset);
    popup->show();
}

static CCMenuItemSpriteExtra* createOffsetBtn(CCObject* target, SEL_MenuHandler selector) {
    auto circleSprite = CircleButtonSprite::createWithSprite(
        "offset-icon.png"_spr,
        1.0f,
        CircleBaseColor::Green,
        CircleBaseSize::Tiny
    );
    return CCMenuItemSpriteExtra::create(circleSprite, target, selector);
}

// ─── EditLevelLayer hook ────────────────────────────────────────────────────

class $modify(OffsetEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        auto offsetBtn = createOffsetBtn(this, menu_selector(OffsetEditLevelLayer::onOffsetButton));

        auto menu = this->getChildByID("info-button-menu");
        if (menu) {
            menu->addChild(offsetBtn);
            offsetBtn->setID("offset-button"_spr);
            offsetBtn->setPosition(80, 0);
            menu->updateLayout();
        }

        return true;
    }

    void onOffsetButton(CCObject*) {
        showOffsetPopup(this->m_level);
    }
};
