// Monitor de ambiente – GOOUUU ESP32-S3-CAM
//
// Tarefas:
//   capture (core 1)   câmera -> FrameHub -> detector de movimento -> prints
//   httpd   (core 0/1) página, API REST, OTA            (porta 80)
//   stream  (core 0)   MJPEG, 1 tarefa por espectador    (porta 81)
//   sd      (core 0)   gravação assíncrona no cartão
//   loop               Wi-Fi, OTA Arduino, LED
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "auth_service.h"
#include "camera_service.h"
#include "config.h"
#include "frame_hub.h"
#include "http_api.h"
#include "motion_service.h"
#include "net_service.h"
#include "telegram_service.h"
#include "vpn_service.h"
#include "secrets.h"
#include "settings.h"
#include "snapshot_store.h"
#include "status_led.h"
#include "stream_server.h"

static_assert(sizeof(WEB_PASSWORD) > 8, "WEB_PASSWORD precisa de pelo menos 8 caracteres");

void setup() {
  Serial.begin(115200);
  StatusLed::begin();
  Serial.printf("\n[boot] firmware %s\n", FW_VERSION);

  if (!psramFound()) {
    Serial.println("[boot] PSRAM nao encontrada: confira memory_type no platformio.ini");
    delay(5000);
    ESP.restart();
  }
  SettingsStore::begin();
  AuthService::begin();
  FrameHub::begin();
  MotionService::begin();
  SnapshotStore::begin();

  if (!CameraService::begin(SettingsStore::get().cam)) {
    Serial.println("[boot] falha na camera: confira o cabo flat e a pinagem");
    delay(5000);
    ESP.restart();
  }
  Serial.printf("[boot] camera %s ok\n", CameraService::sensorName());
  CameraService::startTask();

  NetService::begin();
  VpnService::begin();
  TelegramService::begin();
  HttpApi::begin();
  StreamServer::begin();
  enableLoopWDT();
}

void loop() {
  NetService::loop();
  StatusLed::update();
  delay(10);
}
