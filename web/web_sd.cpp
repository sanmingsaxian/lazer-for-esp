#include "web_internal.h"
#include "app.h"
#include "board.h"
#include "library.h"
#include "multipart.h"
#include "pathutil.h"
#include "settings.h"
#include "storage.h"
#include "strutil.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <esp_ota_ops.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lz {

namespace {
constexpr size_t MAX_TEXT_FILE = 96 * 1024;
constexpr size_t MAX_LIST_ENTRIES = 300;

struct StorageGuard {
    bool ok;
    StorageGuard() : ok(Audio.storageBegin()) {
        if (!ok) Audio.storageEnd();
    }
    ~StorageGuard() { Audio.storageEnd(); }
};

bool isDirectory(const std::string& vfsPath) {
    struct stat info;
    return stat(vfsPath.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool pathExists(const std::string& vfsPath) {
    struct stat info;
    return stat(vfsPath.c_str(), &info) == 0;
}

bool removeRecursive(const std::string& vfsPath, uint8_t depth) {
    if (depth > 6) return false;
    if (!isDirectory(vfsPath)) return unlink(vfsPath.c_str()) == 0;
    DIR* dir = opendir(vfsPath.c_str());
    if (!dir) return false;
    struct dirent* entry;
    bool ok = true;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const std::string child = vfsPath + "/" + name;
        ok = isDirectory(child) ? removeRecursive(child, depth + 1)
                                : (unlink(child.c_str()) == 0);
        if (!ok) break;
    }
    closedir(dir);
    return ok && rmdir(vfsPath.c_str()) == 0;
}

std::string windowsBaseName(std::string name) {
    const size_t slash = name.rfind('\\');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    return baseName(name);
}
}  // namespace

esp_err_t handleFileList(httpd_req_t* req, WebControl* web) {
    bool hasPath = false;
    std::string input = queryArg(req, "path", &hasPath);
    if (!hasPath) input = "/";
    std::string path;
    if (!normalizePath(input, path)) return sendJson(req, 400, jsonError("目录路径无效"));
    StorageGuard storage;
    if (!storage.ok) return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    if (!Library.ready()) Library.begin();
    const std::string vfsPath = storage_vfs_path(path);
    DIR* directory = opendir(vfsPath.c_str());
    if (!directory) return sendJson(req, 404, jsonError("目录不存在或 SD 卡不可用"));

    struct Entry {
        std::string name;
        std::string path;
        bool directory;
        uint32_t size;
    };
    std::vector<Entry> entries;
    entries.reserve(48);
    bool truncated = false;
    struct dirent* child;
    while ((child = readdir(directory)) != nullptr) {
        const std::string name = child->d_name;
        if (name == "." || name == "..") continue;
        if (entries.size() >= MAX_LIST_ENTRIES) {
            truncated = true;
            break;
        }
        const std::string lower = toLower(name);
        if (name[0] == '.' || endsWith(lower, ".lazer-upload") || endsWith(lower, ".lazer-save") ||
            name == "System Volume Information")
            continue;
        const std::string childVfs = vfsPath + "/" + name;
        struct stat info;
        if (stat(childVfs.c_str(), &info) != 0) continue;
        const bool dir = S_ISDIR(info.st_mode);
        entries.push_back({name, joinPath(path, name), dir, dir ? 0 : (uint32_t)info.st_size});
    }
    closedir(directory);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) return a.directory;
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    web->cacheSDCapacity();

    std::string out;
    out.reserve(4096 + entries.size() * 120);
    out = "{\"ok\":true,\"path\":" + jsonQuote(path);
    out += ",\"parent\":" + jsonQuote(parentPath(path));
    out += ",\"truncated\":" + std::string(truncated ? "true" : "false");
    out += ",\"entries\":[";
    for (size_t i = 0; i < entries.size(); ++i) {
        if (i) out += ',';
        out += "{\"name\":" + jsonQuote(entries[i].name);
        out += ",\"path\":" + jsonQuote(entries[i].path);
        out += ",\"directory\":" + std::string(entries[i].directory ? "true" : "false");
        out += ",\"size\":" + std::to_string(entries[i].size) + "}";
    }
    out += "]}";
    return sendJson(req, 200, out);
}

