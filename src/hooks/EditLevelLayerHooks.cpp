#include <Geode/modify/EditLevelLayer.hpp>

#include "../ui/OffsetPopup.hpp"
#include "../ui/OffsetButton.hpp"
#include "../offset/OffsetController.hpp"
#include "../utils/Utils.hpp"

using namespace geode::prelude;

// ─── EditLevelLayer hook ────────────────────────────────────────────────────

class $modify(OffsetEditLevelLayer, EditLevelLayer) {
	struct Fields {
		OffsetButton* offsetBtn;
	};
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        auto offsetBtn = OffsetButton::create(this, menu_selector(OffsetEditLevelLayer::onOffsetButton), getLevelId(level));
        m_fields->offsetBtn=offsetBtn;

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
        showOffsetPopup(this->m_level,this->m_fields->offsetBtn);
    }
};
