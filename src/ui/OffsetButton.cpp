#include "OffsetButton.hpp"
#include "../offset/OffsetStorage.hpp"

bool OffsetButton::init(CCObject* target, SEL_MenuHandler selector, int levelId) {
    m_levelId = levelId;

    auto circleSprite = CircleButtonSprite::createWithSprite(
        "offset-icon.png"_spr,
        1.0f,
        CircleBaseColor::Green,
        CircleBaseSize::Tiny
    );

    if (!CCMenuItemSpriteExtra::init(circleSprite, nullptr, target, selector))
        return false;

    updateOffset();

    return true;
}

OffsetButton* OffsetButton::create(CCObject* target, SEL_MenuHandler selector, int levelId) {
    auto ret = new OffsetButton();
    if (ret && ret->init(target, selector, levelId)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void OffsetButton::updateOffset() {
    int offset = OffsetStorage::getOffsetForLevel(m_levelId);
    setOffset(offset);
}

void OffsetButton::setOffset(int offset) {
    if (m_offsetLabel) {
        m_offsetLabel->removeFromParent();
        m_offsetLabel = nullptr;
    }

    if (offset != 0) {
        auto children = getChildren();
        if (!children || children->count() == 0) return;
        auto sprite = static_cast<CCSprite*>(children->objectAtIndex(0));
        if (!sprite) return;
        m_offsetLabel = CCLabelBMFont::create(
            geode::utils::numToString(offset).c_str(), "bigFont.fnt"
        );
        m_offsetLabel->setAnchorPoint(ccp(0.5f, 0.0f));
        m_offsetLabel->setScale(std::min(0.3f, 35.0f / m_offsetLabel->getContentWidth()));
        m_offsetLabel->setPosition(sprite->getContentWidth() / 2, 0);
        sprite->addChild(m_offsetLabel);
    }
}
