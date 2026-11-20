#pragma once
#include <cstdint>

namespace lz {

enum class PlayMode : uint8_t { Sequential = 0, RepeatOne = 1, Shuffle = 2 };

// Port of AudioEngine::nextIndex. randomValue feeds the shuffle pick and must
// never return the current index when count > 1.
int nextTrackIndex(PlayMode mode, int current, int count, bool automatic, uint32_t randomValue);

// Port of AudioEngine::previous index selection (position>5 restart is handled
// by the engine, not here).
int previousTrackIndex(int current, int count);

// Port of AudioEngine::seek clamping.
uint32_t clampSeek(int64_t target, uint32_t duration);

}  // namespace lz
