# 1.1.0

## Added
- **Offset label on button**: The offset button now displays the current offset value as a label, updating immediately when changed.
- **`OffsetButton` class**: Extracted the offset button into a reusable class (`OffsetButton`) that wraps `CCMenuItemSpriteExtra` with automatic label updates.
- **`OffsetCalculator` module**: Centralized offset calculation logic into `applyOffset()` with support for both padded and non-padded audio tracks.
- **`PaddedTrackTracker`**: Thread-safe tracker that monitors which audio channels/tracks are using padded audio files, enabling correct offset calculation across all FMOD hooks.
- **Multi-platform CI**: Enabled CI builds for macOS, iOS, and Android (32/64-bit) alongside Windows.
- **Android build script**: Added `scripts/build-android.sh` for local Android cross-compilation.
- **Source link**: Added repository source link to `mod.json`.

## Fixed
- **Double offset on song triggers**: `queueStartMusic` with `noPrepare=false` no longer applies offset (it waits for `triggerQueuedMusic`), preventing double application. `triggerQueuedMusic` now correctly adjusts `m_start` for both queued and direct song triggers.
- **Practice mode music offset**: Added `startMusic` hook to handle offset on practice mode respawn/reset, where `queueStartMusic` is not called.
- **Song trigger offset (`loadAndPlayMusic`)**: Added `loadAndPlayMusic` hook so mid-level song triggers respect the configured offset.
- **Padded file redirect in `getAudioFileName`**: `GJGameLevel::getAudioFileName` now only redirects to padded files when `totalOffset < 0` and the negative-offset fix is enabled, preventing stale padded files from being used with positive/zero offsets.
- **Padded file start adjustment**: Padded files now always have their start time adjusted by the remainder (to skip leading silence), regardless of the `noPrepare` flag.
- **`setMusicTimeMS` padded detection**: Falls back to checking all level song keys via `PaddedTrackTracker` when channel lookup fails to find padded state.
- **Null safety**: Added null checks for `menu` in `LevelInfoLayerHooks` and for `button` in `OffsetPopup::onApply`.
- **Android build compatibility**: Fixed `gd::string` to `std::string` conversions in utility functions for Android NDK compatibility.

## Changed
- **Refactored shared helpers**: Moved `showOffsetPopup` and button creation logic from duplicated code in `EditLevelLayerHooks` and `LevelInfoLayerHooks` into `OffsetPopup` and `OffsetButton` respectively.
- **Centralized offset logic**: Extracted inline offset calculations from `FMODAudioEngineHooks` into the shared `applyOffset()` function, eliminating code duplication and ensuring consistent behavior.
- **`getLevelSongKeys`**: Changed internal string type from `gd::string` to `std::string` for broader platform compatibility.

# 1.0.0
- Initial Release
