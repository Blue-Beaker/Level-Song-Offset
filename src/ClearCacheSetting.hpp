#pragma once

#include <Geode/loader/SettingV3.hpp>

using namespace geode::prelude;

/// Custom setting type "clear-cache-button" that shows a "Clear Audio Cache"
/// button in the mod's settings page. Clicking it deletes all padded audio
/// cache files used for the negative offset workaround.
class ClearCacheSettingV3 : public SettingV3 {
public:
    static Result<std::shared_ptr<SettingV3>> parse(
        std::string const& key, std::string const& modID, matjson::Value const& json
    );

    bool load(matjson::Value const& json) override;
    bool save(matjson::Value& json) const override;
    bool isDefaultValue() const override;
    void reset() override;

    SettingNodeV3* createNode(float width) override;
};
