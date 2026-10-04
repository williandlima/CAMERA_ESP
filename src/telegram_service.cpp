#include "telegram_service.h"

#include <Arduino.h>

#include "secrets.h"

#if defined(TG_BOT_TOKEN) && defined(TG_CHAT_ID)
#define TG_ENABLED 1
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>

#include "motion_service.h"
#include "net_service.h"
#include "vpn_service.h"
#ifndef TG_COOLDOWN_S
#define TG_COOLDOWN_S 60  // intervalo mínimo entre fotos de alerta
#endif
#ifndef TG_POLL_S
#define TG_POLL_S 5  // frequência de leitura dos comandos
#endif
#else
#define TG_ENABLED 0
#endif

namespace TelegramService {

#if TG_ENABLED

extern "C" const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

#define TG_API "https://api.telegram.org/bot" TG_BOT_TOKEN "/"

static WiFiClientSecure s_client;
static HTTPClient s_http;
static SemaphoreHandle_t s_mtx;
static FramePtr s_pending;
static float s_pendingPct = 0;
static uint32_t s_lastAlertMs = 0;
static volatile bool s_alertsOn = true;
static int64_t s_offset = 0;

static bool memoryOk() { return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) > 40000; }

static String stamp() {
  struct tm t;
  const time_t now = time(nullptr);
  localtime_r(&now, &t);
  char b[24];
  strftime(b, sizeof(b), "%d/%m %H:%M:%S", &t);
  return String(b);
}

static bool sendMessage(const String &text) {
  if (!memoryOk()) return false;
  JsonDocument d;
  d["chat_id"] = TG_CHAT_ID;
  d["text"] = text;
  String body;
  serializeJson(d, body);
  s_http.begin(s_client, TG_API "sendMessage");
  s_http.addHeader("Content-Type", "application/json");
  const int code = s_http.POST(body);
  s_http.end();
  if (code != 200) Serial.printf("[tg] sendMessage falhou (%d)\n", code);
  return code == 200;
}

static bool sendPhoto(const FramePtr &f, const String &caption) {
  if (!f || !memoryOk()) return false;
  static const char *kBoundary = "----camesp32boundary";
  const String head = String("--") + kBoundary + "\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + TG_CHAT_ID +
                      "\r\n--" + kBoundary + "\r\nContent-Disposition: form-data; name=\"caption\"\r\n\r\n" + caption +
                      "\r\n--" + kBoundary +
                      "\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"foto.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  const String tail = String("\r\n--") + kBoundary + "--\r\n";
  const size_t total = head.length() + f->len + tail.length();
  uint8_t *buf = (uint8_t *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) return false;
  memcpy(buf, head.c_str(), head.length());
  memcpy(buf + head.length(), f->data, f->len);
  memcpy(buf + head.length() + f->len, tail.c_str(), tail.length());
  s_http.begin(s_client, TG_API "sendPhoto");
  s_http.addHeader("Content-Type", String("multipart/form-data; boundary=") + kBoundary);
  const int code = s_http.POST(buf, total);
  s_http.end();
  heap_caps_free(buf);
  if (code != 200) Serial.printf("[tg] sendPhoto falhou (%d)\n", code);
  return code == 200;
}

static String statusText() {
  const MotionService::Status m = MotionService::status();
  String s = "📷 Câmera online\n";
  s += "Alertas: " + String(s_alertsOn ? "ativos" : "pausados") + "\n";
  s += "Movimento agora: " + String(m.motion ? "sim" : "não") + "\n";
  s += "FPS: " + String(FrameHub::fps(), 1) + "\n";
  s += "Wi-Fi: " + String(WiFi.RSSI()) + " dBm\n";
  s += "Ligada há: " + String((unsigned long)(millis() / 60000UL)) + " min\n";
  s += "http://" + WiFi.localIP().toString() + "/";
  if (VpnService::enabled()) s += "\nVPN: " + String(VpnService::up() ? VpnService::ip() : "conectando");
  return s;
}

