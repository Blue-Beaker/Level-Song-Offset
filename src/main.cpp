#include "OffsetPopup.hpp"
#include "OffsetController.hpp"
#include "LevelUtils.hpp"

// ─── Shared helpers ──────────────────────────────────────────────────────────

/// Show the offset popup for a level.
static void showOffsetPopup(GJGameLevel* level) {
    if (!level) return;
    float currentOffset = OffsetStorage::getOffsetForLevel(getLevelId(level));
    auto popup = OffsetPopup::create(level, currentOffset);
    popup->show();
}

/// Create an offset button with the standard icon, attached to the given
/// target/selector. Caller must set position, add to a menu, and set ID.
static CCMenuItemSpriteExtra* createOffsetBtn(CCObject* target, SEL_MenuHandler selector) {
    auto circleSprite = CircleButtonSprite::createWithSprite(
        "offset-icon.png"_spr,
        0.8f,
        CircleBaseColor::Green,
        CircleBaseSize::Small
    );
    return CCMenuItemSpriteExtra::create(circleSprite, target, selector);
}

// ─── LevelInfoLayer hook ────────────────────────────────────────────────────

#include <Geode/modify/LevelInfoLayer.hpp>
class $modify(OffsetLevelInfoLayer, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level,challenge)) {
			return false;
		}

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

// ─── EditLevelLayer hook ────────────────────────────────────────────────────

#include <Geode/modify/EditLevelLayer.hpp>
class $modify(OffsetEditLevelLayer, EditLevelLayer) {
	bool init(GJGameLevel* level) {
		if (!EditLevelLayer::init(level)) return false;

		auto offsetBtn = createOffsetBtn(this, menu_selector(OffsetEditLevelLayer::onOffsetButton));

		auto menu = this->getChildByID("info-button-menu");
		if (menu) {
			menu->addChild(offsetBtn);
			offsetBtn->setID("offset-button"_spr);
			offsetBtn->setPosition(80,0);
			menu->updateLayout();
		}

		return true;
	}

	void onOffsetButton(CCObject*) {
		showOffsetPopup(this->m_level);
	}
};