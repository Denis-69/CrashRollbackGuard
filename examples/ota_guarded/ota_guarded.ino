#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <algorithm>
#include <CrashRollbackGuard.h>

// Replace with your network credentials and OTA URL
static constexpr const char* WIFI_SSID = "YOUR_WIFI";
static constexpr const char* WIFI_PASS = "YOUR_PASS";
static constexpr const char* OTA_URL   = "https://example.com/firmware.bin";

crg::CrashRollbackGuard guard;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    if (millis() - start > 15000) {
      Serial.println("[OTA] WiFi connect timeout");
      return;
    }
  }
  Serial.print("[OTA] WiFi OK, IP: ");
  Serial.println(WiFi.localIP());
}

bool downloadAndUpdate() {
  HTTPClient http;
  http.begin(OTA_URL);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] HTTP error: %d\n", code);
    http.end();
    return false;
  }

  const int total = http.getSize();
  int remaining = total;
  WiFiClient* stream = http.getStreamPtr();

  const size_t updateSize = (total > 0)
                              ? static_cast<size_t>(total)
                              : static_cast<size_t>(UPDATE_SIZE_UNKNOWN);

  if (!Update.begin(updateSize)) {
    Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString().c_str());
    http.end();
    return false;
  }

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    const size_t available = stream->available();
    if (available) {
      uint8_t buf[512];
      const size_t chunk = std::min(sizeof(buf), available);
      const size_t read = stream->readBytes(buf, chunk);
      if (Update.write(buf, read) != read) {
        Serial.printf("[OTA] Update.write failed: %s\n", Update.errorString().c_str());
        http.end();
        return false;
      }
      if (remaining > 0) {
        remaining -= static_cast<int>(read);
      }
    }
    delay(1);
  }

  if (!Update.end()) {
    Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString().c_str());
    http.end();
    return false;
  }

  Serial.printf("[OTA] Update success, size=%u bytes\n", Update.size());
  http.end();
  return true;
}

void setup() {
  Serial.begin(115200);

  crg::Options opt;
  opt.failLimit             = 3;
  opt.stableTimeMs          = 60000;
  opt.autoSavePrevSlot      = false;
  opt.fallbackToFactory     = true;
  opt.factoryLabel          = "factory";
  opt.maxRollbackAttempts   = 1;
  guard.setOptions(opt);

  guard.beginEarly();

  connectWiFi();

  // BEFORE starting OTA write
  guard.saveCurrentAsPreviousSlot();

  if (downloadAndUpdate()) {
    guard.armControlledRestart();
    Serial.println("[OTA] Rebooting into new firmware");
    ESP.restart();
  }
}

void loop() {
  guard.loopTick();
}
