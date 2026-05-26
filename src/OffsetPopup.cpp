#include "OffsetPopup.hpp"
#include "CacheStorage.hpp"
#include "LevelUtils.hpp"

bool OffsetPopup::setup(GJGameLevel* level, float currentOffset) {
    m_levelId = getLevelId(level);
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

    // Delete button
    auto delBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png"),
        this,
        menu_selector(OffsetPopup::onClearCache)
    );

    auto menu = CCMenu::create();
    menu->addChild(okBtn);
    menu->addChild(cancelBtn);
    menu->addChild(delBtn);
    menu->alignItemsHorizontallyWithPadding(20);
    m_mainLayer->addChildAtPosition(menu, Anchor::Center, ccp(0, -50));

    return true;
}

void OffsetPopup::onApply(CCObject*) {
    float offset = 0.f;
    auto result = numFromString<float>(m_offsetInput->getString());
    if (result) {
        offset = result.unwrap();
    }
    OffsetStorage::setOffsetForLevel(m_levelId, offset);
    log::debug("Set offset for level {} (resolved) to {}ms", m_levelId, offset);

    Notification::create(
        fmt::format("Offset set to {:.0f}ms for level {}", offset, m_levelId),
        NotificationIcon::Success
    )->show();

    this->onClose(nullptr);
}

void OffsetPopup::onCancel(CCObject*) {
    this->onClose(nullptr);
}

void OffsetPopup::onClearCache(CCObject*) {
    createQuickPopup(
        "Clear Cache",
        "Are you sure you want to clear all cached delayed audio files? Original songs won't be deleted.",
        "Cancel", "Clear",
        [](auto, bool btn2) {
            if (btn2) {
                reduceCacheToSize(0, {});
            }
        }
    );
}

OffsetPopup* OffsetPopup::create(GJGameLevel* level, float currentOffset) {
    auto ret = new OffsetPopup();
    if (ret->init(280.f, 180.f)) {
        ret->setup(level, currentOffset);
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}
