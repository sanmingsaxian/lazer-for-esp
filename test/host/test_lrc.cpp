#include "minitest.h"
#include "lrc.h"

using namespace lz;

CASE(parse_basic_timestamps) {
    auto lines = parseLrc("[00:01.00]first\n[00:05.50]second\n[00:10]third\n");
    CHECK_EQ(lines.size(), 3u);
    CHECK_EQ(lines[0].startMs, 1000u);
    CHECK_EQ(lines[0].text, std::string("first"));
    CHECK_EQ(lines[1].startMs, 5500u);
    CHECK_EQ(lines[2].startMs, 10000u);
}

CASE(parse_multiple_tags_one_line_and_sort) {
    auto lines = parseLrc("[00:20.00][00:02.00]chorus\n[00:10.00]verse\n");
    CHECK_EQ(lines.size(), 3u);
    CHECK_EQ(lines[0].startMs, 2000u);
    CHECK_EQ(lines[1].startMs, 10000u);
    CHECK_EQ(lines[2].startMs, 20000u);
    CHECK_EQ(lines[0].text, std::string("chorus"));
}

CASE(parse_ignores_metadata_and_junk) {
    auto lines = parseLrc("[ar:Someone]\n[ti:Song]\nnot a lyric\n[00:03.00]ok\n");
    CHECK_EQ(lines.size(), 1u);
    CHECK_EQ(lines[0].text, std::string("ok"));
}

CASE(parse_caps_at_72_lines) {
    std::string text;
    for (int i = 0; i < 100; ++i) {
        char line[48];
        snprintf(line, sizeof(line), "[00:%02d.00]line%d\n", i, i);
        text += line;
    }
    auto lines = parseLrc(text);
    CHECK_EQ(lines.size(), 72u);
}

CASE(active_line_selection) {
    auto lines = parseLrc("[00:01.00]a\n[00:05.00]b\n[00:09.00]c\n");
    CHECK_EQ(activeLrcLine(lines, 0), -1);
    CHECK_EQ(activeLrcLine(lines, 1000), 0);
    CHECK_EQ(activeLrcLine(lines, 4999), 0);
    CHECK_EQ(activeLrcLine(lines, 5000), 1);
    CHECK_EQ(activeLrcLine(lines, 99999), 2);
}

CASE(snapshot_window_and_progress_bounds) {
    auto lines = parseLrc("[00:01.00]a\n[00:05.00]b\n[00:09.00]c\n[00:13.00]d\n");
    LrcView view;
    lrcSnapshot(lines, 6000, view);
    CHECK(view.available);
    CHECK_EQ(view.activeLine, 1);
    // Active slot shows line b with endMs = next start.
    CHECK(view.active >= 0 && view.active < (int)LrcView::VISIBLE);
    const auto& slot = view.lines[view.active];
    CHECK(slot.valid);
    CHECK_EQ(slot.text, std::string("b"));
    CHECK_EQ(slot.startMs, 5000u);
    CHECK_EQ(slot.endMs, 9000u);
    CHECK_EQ(slot.relative, 0);
    // Last line has endMs == startMs (renderer treats as fully progressed).
    lrcSnapshot(lines, 20000, view);
    CHECK_EQ(view.activeLine, 3);
    CHECK_EQ(view.lines[view.active].endMs, 13000u);
}

CASE(snapshot_before_first_line) {
    auto lines = parseLrc("[00:05.00]a\n[00:09.00]b\n");
    LrcView view;
    lrcSnapshot(lines, 0, view);
    CHECK(view.available);
    CHECK_EQ(view.activeLine, -1);
    CHECK_EQ(view.active, -1);
    // Window still centered on line 0 so upcoming lyrics are visible.
    bool foundFirst = false;
    for (size_t i = 0; i < LrcView::VISIBLE; ++i)
        if (view.lines[i].valid && view.lines[i].text == "a" && view.lines[i].relative == 0)
            foundFirst = true;
    CHECK(foundFirst);
}

CASE(snapshot_empty_sheet) {
    std::vector<LrcLine> none;
    LrcView view;
    lrcSnapshot(none, 0, view);
    CHECK(!view.available);
    CHECK_EQ(view.active, -1);
}

int main() { return test_summary(); }