static void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  const int at = cmd.indexOf('@');  // /foto@MeuBot
  if (at > 0) cmd = cmd.substring(0, at);
  if (cmd == "/foto") {
    if (!sendPhoto(FrameHub::latest(), "Agora " + stamp())) sendMessage("Não consegui capturar a foto agora.");
  } else if (cmd == "/status") {
    sendMessage(statusText());
  } else if (cmd == "/pausar") {
    s_alertsOn = false;
    sendMessage("🔕 Alertas de movimento pausados. Use /ativar para voltar.");
  } else if (cmd == "/ativar") {
    s_alertsOn = true;
    sendMessage("🔔 Alertas de movimento ativos.");
  } else if (cmd == "/start" || cmd == "/ajuda" || cmd == "/help") {
    sendMessage("Comandos:\n/foto - foto agora\n/status - estado da câmera\n/pausar - pausa os alertas\n/ativar - retoma os alertas");
  }
}

// Lê comandos novos. first=true só descarta o histórico (não executa comandos antigos após reiniciar).
static bool poll(bool first) {
  if (!memoryOk()) return false;
  String url = TG_API "getUpdates?timeout=0&limit=5&offset=";
  url += first ? String(-1) : String((long long)s_offset);
  s_http.begin(s_client, url);
  const int code = s_http.GET();
  if (code != 200) {
    s_http.end();
    if (code != 409) Serial.printf("[tg] getUpdates falhou (%d)\n", code);
    return false;
  }
  const String body = s_http.getString();
  s_http.end();

  JsonDocument filter;
  filter["result"][0]["update_id"] = true;
  filter["result"][0]["message"]["chat"]["id"] = true;
  filter["result"][0]["message"]["text"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) return false;

  for (JsonObject u : doc["result"].as<JsonArray>()) {
    s_offset = u["update_id"].as<int64_t>() + 1;
    if (first) continue;
    char chat[24];
    snprintf(chat, sizeof(chat), "%lld", (long long)u["message"]["chat"]["id"].as<int64_t>());
    if (strcmp(chat, TG_CHAT_ID) != 0) continue;  // ignora quem não é o dono
    const char *text = u["message"]["text"] | "";
    if (*text == '/') handleCommand(text);
  }
  return true;
}

static void task(void *) {
  while (!NetService::connected() || time(nullptr) < 1700000000) vTaskDelay(pdMS_TO_TICKS(1000));  // TLS precisa da data certa
  s_client.setCACertBundle(rootca_crt_bundle_start);
  s_http.setReuse(true);
  s_http.setTimeout(12000);

  uint32_t failures = 0, nextPoll = 0;
  bool synced = false, hello = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (!NetService::connected()) continue;
    const uint32_t now = millis();

    if (!synced) {
      synced = poll(true);
      if (!synced) {
        failures++;
        vTaskDelay(pdMS_TO_TICKS(min<uint32_t>(failures * 5000, 60000)));
        continue;
      }
    }
    if (!hello) hello = sendMessage("📷 Câmera online: http://" + WiFi.localIP().toString() + "/\nUse /ajuda para ver os comandos.");

    FramePtr f;
    float pct = 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    f = s_pending;
    pct = s_pendingPct;
    s_pending.reset();
    xSemaphoreGive(s_mtx);
    if (f && s_alertsOn) {
      if (!sendPhoto(f, "🚨 Movimento detectado " + stamp() + " (" + String(pct, 1) + "% da imagem)")) failures++;
      else failures = 0;
    }

    if ((int32_t)(now - nextPoll) >= 0) {
      if (poll(false)) failures = 0;
      else failures++;
      nextPoll = millis() + TG_POLL_S * 1000UL + min<uint32_t>(failures * 5000, 60000);  // recuo se a rede falhar
    }
  }
}

void begin() {
  s_mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "telegram", 12288, nullptr, 1, nullptr, 0);
}

bool enabled() { return true; }

void onMotion(const FramePtr &f, float percent) {
  if (!s_alertsOn) return;
  const uint32_t now = millis();
  if (s_lastAlertMs && now - s_lastAlertMs < TG_COOLDOWN_S * 1000UL) return;
  s_lastAlertMs = now;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_pending = f;
  s_pendingPct = percent;
  xSemaphoreGive(s_mtx);
}

#else
void begin() {}
bool enabled() { return false; }
void onMotion(const FramePtr &, float) {}
#endif

}  // namespace TelegramService
