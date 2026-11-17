#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

class WebControl {
public:
    void begin();
    void update();
    void applyNetwork();

    bool enabled() const { return _running; }
    String address() const;
    String connectionLabel() const;

private:
    void installRoutes();
    bool authorize();
    void sendJson(int status, const String& json);
    void sendState();
    void handleRoot();
    void handleState();
    void handleTracks();
    void handleControl();
    void handleSettings();
    void handleNetworkSettings();
    void handleRescan();
    void handleReboot();
    void handleFileList();
    void handleFileRead();
    void handleFileWrite();
    void handleFileDownload();
    void handleMkdir();
    void handleRename();
    void handleDelete();
    void handleSDUpload();
    void handleSDUploadComplete();
    void handleOTAUpload();
    void handleOTAUploadComplete();
    void refreshLibraryLocked();
    void cacheSDCapacityLocked();

    WebServer _server{80};
    bool _running = false;
    String _activeWebPassword;
    uint64_t _sdTotal = 0;
    uint64_t _sdUsed = 0;
    uint32_t _lastReconnect = 0;
    uint32_t _rebootAt = 0;

    File _sdUploadFile;
    bool _sdUploadActive = false;
    bool _sdUploadOverwrite = false;
    String _sdUploadFinal;
    String _sdUploadTemp;
    String _sdUploadError;
    size_t _sdUploadBytes = 0;

    bool _otaActive = false;
    bool _otaFinished = false;
    String _otaError;
    size_t _otaBytes = 0;
};

extern WebControl Web;
