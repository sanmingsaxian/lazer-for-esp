#include "minitest.h"
#include "pathutil.h"

using namespace lz;

CASE(base_and_parent) {
    CHECK_EQ(baseName("/Music/a.mp3"), std::string("a.mp3"));
    CHECK_EQ(baseName("a.mp3"), std::string("a.mp3"));
    CHECK_EQ(parentPath("/Music/a.mp3"), std::string("/Music"));
    CHECK_EQ(parentPath("/a.mp3"), std::string("/"));
    CHECK_EQ(parentPath("/"), std::string("/"));
    CHECK_EQ(joinPath("/", "x"), std::string("/x"));
    CHECK_EQ(joinPath("/Music", "x"), std::string("/Music/x"));
}

CASE(normalize_accepts_plain_paths) {
    std::string out;
    CHECK(normalizePath("/Music/a.mp3", out));
    CHECK_EQ(out, std::string("/Music/a.mp3"));
    CHECK(normalizePath("/", out));
    CHECK_EQ(out, std::string("/"));
    CHECK(normalizePath("/a//b/./c", out));
    CHECK_EQ(out, std::string("/a/b/c"));
    CHECK(normalizePath("/a/", out));
    CHECK_EQ(out, std::string("/a"));
}

CASE(normalize_rejects_traversal_and_bad_chars) {
    std::string out;
    CHECK(!normalizePath("", out));
    CHECK(!normalizePath("relative/x", out));
    CHECK(!normalizePath("/a/../b", out));
    CHECK(!normalizePath("/a/..", out));
    CHECK(!normalizePath("/a\\b", out));
    CHECK(!normalizePath(std::string("/a\x01" "b"), out));
    CHECK(!normalizePath("/" + std::string(181, 'x'), out));
}

CASE(child_name_rules) {
    CHECK(validChildName("song.mp3"));
    CHECK(!validChildName(""));
    CHECK(!validChildName("."));
    CHECK(!validChildName(".."));
    CHECK(!validChildName("a/b"));
    CHECK(!validChildName("a\\b"));
    CHECK(!validChildName("a:b"));
    CHECK(!validChildName("a*b"));
    CHECK(!validChildName("a?b"));
    CHECK(!validChildName("a\"b"));
    CHECK(!validChildName("a<b"));
    CHECK(!validChildName("a>b"));
    CHECK(!validChildName("a|b"));
    CHECK(!validChildName(std::string(81, 'x')));
    CHECK(validChildName(std::string(80, 'x')));
}

CASE(system_path_protected_case_insensitive) {
    CHECK(protectedSystemPath("/System Volume Information"));
    CHECK(protectedSystemPath("/system volume information/x"));
    CHECK(!protectedSystemPath("/System Volume Information2"));
    CHECK(!protectedSystemPath("/Music"));
}

CASE(text_extensions_whitelist) {
    CHECK(textFilePath("/a.lrc"));
    CHECK(textFilePath("/a.TXT"));
    CHECK(textFilePath("/a.md"));
    CHECK(textFilePath("/a.json"));
    CHECK(textFilePath("/a.csv"));
    CHECK(textFilePath("/a.log"));
    CHECK(textFilePath("/a.m3u"));
    CHECK(textFilePath("/a.m3u8"));
    CHECK(!textFilePath("/a.mp3"));
    CHECK(!textFilePath("/a.bin"));
}

CASE(content_types) {
    CHECK_EQ(contentTypeFor("/a.mp3"), std::string("audio/mpeg"));
    CHECK_EQ(contentTypeFor("/a.JPG"), std::string("image/jpeg"));
    CHECK_EQ(contentTypeFor("/a.png"), std::string("image/png"));
    CHECK_EQ(contentTypeFor("/a.gif"), std::string("image/gif"));
    CHECK_EQ(contentTypeFor("/a.json"), std::string("application/json"));
    CHECK_EQ(contentTypeFor("/a.csv"), std::string("text/csv; charset=utf-8"));
    CHECK_EQ(contentTypeFor("/a.lrc"), std::string("text/plain; charset=utf-8"));
    CHECK_EQ(contentTypeFor("/a.bin"), std::string("application/octet-stream"));
}

int main() { return test_summary(); }
