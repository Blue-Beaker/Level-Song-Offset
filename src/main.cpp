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

class OffsetPopup : public geode::Popup {
protected:
    TextInput* m_offsetInput;
    int m_levelId;

    bool setup(int levelId, float currentOffset) {
        m_levelId = levelId;
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
        log::debug("Set offset for level {} to {}ms", m_levelId, offset);

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
    static OffsetPopup* create(int levelId, float currentOffset) {
        auto ret = new OffsetPopup();
        if (ret->init(280.f, 180.f)) {
            ret->setup(levelId, currentOffset);
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

/**
 * `$modify` lets you extend and modify GD's classes.
 * To hook a function in Geode, simply $modify the class
 * and write a new function definition with the signature of
 * the function you want to hook.
 *
 * Here we use the overloaded `$modify` macro to set our own class name,
 * so that we can use it for button callbacks.
 *
 * Notice the header being included, you *must* include the header for
 * the class you are modifying, or you will get a compile error.
 *
 * Another way you could do this is like this:
 *
 * struct MyMenuLayer : Modify<MyMenuLayer, MenuLayer> {};
 */
#include <Geode/modify/LevelInfoLayer.hpp>
class $modify(MyLevelInfoLayer, LevelInfoLayer) {
	/**
	 * Typically classes in GD are initialized using the `init` function, (though not always!),
	 * so here we use it to add our own button to the bottom menu.
	 *
	 * Note that for all hooks, your signature has to *match exactly*,
	 * `void init()` would not place a hook!
	*/
	bool init(GJGameLevel* level, bool challenge) {
		/**
		 * We call the original init function so that the
		 * original class is properly initialized.
		 */
		if (!LevelInfoLayer::init(level,challenge)) {
			return false;
		}

		/**
		 * You can use methods from the `geode::log` namespace to log messages to the console,
		 * being useful for debugging and such. See this page for more info about logging:
		 * https://docs.geode-sdk.org/tutorials/logging
		*/
		log::debug("Hello from my MenuLayer::init hook! This layer has {} children.", this->getChildrenCount());

		/**
		 * See this page for more info about buttons
		 * https://docs.geode-sdk.org/tutorials/buttons
		*/
		auto myButton = CCMenuItemSpriteExtra::create(
			CCSprite::createWithSpriteFrameName("GJ_likeBtn_001.png"),
			this,
			/**
			 * Here we use the name we set earlier for our modify class.
			*/
			menu_selector(MyLevelInfoLayer::onMyButton)
		);
		myButton->setPosition(80,0);

		/**
		 * Here we access the `bottom-menu` node by its ID, and add our button to it.
		 * Node IDs are a Geode feature, see this page for more info about it:
		 * https://docs.geode-sdk.org/tutorials/nodetree
		*/
		auto menu = this->getChildByID("other-menu");
		menu->addChild(myButton);

		/**
		 * The `_spr` string literal operator just prefixes the string with
		 * your mod id followed by a slash. This is good practice for setting your own node ids.
		*/
		myButton->setID("my-button"_spr);

		/**
		 * We update the layout of the menu to ensure that our button is properly placed.
		 * This is yet another Geode feature, see this page for more info about it:
		 * https://docs.geode-sdk.org/tutorials/layouts
		*/
		menu->updateLayout();

		/**
		 * We return `true` to indicate that the class was properly initialized.
		 */
		return true;
	}

	/**
	 * This is the callback function for the button we created earlier.
	 * The signature for button callbacks must always be the same,
	 * return type `void` and taking a `CCObject*`.
	*/
	void onMyButton(CCObject*) {
		auto level = this->m_level;
		if (!level) return;

		int levelId = level->m_levelID;
		float currentOffset = OffsetStorage::getOffsetForLevel(levelId);

		auto popup = OffsetPopup::create(levelId, currentOffset);
		popup->show();
	}
};