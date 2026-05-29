#include <Geode/modify/EditLevelLayer.hpp>

#include "../ui/OffsetPopup.hpp"
#include "../offset/OffsetController.hpp"
#include "../utils/Utils.hpp"
#include "../offset/OffsetStorage.hpp"

using namespace geode::prelude;

// ─── EditLevelLayer hook ────────────────────────────────────────────────────

class $modify(OffsetEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        auto offsetBtn = createOffsetBtn(this, menu_selector(OffsetEditLevelLayer::onOffsetButton), getLevelId(level));

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
