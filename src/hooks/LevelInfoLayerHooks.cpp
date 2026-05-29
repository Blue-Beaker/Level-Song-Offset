#include <Geode/modify/LevelInfoLayer.hpp>

#include "../ui/OffsetPopup.hpp"
#include "../ui/OffsetButton.hpp"
#include "../offset/OffsetController.hpp"
#include "../utils/Utils.hpp"

using namespace geode::prelude;

// ─── LevelInfoLayer hook ────────────────────────────────────────────────────

class $modify(OffsetLevelInfoLayer, LevelInfoLayer) {
	struct Fields {
		OffsetButton* offsetBtn;
	};
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        auto offsetBtn = OffsetButton::create(this, menu_selector(OffsetLevelInfoLayer::onOffsetButton), getLevelId(level));
        m_fields->offsetBtn=offsetBtn;
        
        offsetBtn->setPosition(80, 0);

        auto menu = this->getChildByID("other-menu");
        menu->addChild(offsetBtn);
        offsetBtn->setID("offset-button"_spr);
        menu->updateLayout();

        return true;
    }

    void onOffsetButton(CCObject*) {
        showOffsetPopup(this->m_level,this->m_fields->offsetBtn);
    }
};
