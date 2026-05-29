#include "OffsetPopup.hpp"
#include "../offset/OffsetController.hpp"
#include "../offset/negative-offset-workaround/CacheStorage.hpp"
#include "../utils/Utils.hpp"

bool OffsetPopup::setup(GJGameLevel* level, OffsetButton* button, int currentOffset) {
    if (!level) return false;
    this->button=button;

    m_levelId = getLevelId(level);
    m_level = level;
    this->setTitle("Level Song Offset");

    // Input label
    auto label = CCLabelBMFont::create("Offset (ms):", "bigFont.fnt");
    label->setScale(0.6f);
    m_mainLayer->addChildAtPosition(label, Anchor::Center, ccp(0, 25));

    // Text input
    m_offsetInput = TextInput::create(200.f, "0", "bigFont.fnt");
    m_offsetInput->setString(fmt::format("{}", currentOffset));
    m_offsetInput->setCommonFilter(CommonFilter::Int);
    m_offsetInput->setMaxCharCount(8);
    m_mainLayer->addChildAtPosition(m_offsetInput, Anchor::Center, ccp(0, -5));

    // Apply button
    auto okBtn = CCMenuItemSpriteExtra::create(
        ButtonSprite::create("Apply"),
        this,
        menu_selector(OffsetPopup::onApply)
    );

    // Cancel button
    // auto cancelBtn = CCMenuItemSpriteExtra::create(
    //     ButtonSprite::create("Cancel"),
    //     this,
    //     menu_selector(OffsetPopup::onCancel)
    // );

    // Delete button
    auto delBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_trashBtn_001.png"),
        this,
        menu_selector(OffsetPopup::onClearCache)
    );

    auto menu = CCMenu::create();
    menu->addChild(okBtn);
    // menu->addChild(cancelBtn);
    menu->addChild(delBtn);
    menu->alignItemsHorizontallyWithPadding(20);
    m_mainLayer->addChildAtPosition(menu, Anchor::Center, ccp(0, -50));

    return true;
}

void OffsetPopup::onApply(CCObject*) {
    int offset = 0;
    auto result = numFromString<int>(m_offsetInput->getString());
    if (result) {
        offset = result.unwrap();
    }
    OffsetStorage::setOffsetForLevel(m_levelId, offset);
    log::debug("Set offset for level {} (resolved) to {}ms", m_levelId, offset);

    // Start async pre-generation for the new offset value
    startPregenerateForLevel(m_level);

    Notification::create(
        fmt::format("Offset set to {}ms for level {}", offset, m_levelId),
        NotificationIcon::Success
    )->show();

    if(this->button!=nullptr){
        this->button->setOffset(offset);
    }

    this->onClose(nullptr);
}

void OffsetPopup::onCancel(CCObject*) {
    this->onClose(nullptr);
}

void OffsetPopup::onClearCache(CCObject*) {
    promptClearAllCache();
}

OffsetPopup* OffsetPopup::create(GJGameLevel* level, OffsetButton* button, int currentOffset) {
    auto ret = new OffsetPopup();
    if (ret->init(280.f, 180.f)) {
        ret->setup(level, button, currentOffset);
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

// ─── Shared helpers ──────────────────────────────────────────────────────────

/// Show the offset popup for a level.
void showOffsetPopup(GJGameLevel* level, OffsetButton* button) {
    if (!level) return;
    int currentOffset = OffsetStorage::getOffsetForLevel(getLevelId(level));
    auto popup = OffsetPopup::create(level, button, currentOffset);
    popup->show();
}