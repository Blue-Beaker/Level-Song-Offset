#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include "../offset/OffsetStorage.hpp"
#include "OffsetButton.hpp"

using namespace geode::prelude;

class OffsetPopup : public geode::Popup {
protected:
    TextInput* m_offsetInput;
    int m_levelId;
    GJGameLevel* m_level;
    OffsetButton* button;

    bool setup(GJGameLevel* level, OffsetButton* button, int currentOffset);
    void onApply(CCObject*);
    void onCancel(CCObject*);
    void onClearCache(CCObject*);

public:
    static OffsetPopup* create(GJGameLevel* level, OffsetButton* button, int currentOffset);
};

void showOffsetPopup(GJGameLevel* level, OffsetButton* button);