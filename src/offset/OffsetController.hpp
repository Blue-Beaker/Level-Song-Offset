#pragma once

#include <Geode/Geode.hpp>

using namespace geode::prelude;

/**
 * Manages per-level song offset — pure logic, no hooks.
 *
 * Hooks are in src/hooks/:
 *   PlayLayerHooks.cpp       — MyPlayLayer, NegativeOffsetPlayLayer
 *   FMODAudioEngineHooks.cpp — MyFMODAudioEngine
 *   GJGameLevelHooks.cpp     — NegativeOffsetGJGameLevel
 *   LevelInfoLayerHooks.cpp  — OffsetLevelInfoLayer
 *   EditLevelLayerHooks.cpp  — OffsetEditLevelLayer
 */

/// Start async pre-generation of padded audio files for a level's songs.
/// If generation is already running, this call is silently ignored.
/// Safe to call multiple times — already-cached files are skipped.
void startPregenerateForLevel(GJGameLevel* level);
