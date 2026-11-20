#include "minitest.h"
#include "netease_parse.h"

using namespace lz;

CASE(search_parses_songs_with_fallbacks) {
    const char* json = R"({"code":200,"result":{"songs":[
        {"id":123,"name":"Song A","ar":[{"name":"Art1"},{"name":"Art2"},{"name":"Art3"}],
         "al":{"name":"AlbumA"},"dt":215000},
        {"id":456,"name":"Song B","artists":[{"name":"OldArt"}],
         "album":{"name":"OldAlbum"},"duration":61000},
        {"id":0,"name":"ignored"}
    ]}})";
    std::vector<OnlineTrack> out;
    std::string error;
    CHECK(parseSearchResponse(json, out, error));
    CHECK_EQ(out.size(), 2u);
    CHECK_EQ(out[0].id, 123ull);
    CHECK_EQ(out[0].title, std::string("Song A"));
    CHECK_EQ(out[0].artist, std::string("Art1 / Art2"));  // capped at 2
    CHECK_EQ(out[0].album, std::string("AlbumA"));
    CHECK_EQ(out[0].duration, 215u);
    CHECK_EQ(out[1].artist, std::string("OldArt"));
    CHECK_EQ(out[1].album, std::string("OldAlbum"));
    CHECK_EQ(out[1].duration, 61u);
}

CASE(search_error_code_uses_message) {
    std::vector<OnlineTrack> out;
    std::string error;
    CHECK(!parseSearchResponse(R"({"code":400,"message":"bad"})", out, error));
    CHECK_EQ(error, std::string("bad"));
    CHECK(!parseSearchResponse("not json", out, error));
}

CASE(resolve_accepts_standard_mp3) {
    OnlinePlayback pb;
    std::string error;
    const char* json = R"({"code":200,"data":[{"url":"http://cdn/x.mp3","type":"MP3","br":128000,"size":4000000}]})";
    CHECK(parseResolveResponse(json, pb, error));
    CHECK_EQ(pb.url, std::string("http://cdn/x.mp3"));
    CHECK_EQ(pb.format, std::string("mp3"));
    CHECK_EQ(pb.bitrate, 128000u);
    CHECK_EQ(pb.size, 4000000ull);
}

CASE(resolve_rejects_missing_url_other_formats_and_protocols) {
    OnlinePlayback pb;
    std::string error;
    CHECK(!parseResolveResponse(R"({"code":200,"data":[{"url":null,"type":"mp3"}]})", pb, error));
    CHECK(error.find("版权") != std::string::npos);
    CHECK(!parseResolveResponse(R"({"code":200,"data":[{"url":"http://x/a.flac","type":"flac"}]})", pb, error));
    CHECK(error.find("flac") != std::string::npos);
    CHECK(!parseResolveResponse(R"({"code":200,"data":[{"url":"ftp://x/a.mp3","type":"mp3"}]})", pb, error));
    CHECK(!parseResolveResponse(R"({"code":-460,"msg":"网络环境存在风险"})", pb, error));
    CHECK_EQ(error, std::string("网络环境存在风险"));
}

CASE(qr_check_codes) {
    std::string cookie, nickname, error;
    uint64_t uid = 0;
    CHECK_EQ(parseQrCheckResponse(R"({"code":801})", cookie, nickname, uid, error), 801);
    CHECK_EQ(parseQrCheckResponse(R"({"code":802})", cookie, nickname, uid, error), 802);
    CHECK_EQ(parseQrCheckResponse(R"({"code":800,"message":"expired"})", cookie, nickname, uid, error), 800);
    CHECK_EQ(parseQrCheckResponse(R"({"code":803,"cookie":"MUSIC_U=abc","nickname":"Nick","userId":777})",
                                  cookie, nickname, uid, error), 803);
    CHECK_EQ(cookie, std::string("MUSIC_U=abc"));
    CHECK_EQ(nickname, std::string("Nick"));
    CHECK_EQ(uid, 777ull);
    CHECK_EQ(parseQrCheckResponse(R"({"code":500,"message":"oops"})", cookie, nickname, uid, error), -1);
    CHECK_EQ(error, std::string("oops"));
}

CASE(login_response_contract) {
    std::string cookie, nickname, error;
    uint64_t uid = 0;
    CHECK(parseLoginResponse(R"({"code":200,"cookie":"c=1","profile":{"nickname":"N","userId":42}})",
                             cookie, nickname, uid, error));
    CHECK_EQ(cookie, std::string("c=1"));
    CHECK_EQ(nickname, std::string("N"));
    CHECK_EQ(uid, 42ull);
    CHECK(!parseLoginResponse(R"({"code":400,"message":"密码错误"})", cookie, nickname, uid, error));
    CHECK_EQ(error, std::string("密码错误"));
}

CASE(status_profile_locations) {
    bool has = false;
    std::string nick;
    uint64_t uid = 0;
    CHECK(parseStatusResponse(R"({"data":{"profile":{"nickname":"A","userId":1}}})", has, nick, uid));
    CHECK(has);
    CHECK_EQ(nick, std::string("A"));
    CHECK(parseStatusResponse(R"({"profile":{"nickname":"B","userId":2}})", has, nick, uid));
    CHECK_EQ(nick, std::string("B"));
    CHECK(parseStatusResponse(R"({"data":{"profile":null}})", has, nick, uid));
    CHECK(!has);
}

int main() { return test_summary(); }