esp_err_t handleFileRead(httpd_req_t* req, WebControl* web) {
    std::string path;
    if (!normalizePath(queryArg(req, "path"), path) || protectedSystemPath(path) ||
        !textFilePath(path))
        return sendJson(req, 400, jsonError("仅支持读取 LRC、TXT、MD、JSON、CSV、LOG 和播放列表文本"));
    StorageGuard storage;
    if (!storage.ok) return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    const std::string vfsPath = storage_vfs_path(path);
    if (isDirectory(vfsPath)) return sendJson(req, 404, jsonError("文件不存在"));
    FILE* file = fopen(vfsPath.c_str(), "rb");
    if (!file) return sendJson(req, 404, jsonError("文件不存在"));
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size > (long)MAX_TEXT_FILE) {
        fclose(file);
        return sendJson(req, 413, jsonError("文本超过 96 KiB，请下载后编辑"));
    }
    std::string content;
    content.reserve(size > 0 ? size + 1 : 1);
    char buffer[1024];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) content.append(buffer, count);
    fclose(file);
    return sendText(req, 200, "text/plain; charset=utf-8", content);
}

esp_err_t handleFileWrite(httpd_req_t* req, WebControl* web) {
    std::string path;
    if (!normalizePath(queryArg(req, "path"), path) || path == "/" || protectedSystemPath(path) ||
        !textFilePath(path))
        return sendJson(req, 400, jsonError("文本保存路径或扩展名无效"));
    // Frontend posts the editor text as a raw text/plain body.
    const std::string content = readBody(req, MAX_TEXT_FILE + 1);
    if (content.size() > MAX_TEXT_FILE) return sendJson(req, 413, jsonError("文本超过 96 KiB"));
    const std::string parent = parentPath(path);
    const std::string temp = path + ".lazer-save";
    if (temp.size() > 180) return sendJson(req, 400, jsonError("文件路径过长"));
    StorageGuard storage;
    if (!storage.ok) return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    if (!isDirectory(storage_vfs_path(parent)))
        return sendJson(req, 404, jsonError("目标目录不存在"));
    const std::string vfsPath = storage_vfs_path(path);
    const std::string vfsTemp = storage_vfs_path(temp);
    if (isDirectory(vfsPath)) return sendJson(req, 409, jsonError("不能覆盖目录"));
    unlink(vfsTemp.c_str());
    FILE* file = fopen(vfsTemp.c_str(), "wb");
    if (!file) return sendJson(req, 500, jsonError("无法创建临时文件"));
    const size_t written = fwrite(content.data(), 1, content.size(), file);
    fclose(file);
    if (written != content.size()) {
        unlink(vfsTemp.c_str());
        return sendJson(req, 500, jsonError("写入不完整，SD 卡可能已满"));
    }
    if (pathExists(vfsPath) && unlink(vfsPath.c_str()) != 0) {
        unlink(vfsTemp.c_str());
        return sendJson(req, 500, jsonError("无法替换原文件"));
    }
    if (rename(vfsTemp.c_str(), vfsPath.c_str()) != 0) {
        unlink(vfsTemp.c_str());
        return sendJson(req, 500, jsonError("保存失败"));
    }
    web->cacheSDCapacity();
    return sendJson(req, 200, "{\"ok\":true,\"bytes\":" + std::to_string(content.size()) + "}");
}

esp_err_t handleFileDownload(httpd_req_t* req, WebControl* web) {
    std::string path;
    if (!normalizePath(queryArg(req, "path"), path) || path == "/" || protectedSystemPath(path))
        return sendJson(req, 400, jsonError("下载路径无效"));
    StorageGuard storage;
    if (!storage.ok) return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    const std::string vfsPath = storage_vfs_path(path);
    if (isDirectory(vfsPath)) return sendJson(req, 404, jsonError("文件不存在或目录不能直接下载"));
    FILE* file = fopen(vfsPath.c_str(), "rb");
    if (!file) return sendJson(req, 404, jsonError("文件不存在或目录不能直接下载"));
    const std::string name = baseName(path);
    httpd_resp_set_type(req, contentTypeFor(path).c_str());
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const std::string disposition =
        "attachment; filename=\"download\"; filename*=UTF-8''" + urlEncode(name);
    httpd_resp_set_hdr(req, "Content-Disposition", disposition.c_str());
    char buffer[4096];
    size_t count;
    esp_err_t result = ESP_OK;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        result = httpd_resp_send_chunk(req, buffer, (ssize_t)count);
        if (result != ESP_OK) break;
    }
    fclose(file);
    if (result == ESP_OK) result = httpd_resp_send_chunk(req, nullptr, 0);
    return result;
}

