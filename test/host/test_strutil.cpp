#include "minitest.h"
#include "strutil.h"

using namespace lz;

CASE(trim_removes_whitespace_both_ends) {
    CHECK_EQ(trim("  hello \t\r\n"), std::string("hello"));
    CHECK_EQ(trim(""), std::string(""));
    CHECK_EQ(trim("   "), std::string(""));
    CHECK_EQ(trim("a b"), std::string("a b"));
}

CASE(lower_ascii_only) {
    CHECK_EQ(toLower("ABC-Def.MP3"), std::string("abc-def.mp3"));
    CHECK_EQ(toLower("本地"), std::string("本地"));  // UTF-8 bytes untouched
}

CASE(starts_ends) {
    CHECK(startsWith("http://x", "http://"));
    CHECK(!startsWith("https", "http://"));
    CHECK(endsWith("song.MP3", ".MP3"));
    CHECK(!endsWith("mp3", ".mp3"));
    CHECK(endsWith("", ""));
}

CASE(fnv1a_known_vector) {
    // FNV-1a 32-bit reference values.
    CHECK_EQ(fnv1a(""), 2166136261u);
    CHECK_EQ(fnv1a("a"), 0xe40c292cu);
    CHECK_EQ(fnv1a("foobar"), 0xbf9cf968u);
}

CASE(url_encode_rfc3986_unreserved) {
    CHECK_EQ(urlEncode("azAZ09-_.~"), std::string("azAZ09-_.~"));
    CHECK_EQ(urlEncode("a b&c"), std::string("a%20b%26c"));
    CHECK_EQ(urlEncode("中文"), std::string("%E4%B8%AD%E6%96%87"));
}

CASE(json_quote_escapes) {
    CHECK_EQ(jsonQuote("ab"), std::string("\"ab\""));
    CHECK_EQ(jsonQuote("a\"b\\c"), std::string("\"a\\\"b\\\\c\""));
    CHECK_EQ(jsonQuote("a\nb\tc\rd"), std::string("\"a\\nb\\tc\\rd\""));
    CHECK_EQ(jsonQuote(std::string("a\x01" "b")), std::string("\"a\\u0001b\""));
    CHECK_EQ(jsonQuote("中文"), std::string("\"中文\""));  // UTF-8 passes through
}

CASE(url_decode_form_and_percent) {
    CHECK_EQ(urlDecode("a+b%26c"), std::string("a b&c"));
    CHECK_EQ(urlDecode("%E4%B8%AD%E6%96%87"), std::string("中文"));
    CHECK_EQ(urlDecode("plain"), std::string("plain"));
    CHECK_EQ(urlDecode("%zz"), std::string("%zz"));  // invalid escape kept
    CHECK_EQ(urlDecode("%4"), std::string("%4"));    // truncated escape kept
}

CASE(u64_decimal) {
    CHECK_EQ(u64str(0), std::string("0"));
    CHECK_EQ(u64str(18446744073709551615ull), std::string("18446744073709551615"));
}

int main() { return test_summary(); }
