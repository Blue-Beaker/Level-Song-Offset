#include "ui/ClearCacheSetting.hpp"

using namespace geode::prelude;

// ─── Register custom setting type ────────────────────────────────────────────

$on_mod(Loaded) {
    (void)Mod::get()->registerCustomSettingType("clear-cache-button", &ClearCacheSettingV3::parse);
}