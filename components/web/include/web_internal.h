#pragma once
#include <string>
#include <utility>
#include <vector>

#include <esp_http_server.h>

#include "audio_engine.h"
#include "web.h"

namespace lz {

struct FormArgs {
    std::vector<std::pair<std::string, std::string>> items;
    bool has(const std::string& key) const;
    std::string get(const std::string& key) const;
    static FormArgs parse(const std::string& body);
};

std::string queryArg(httpd_req_t* req, const char* key, bool* present = nullptr);
std::string readBody(httpd_req_t* req, size_t cap);
// Query string first, then form body (matches Arduino WebServer.arg()).
std::string arg(httpd_req_t* req, const FormArgs& form, const char* key);
bool argBool(const std::string& value);

bool authorize(WebControl* web, httpd_req_t* req);
esp_err_t sendJson(httpd_req_t* req, int status, const std::string& json);
esp_err_t sendText(httpd_req_t* req, int status, const char* type, const std::string& body);
std::string jsonError(const std::string& message);
const char* audioStateName(AudioState state);

// Handlers implemented in web_sd.cpp.
esp_err_t handleFileList(httpd_req_t* req, WebControl* web);
esp_err_t handleFileRead(httpd_req_t* req, WebControl* web);
esp_err_t handleFileWrite(httpd_req_t* req, WebControl* web);
esp_err_t handleFileDownload(httpd_req_t* req, WebControl* web);
esp_err_t handleMkdir(httpd_req_t* req, WebControl* web);
esp_err_t handleRename(httpd_req_t* req, WebControl* web);
esp_err_t handleDelete(httpd_req_t* req, WebControl* web);
esp_err_t handleSDUpload(httpd_req_t* req, WebControl* web);
esp_err_t handleOTAUpload(httpd_req_t* req, WebControl* web);

}  // namespace lz
