#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

/**
 * A wrapper around the offset icon button that can update its displayed offset value.
 *
 * Usage:
 *   auto btn = OffsetButton::create(target, selector, levelId);
 *   btn->update();       // refresh the label from storage
 *   btn->setOffset(42);  // set a new offset and refresh
 */
class OffsetButton : public CCMenuItemSpriteExtra {
protected:
    int m_levelId;
    CCLabelBMFont* m_offsetLabel = nullptr;

    bool init(CCObject* target, SEL_MenuHandler selector, int levelId);

public:
    static OffsetButton* create(CCObject* target, SEL_MenuHandler selector, int levelId);

    /**
     * Refresh the offset label from OffsetStorage.
     * Call this after changing the offset externally (e.g. after the popup saves).
     */
    void updateOffset();

    /**
     * Set the offset value and update the label immediately.
     */
    void setOffset(int offset);
};
