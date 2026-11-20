#include "minitest.h"
#include "queue.h"

using namespace lz;

CASE(sequential_wraps) {
    CHECK_EQ(nextTrackIndex(PlayMode::Sequential, 0, 3, false, 0), 1);
    CHECK_EQ(nextTrackIndex(PlayMode::Sequential, 2, 3, true, 0), 0);
    CHECK_EQ(nextTrackIndex(PlayMode::Sequential, -1, 3, false, 0), 0);
    CHECK_EQ(nextTrackIndex(PlayMode::Sequential, 0, 0, false, 0), -1);
}

CASE(repeat_one_only_on_automatic) {
    CHECK_EQ(nextTrackIndex(PlayMode::RepeatOne, 1, 3, true, 0), 1);
    CHECK_EQ(nextTrackIndex(PlayMode::RepeatOne, 1, 3, false, 0), 2);
}

CASE(shuffle_never_repeats_current) {
    for (uint32_t r = 0; r < 50; ++r) {
        int n = nextTrackIndex(PlayMode::Shuffle, 1, 4, false, r);
        CHECK(n >= 0 && n < 4 && n != 1);
    }
    // Single track: shuffle falls through to sequential wrap.
    CHECK_EQ(nextTrackIndex(PlayMode::Shuffle, 0, 1, false, 7), 0);
}

CASE(previous_wraps_and_handles_no_selection) {
    CHECK_EQ(previousTrackIndex(0, 3), 2);
    CHECK_EQ(previousTrackIndex(2, 3), 1);
    CHECK_EQ(previousTrackIndex(-1, 3), 0);
    CHECK_EQ(previousTrackIndex(0, 0), -1);
}

CASE(seek_clamping) {
    CHECK_EQ(clampSeek(-10, 100), 0u);
    CHECK_EQ(clampSeek(50, 100), 50u);
    CHECK_EQ(clampSeek(100, 100), 99u);
    CHECK_EQ(clampSeek(500, 100), 99u);
    CHECK_EQ(clampSeek(5, 1), 0u);
    CHECK_EQ(clampSeek(5, 0), 5u);  // unknown duration: no upper clamp
}

int main() { return test_summary(); }
