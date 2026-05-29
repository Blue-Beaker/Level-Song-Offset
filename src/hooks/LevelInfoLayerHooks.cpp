#include <Geode/modify/LevelInfoLayer.hpp>

#include "../ui/OffsetPopup.hpp"
#include "../offset/OffsetController.hpp"
#include "../utils/Utils.hpp"
#include "../offset/OffsetStorage.hpp"

using namespace geode::prelude;

// ─── LevelInfoLayer hook ────────────────────────────────────────────────────

class $modify(OffsetLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        auto offsetBtn = createOffsetBtn(this, menu_selector(OffsetLevelInfoLayer::onOffsetButton), getLevelId(level));
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