esp_err_t handleMkdir(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, 4096));
    std::string parent;
    const std::string name = arg(req, form, "name");
    if (!normalizePath(arg(req, form, "path"), parent) || !validChildName(name))
        return sendJson(req, 400, jsonError("目录名称无效"));
    const std::string target = joinPath(parent, name);
    if (target.size() > 180 || protectedSystemPath(target))
        return sendJson(req, 400, jsonError("不允许创建该目录"));
    StorageGuard storage;
    if (!storage.ok) return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    const std::string vfsTarget = storage_vfs_path(target);
    if (pathExists(vfsTarget)) return sendJson(req, 409, jsonError("同名文件或目录已存在"));
    if (mkdir(vfsTarget.c_str(), 0777) != 0) return sendJson(req, 500, jsonError("创建目录失败"));
    web->refreshLibrary();
    return sendJson(req, 200, "{\"ok\":true,\"path\":" + jsonQuote(target) + "}");
}

esp_err_t handleRename(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, 4096));
    std::string source;
    const std::string name = arg(req, form, "name");
    if (!normalizePath(arg(req, form, "path"), source) || source == "/" ||
        protectedSystemPath(source) || !validChildName(name))
        return sendJson(req, 400, jsonError("原路径或新名称无效"));
    const std::string target = joinPath(parentPath(source), name);
    if (target.size() > 180 || protectedSystemPath(target))
        return sendJson(req, 400, jsonError("目标名称无效"));
    StorageGuard storage;
    if (!storage.ok) return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    const std::string vfsSource = storage_vfs_path(source);
    const std::string vfsTarget = storage_vfs_path(target);
    if (!pathExists(vfsSource)) return sendJson(req, 404, jsonError("原文件不存在"));
    if (pathExists(vfsTarget)) return sendJson(req, 409, jsonError("目标名称已存在"));
    if (rename(vfsSource.c_str(), vfsTarget.c_str()) != 0)
        return sendJson(req, 500, jsonError("重命名失败"));
    web->refreshLibrary();
    return sendJson(req, 200, "{\"ok\":true,\"path\":" + jsonQuote(target) + "}");
}

esp_err_t handleDelete(httpd_req_t* req, WebControl* web) {
    const FormArgs form = FormArgs::parse(readBody(req, 4096));
    std::string path;
    if (!normalizePath(arg(req, form, "path"), path) || path == "/" || protectedSystemPath(path))
        return sendJson(req, 400, jsonError("不能删除该路径"));
    StorageGuard storage;
    if (!storage.ok) return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    const std::string vfsPath = storage_vfs_path(path);
    if (!pathExists(vfsPath)) return sendJson(req, 404, jsonError("文件或目录不存在"));
    const bool directory = isDirectory(vfsPath);
    bool ok;
    if (directory && argBool(arg(req, form, "recursive"))) ok = removeRecursive(vfsPath, 0);
    else ok = directory ? rmdir(vfsPath.c_str()) == 0 : unlink(vfsPath.c_str()) == 0;
    if (!ok)
        return sendJson(req, 500, jsonError(directory ? "目录删除失败；非空目录需确认递归删除"
                                                      : "文件删除失败"));
    web->refreshLibrary();
    return sendJson(req, 200, "{\"ok\":true}");
}

