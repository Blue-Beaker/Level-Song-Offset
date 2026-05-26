/**
 * Include the Geode headers.
 */
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include "OffsetStorage.hpp"

/**
 * Brings cocos2d and all Geode namespaces to the current scope.
 */
using namespace geode::prelude;

#include <cvolton.level-id-api/include/EditorIDs.hpp>

class OffsetPopup : public geode::Popup {
protected:
    TextInput* m_offsetInput;
    int m_levelId;

    bool setup(GJGameLevel* level, float currentOffset) {
        m_levelId = level->m_levelID != 0 ? level->m_levelID : EditorIDs::getID(level);
        this->setTitle("Level Song Offset");

        // Input label
        auto label = CCLabelBMFont::create("Offset (ms):", "bigFont.fnt");
        label->setScale(0.6f);
        m_mainLayer->addChildAtPosition(label, Anchor::Center, ccp(0, 25));

        // Text input
        m_offsetInput = TextInput::create(200.f, "0", "bigFont.fnt");
        m_offsetInput->setString(fmt::format("{:.0f}", currentOffset));
        m_offsetInput->setCommonFilter(CommonFilter::Float);
        m_offsetInput->setMaxCharCount(8);
        m_mainLayer->addChildAtPosition(m_offsetInput, Anchor::Center, ccp(0, -5));

        // Apply button
        auto okBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Apply"),
            this,
            menu_selector(OffsetPopup::onApply)
        );

        // Cancel button
        auto cancelBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Cancel"),
            this,
            menu_selector(OffsetPopup::onCancel)
        );

        auto menu = CCMenu::create();
        menu->addChild(okBtn);
        menu->addChild(cancelBtn);
        menu->alignItemsHorizontallyWithPadding(20);
        m_mainLayer->addChildAtPosition(menu, Anchor::Center, ccp(0, -50));

        return true;
    }

    void onApply(CCObject*) {
        float offset = 0.f;
        auto result = numFromString<float>(m_offsetInput->getString());
        if (result) {
            offset = result.unwrap();
        }
        OffsetStorage::setOffsetForLevel(m_levelId, offset);
        log::debug("Set offset for level {} (resolved) to {}ms", m_levelId, offset);

        FLAlertLayer::create(
            "Offset Set",
            fmt::format("Level {} offset: {:.0f}ms", m_levelId, offset),
            "OK"
        )->show();

        this->onClose(nullptr);
    }

    void onCancel(CCObject*) {
        this->onClose(nullptr);
    }

public:
    static OffsetPopup* create(GJGameLevel* level, float currentOffset) {
        auto ret = new OffsetPopup();
        if (ret->init(280.f, 180.f)) {
            ret->setup(level, currentOffset);
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

#include "OffsetController.hpp"

#include <Geode/modify/LevelInfoLayer.hpp>
class $modify(OffsetLevelInfoLayer, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level,challenge)) {
			return false;
		}

		log::debug("OffsetLevelInfoLayer::init - Layer has {} children.", this->getChildrenCount());

		// Create the offset button using CircleButtonSprite
		// Background: Geode's blank green circle (handled by CircleButtonSprite internally)
		// Top:        custom offset icon loaded via CCSprite::create
		auto circleSprite = CircleButtonSprite::createWithSprite(
			"offset-icon.png"_spr,
			0.8f,
			CircleBaseColor::Green,
			CircleBaseSize::Small
		);

		auto offsetBtn = CCMenuItemSpriteExtra::create(
			circleSprite,
			this,
			menu_selector(OffsetLevelInfoLayer::onOffsetButton)
		);
		offsetBtn->setPosition(80, 0);

		auto menu = this->getChildByID("other-menu");
		menu->addChild(offsetBtn);
		offsetBtn->setID("offset-button"_spr);
		menu->updateLayout();

		return true;
	}

	void onOffsetButton(CCObject*) {
		auto level = this->m_level;
		if (!level) return;

		int levelId = level->m_levelID;
		float currentOffset = OffsetStorage::getOffsetForLevel(level);

		auto popup = OffsetPopup::create(level, currentOffset);
		popup->show();
	}
};

#include <Geode/modify/EditLevelLayer.hpp>
class $modify(OffsetEditLevelLayer, EditLevelLayer) {
	bool init(GJGameLevel* level) {
		if (!EditLevelLayer::init(level)) return false;

		auto circleSprite = CircleButtonSprite::createWithSprite(
			"offset-icon.png"_spr,
			0.8f,
			CircleBaseColor::Green,
			CircleBaseSize::Small
		);

		auto offsetBtn = CCMenuItemSpriteExtra::create(
			circleSprite,
			this,
			menu_selector(OffsetEditLevelLayer::onOffsetButton)
		);

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
		auto level = this->m_level;
		if (!level) return;

		int levelId = level->m_levelID;
		float currentOffset = OffsetStorage::getOffsetForLevel(level);

		auto popup = OffsetPopup::create(level, currentOffset);
		popup->show();
	}
};