#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include "OffsetStorage.hpp"

using namespace geode::prelude;

class OffsetPopup : public geode::Popup {
protected:
    TextInput* m_offsetInput;
    int m_levelId;

    bool setup(GJGameLevel* level, int currentOffset);
    void onApply(CCObject*);
    void onCancel(CCObject*);
    void onClearCache(CCObject*);

public:
    static OffsetPopup* create(GJGameLevel* level, int currentOffset);
};
