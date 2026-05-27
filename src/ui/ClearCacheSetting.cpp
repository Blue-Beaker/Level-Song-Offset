#include "ClearCacheSetting.hpp"
#include "../offset/negative-offset-workaround/CacheStorage.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

// ─── ClearCacheSettingV3 ─────────────────────────────────────────────────────

Result<std::shared_ptr<SettingV3>> ClearCacheSettingV3::parse(
    std::string const& key, std::string const& modID, matjson::Value const& json
) {
    auto res = std::make_shared<ClearCacheSettingV3>();
    auto root = checkJson(json, "ClearCacheSettingV3");
    res->init(key, modID, root);
    res->parseNameAndDescription(root);
    res->parseEnableIf(root);
    root.checkUnknownKeys();
    return root.ok(std::static_pointer_cast<SettingV3>(res));
}

bool ClearCacheSettingV3::load(matjson::Value const& json) {
    return true;
}

bool ClearCacheSettingV3::save(matjson::Value& json) const {
    return true;
}

bool ClearCacheSettingV3::isDefaultValue() const {
    return true;
}

void ClearCacheSettingV3::reset() {}

// ─── ClearCacheSettingNodeV3 ─────────────────────────────────────────────────

class ClearCacheSettingNodeV3 : public SettingNodeV3 {
protected:
    ButtonSprite* m_buttonSprite;
    CCMenuItemSpriteExtra* m_button;
    CCLabelBMFont* m_cacheSizeLabel;

    bool init(std::shared_ptr<ClearCacheSettingV3> setting, float width) {
        if (!SettingNodeV3::init(setting, width))
            return false;

        // Cache size label (updated on each state refresh)
        m_cacheSizeLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_cacheSizeLabel->setScale(.35f);
        m_cacheSizeLabel->setAnchorPoint(ccp(1.0f, 0.5f));

        m_buttonSprite = ButtonSprite::create("Clear", "goldFont.fnt", "GJ_button_01.png", .8f);
        m_buttonSprite->setScale(.5f);
        m_button = CCMenuItemSpriteExtra::create(
            m_buttonSprite, this, menu_selector(ClearCacheSettingNodeV3::onButton)
        );
        m_button->setAnchorPoint(ccp(0.5f,0.5f));

        this->getButtonMenu()->addChildAtPosition(m_button, Anchor::Right, ccp(-(m_button->getContentWidth()/2)-5,0));
        this->getButtonMenu()->addChildAtPosition(m_cacheSizeLabel, Anchor::Right, ccp(-m_button->getContentWidth()-10,0));

        // this->getButtonMenu()->setContentWidth(160);
        this->getButtonMenu()->updateLayout();

        this->updateState(nullptr);
        return true;
    }

    void updateState(CCNode* invoker) override {
        SettingNodeV3::updateState(invoker);
        auto shouldEnable = this->getSetting()->shouldEnable();
        m_button->setEnabled(shouldEnable);
        m_buttonSprite->setCascadeColorEnabled(true);
        m_buttonSprite->setCascadeOpacityEnabled(true);
        m_buttonSprite->setOpacity(shouldEnable ? 255 : 155);
        m_buttonSprite->setColor(shouldEnable ? ccWHITE : ccGRAY);

        // Refresh cache size info
        auto collection = collectRemovableCacheFiles({});
        if (collection.totalFiles > 0) {
            m_cacheSizeLabel->setString(fmt::format(
                "{:.2f} MB",
                static_cast<double>(collection.totalSize) / (1024.0 * 1024.0)
            ).c_str());
            m_cacheSizeLabel->setVisible(true);
        } else {
            m_cacheSizeLabel->setString("No cached audio");
            m_cacheSizeLabel->setVisible(true);
        }
    }

    void onButton(CCObject*) {
        promptClearAllCache();
    }

    void onCommit() override {}
    void onResetToDefault() override {}

public:
    static ClearCacheSettingNodeV3* create(std::shared_ptr<ClearCacheSettingV3> setting, float width) {
        auto ret = new ClearCacheSettingNodeV3();
        if (ret->init(setting, width)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    bool hasUncommittedChanges() const override { return false; }
    bool hasNonDefaultValue() const override { return false; }

    std::shared_ptr<ClearCacheSettingV3> getSetting() const {
        return std::static_pointer_cast<ClearCacheSettingV3>(SettingNodeV3::getSetting());
    }
};

// ─── createNode (defined out-of-line because NodeV3 is forward-declared) ─────

SettingNodeV3* ClearCacheSettingV3::createNode(float width) {
    return ClearCacheSettingNodeV3::create(
        std::static_pointer_cast<ClearCacheSettingV3>(shared_from_this()),
        width
    );
}
