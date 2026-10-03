#include "net_service.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config.h"
#include "secrets.h"
#include "status_led.h"

namespace NetService {

static volatile bool s_connected = false;
static bool s_servicesUp = false;

static void onEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      s_connected = true;
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      s_connected = false;
      break;
    default:
      break;
  }
}

static void startServices() {
  if (s_servicesUp) return;
  s_servicesUp = true;
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
  }
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(WEB_PASSWORD);
  ArduinoOTA.onStart([] { StatusLed::setMode(StatusLed::Mode::Updating); });
  ArduinoOTA.onError([](ota_error_t) { StatusLed::setMode(StatusLed::Mode::Normal); });
  ArduinoOTA.begin();
}

void begin() {
  WiFi.onEvent(onEvent);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setSleep(false);  // menor latência no stream
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  const uint32_t start = millis();
  while (!s_connected && millis() - start < 20000) {
    StatusLed::update();
    delay(50);
  }
  if (s_connected) {
    StatusLed::setMode(StatusLed::Mode::Normal);
    startServices();
  } else {
    Serial.println("[wifi] sem conexao ainda; tentando em segundo plano");
  }
}

void loop() {
  static uint32_t lastTry = 0, lastLog = 0;
  if (s_connected) {
    startServices();
    ArduinoOTA.handle();
    if (millis() - lastLog > 10000) {  // USB CDC perde o log de boot: repete o IP
      lastLog = millis();
      Serial.printf("[wifi] http://%s/  ou  http://%s.local/  (RSSI %d dBm)\n", WiFi.localIP().toString().c_str(),
                    HOSTNAME, WiFi.RSSI());
    }
  } else if (millis() - lastTry > 15000) {
    lastTry = millis();
    WiFi.reconnect();
  }
  static bool wasConnected = true;
  if (s_connected != wasConnected) {  // só muda o LED na transição (não sobrescreve o modo OTA)
    wasConnected = s_connected;
    StatusLed::setMode(s_connected ? StatusLed::Mode::Normal : StatusLed::Mode::Connecting);
  }
}

bool connected() { return s_connected; }

}  // namespace NetService