esp_err_t handleSDUpload(httpd_req_t* req, WebControl* web) {
    MultipartReader multipart;
    if (!multipart.begin(req)) return sendJson(req, 400, jsonError("没有收到上传文件"));
    std::string parent;
    std::string name = windowsBaseName(multipart.filename());
    const bool overwrite = argBool(queryArg(req, "overwrite"));
    if (!normalizePath(queryArg(req, "path"), parent) || !validChildName(name))
        return sendJson(req, 400, jsonError("上传目录或文件名无效"));
    const std::string finalPath = joinPath(parent, name);
    const std::string tempPath = finalPath + ".lazer-upload";
    if (tempPath.size() > 180 || protectedSystemPath(finalPath))
        return sendJson(req, 400, jsonError("不允许写入该路径"));
    if (!Audio.storageBegin()) {
        Audio.storageEnd();
        return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    }
    if (!Library.ready()) Library.begin();
    const std::string vfsFinal = storage_vfs_path(finalPath);
    const std::string vfsTemp = storage_vfs_path(tempPath);
    if (!isDirectory(storage_vfs_path(parent))) {
        Audio.storageEnd();
        return sendJson(req, 400, jsonError("目标目录不存在或 SD 卡不可用"));
    }
    if (pathExists(vfsFinal)) {
        if (isDirectory(vfsFinal)) {
            Audio.storageEnd();
            return sendJson(req, 400, jsonError("不能覆盖目录"));
        }
        if (!overwrite) {
            Audio.storageEnd();
            return sendJson(req, 400, jsonError("文件已存在；勾选覆盖后重试"));
        }
    }
    unlink(vfsTemp.c_str());
    FILE* file = fopen(vfsTemp.c_str(), "wb");
    if (!file) {
        Audio.storageEnd();
        return sendJson(req, 500, jsonError("无法创建上传临时文件"));
    }
    uint8_t buffer[2048];
    size_t totalBytes = 0;
    bool writeOk = true;
    int count;
    while ((count = multipart.read(buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, count, file) != (size_t)count) {
            writeOk = false;
            break;
        }
        totalBytes += count;
    }
    fclose(file);
    if (!writeOk || count < 0) {
        unlink(vfsTemp.c_str());
        Audio.storageEnd();
        return sendJson(req, 500, jsonError("上传写入失败，SD 卡可能已满"));
    }
    if (pathExists(vfsFinal) && unlink(vfsFinal.c_str()) != 0) {
        unlink(vfsTemp.c_str());
        Audio.storageEnd();
        return sendJson(req, 500, jsonError("无法替换原文件"));
    }
    if (rename(vfsTemp.c_str(), vfsFinal.c_str()) != 0) {
        unlink(vfsTemp.c_str());
        Audio.storageEnd();
        return sendJson(req, 500, jsonError("上传完成但改名失败"));
    }
    web->refreshLibrary();
    Audio.storageEnd();
    return sendJson(req, 200, "{\"ok\":true,\"path\":" + jsonQuote(finalPath) +
                              ",\"bytes\":" + std::to_string(totalBytes) + "}");
}

esp_err_t handleOTAUpload(httpd_req_t* req, WebControl* web) {
    MultipartReader multipart;
    if (!multipart.begin(req)) return sendJson(req, 400, jsonError("OTA 尚未完成"));
    std::string filename = toLower(multipart.filename());
    if (!endsWith(filename, ".bin") || filename.find("merged") != std::string::npos ||
        filename.find("bootloader") != std::string::npos ||
        filename.find("partition") != std::string::npos ||
        filename.find("otadata") != std::string::npos) {
        return sendJson(req, 400,
                        jsonError("请选择应用 app.bin，不能上传 merged、bootloader、分区表或 otadata"));
    }
    if (!Audio.storageBegin()) {
        Audio.storageEnd();
        return sendJson(req, 503, jsonError("音频任务未停止，请稍后重试"));
    }
    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    esp_ota_handle_t handle = 0;
    if (!partition || esp_ota_begin(partition, OTA_SIZE_UNKNOWN, &handle) != ESP_OK) {
        Audio.storageEnd();
        return sendJson(req, 500, jsonError("无法开始 OTA"));
    }
    uint8_t buffer[2048];
    size_t totalBytes = 0;
    std::string error;
    int count;
    while ((count = multipart.read(buffer, sizeof(buffer))) > 0) {
        if (!totalBytes && buffer[0] != 0xE9) {
            error = "文件不是有效的 ESP32 应用镜像";
            break;
        }
        if (esp_ota_write(handle, buffer, count) != ESP_OK) {
            error = "固件写入失败";
            break;
        }
        totalBytes += count;
    }
    if (count < 0 && error.empty()) error = "OTA 上传已中止";
    if (error.empty()) {
        if (esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(partition) != ESP_OK)
            error = "固件校验失败";
    } else {
        esp_ota_abort(handle);
    }
    if (!error.empty()) {
        Audio.storageEnd();
        return sendJson(req, 400, jsonError(error));
    }
    // Keep the decoder stopped until the delayed reboot (matches original).
    sendJson(req, 200, "{\"ok\":true,\"bytes\":" + std::to_string(totalBytes) + ",\"reboot\":true}");
    web->scheduleReboot(1500);
    return ESP_OK;
}

}  // namespace lz
