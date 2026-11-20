#include "queue.h"

namespace lz {

int nextTrackIndex(PlayMode mode, int current, int count, bool automatic, uint32_t randomValue) {
    if (count <= 0) return -1;
    if (mode == PlayMode::RepeatOne && automatic) return current;
    if (mode == PlayMode::Shuffle && count > 1) {
        // Map the random value onto the count-1 slots that exclude current.
        int candidate = (int)(randomValue % (uint32_t)(count - 1));
        if (candidate >= current) ++candidate;
        return candidate;
    }
    int index = current + 1;
    if (index >= count) index = 0;
    return index;
}

int previousTrackIndex(int current, int count) {
    if (count <= 0) return -1;
    int index = current < 0 ? 0 : current - 1;
    if (index < 0) index = count - 1;
    return index;
}

uint32_t clampSeek(int64_t target, uint32_t duration) {
    if (target < 0) target = 0;
    if (duration && target >= duration) target = duration > 1 ? duration - 1 : 0;
    return (uint32_t)target;
}

}  // namespace lz
