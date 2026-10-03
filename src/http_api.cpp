#include "http_api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_http_server.h>

#include "auth_service.h"
#include "camera_service.h"
#include "config.h"
#include "frame_hub.h"
#include "generated/web_assets.h"
#include "motion_service.h"
#include "settings.h"
#include "snapshot_store.h"
#include "status_led.h"
#include "stream_server.h"

namespace HttpApi {

static const char *kFacesPath = "/faces.json";

// ---------------- utilitários ----------------
static String header(httpd_req_t *req, const char *name) {
  const size_t len = httpd_req_get_hdr_value_len(req, name);
  if (!len || len > 4096) return String();
  String v;
  v.reserve(len + 1);
  char *buf = (char *)malloc(len + 1);
  if (!buf) return String();
  if (httpd_req_get_hdr_value_str(req, name, buf, len + 1) == ESP_OK) v = buf;
  free(buf);
  return v;
}

static bool authed(httpd_req_t *req) { return AuthService::validateCookieHeader(header(req, "Cookie").c_str()); }

static void securityHeaders(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

static esp_err_t sendJson(httpd_req_t *req, const String &body, const char *status = "200 OK") {
  securityHeaders(req);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, body.c_str(), body.length());
}

static esp_err_t sendError(httpd_req_t *req, const char *status, const char *msg) {
  JsonDocument d;
  d["error"] = msg;
  String s;
  serializeJson(d, s);
  return sendJson(req, s, status);
}

#define REQUIRE_AUTH(req) \
  if (!authed(req)) return sendError(req, "401 Unauthorized", "nao autorizado")

// Lê corpo pequeno inteiro (até max-1 bytes) e termina com '\0'.
static bool readBody(httpd_req_t *req, char *buf, size_t max, size_t &len) {
  if (req->content_len >= max) return false;
  len = 0;
  int retries = 3;
  while (len < req->content_len) {
    const int n = httpd_req_recv(req, buf + len, req->content_len - len);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && retries-- > 0) continue;
    if (n <= 0) return false;
    len += (size_t)n;
  }
  buf[len] = 0;
  return true;
}

static uint32_t queryU32(httpd_req_t *req, const char *key) {
  char q[96], v[16];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return 0;
  if (httpd_query_key_value(q, key, v, sizeof(v)) != ESP_OK) return 0;
  return (uint32_t)strtoul(v, nullptr, 10);
}

static void urlDecode(char *s) {
  char *o = s;
  for (; *s; s++) {
    if (*s == '+') *o++ = ' ';
    else if (*s == '%' && isxdigit((uint8_t)s[1]) && isxdigit((uint8_t)s[2])) {
      const char hex[3] = {s[1], s[2], 0};
      *o++ = (char)strtol(hex, nullptr, 16);
      s += 2;
    } else *o++ = *s;
  }
  *o = 0;
}

static esp_err_t sendFrame(httpd_req_t *req, const FramePtr &f, const char *filename) {
  if (!f) return sendError(req, "404 Not Found", "sem imagem");
  securityHeaders(req);
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char disp[64];
  snprintf(disp, sizeof(disp), "inline; filename=\"%s\"", filename);
  httpd_resp_set_hdr(req, "Content-Disposition", disp);
  // Sem trava: o FramePtr mantém o buffer vivo durante o envio.
  return httpd_resp_send(req, (const char *)f->data, f->len);
}

// ---------------- páginas e assets ----------------
static const WebAsset *findAsset(const char *path) {
  for (size_t i = 0; i < WEB_ASSETS_COUNT; i++)
    if (strcmp(WEB_ASSETS[i].path, path) == 0) return &WEB_ASSETS[i];
  return nullptr;
}

static esp_err_t sendAsset(httpd_req_t *req, const WebAsset *a) {
  securityHeaders(req);
  httpd_resp_set_hdr(req, "ETag", a->etag);
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");  // revalida sempre (barato com ETag)
  if (header(req, "If-None-Match") == a->etag) {
    httpd_resp_set_status(req, "304 Not Modified");
    return httpd_resp_send(req, nullptr, 0);
  }
  httpd_resp_set_type(req, a->mime);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)a->data, a->len);
}

static esp_err_t handleRoot(httpd_req_t *req) {
  return sendAsset(req, findAsset(authed(req) ? "/index.html" : "/login.html"));
}

static esp_err_t handleStatic(httpd_req_t *req) {
  char path[64];
  strlcpy(path, req->uri, sizeof(path));
  if (char *q = strchr(path, '?')) *q = 0;
  const WebAsset *a = findAsset(path);
  if (!a || strcmp(path, "/index.html") == 0) {
    securityHeaders(req);
    httpd_resp_set_status(req, "404 Not Found");
    return httpd_resp_sendstr(req, "nao encontrado");
  }
  return sendAsset(req, a);
}

// ---------------- autenticação ----------------
static esp_err_t handleLogin(httpd_req_t *req) {
  char body[192];
  size_t len;
  if (!readBody(req, body, sizeof(body), len)) return sendError(req, "400 Bad Request", "corpo invalido");
  char pass[96] = "";
  if (body[0] == '{') {
    JsonDocument d;
    if (deserializeJson(d, body, len) == DeserializationError::Ok) strlcpy(pass, d["password"] | "", sizeof(pass));
  } else if (httpd_query_key_value(body, "password", pass, sizeof(pass)) == ESP_OK) {
    urlDecode(pass);
  }
  char token[40];
  const int32_t r = AuthService::login(pass, strlen(pass), token);
  memset(pass, 0, sizeof(pass));
  memset(body, 0, sizeof(body));
  if (r > 0) {
    char ra[12];
    snprintf(ra, sizeof(ra), "%lu", (unsigned long)((r + 999) / 1000));
    httpd_resp_set_hdr(req, "Retry-After", ra);
    return sendJson(req, String("{\"error\":\"bloqueado\",\"retryAfter\":") + ra + "}", "429 Too Many Requests");
  }
  if (r < 0) return sendError(req, "401 Unauthorized", "senha incorreta");
  char cookie[128];
  snprintf(cookie, sizeof(cookie), "%s=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%lu", AuthService::kCookie,
           token, (unsigned long)(SESSION_MAX_MS / 1000));
  httpd_resp_set_hdr(req, "Set-Cookie", cookie);
  return sendJson(req, "{\"ok\":true}");
}

static esp_err_t handleLogout(httpd_req_t *req) {
  AuthService::logout(header(req, "Cookie").c_str());
  httpd_resp_set_hdr(req, "Set-Cookie", "sid=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  return sendJson(req, "{\"ok\":true}");
}

// ---------------- API ----------------
static esp_err_t handleStatus(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  const Settings cfg = SettingsStore::get();
  const MotionService::Status m = MotionService::status();
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["sensor"] = CameraService::sensorName();
  d["uptime"] = (uint32_t)(millis() / 1000);
  d["time"] = (uint32_t)time(nullptr);
  d["fps"] = roundf(FrameHub::fps() * 10) / 10;
  d["rssi"] = WiFi.RSSI();
  d["ip"] = WiFi.localIP().toString();
  d["heap"] = ESP.getFreeHeap();
  d["psram"] = ESP.getFreePsram();
  d["temp"] = roundf(temperatureRead() * 10) / 10;
  d["clients"] = StreamServer::clients();
  d["sd"]["ok"] = SnapshotStore::sdReady();
  d["sd"]["used"] = (uint32_t)(SnapshotStore::sdUsedBytes() / (1024 * 1024));
  d["sd"]["total"] = (uint32_t)(SnapshotStore::sdTotalBytes() / (1024 * 1024));
  JsonObject mo = d["motion"].to<JsonObject>();
  mo["enabled"] = cfg.motion.enabled;
  mo["capture"] = cfg.motion.capture;
  mo["active"] = m.motion;
  mo["light"] = m.lightChange;
  mo["percent"] = roundf(m.percent * 10) / 10;
  mo["events"] = m.events;
  JsonArray box = mo["box"].to<JsonArray>();
  for (float v : m.box) box.add(roundf(v * 1000) / 1000);
  d["lastEvent"] = SnapshotStore::lastId();
  String s;
  serializeJson(d, s);
  return sendJson(req, s);
}

static esp_err_t handleEvents(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  SnapshotStore::Info items[MAX_SNAPSHOTS];
  const size_t n = SnapshotStore::list(items, MAX_SNAPSHOTS);
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (size_t i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = items[i].id;
    o["time"] = (uint32_t)items[i].time;
    o["percent"] = roundf(items[i].percent * 10) / 10;
    o["manual"] = items[i].manual;
  }
  String s;
  serializeJson(d, s);
  return sendJson(req, s);
}

static esp_err_t handleSnapshotGet(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  const uint32_t id = queryU32(req, "id");
  char name[32];
  snprintf(name, sizeof(name), "evento_%u.jpg", (unsigned)id);
  return sendFrame(req, SnapshotStore::get(id), name);
}

static esp_err_t handleSnapshotPost(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  FramePtr f = FrameHub::latest();
  if (!f) return sendError(req, "503 Service Unavailable", "sem imagem");
  SnapshotStore::add(f, 0, true);
  return sendJson(req, String("{\"id\":") + SnapshotStore::lastId() + "}");
}

static esp_err_t handleFrame(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  return sendFrame(req, FrameHub::latest(), "agora.jpg");
}

static esp_err_t handleSettingsGet(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  return sendJson(req, SettingsStore::toJson());
}

static esp_err_t handleSettingsPost(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  char body[768];
  size_t len;
  if (!readBody(req, body, sizeof(body), len)) return sendError(req, "400 Bad Request", "corpo invalido");
  String err;
  if (!SettingsStore::update(body, len, err)) return sendError(req, "400 Bad Request", err.c_str());
  const Settings cfg = SettingsStore::get();
  CameraService::apply(cfg.cam);
  MotionService::configure(cfg.motion);
  return sendJson(req, SettingsStore::toJson());
}

static esp_err_t handleFacesGet(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  File f = LittleFS.open(kFacesPath, "r");
  if (!f) return sendJson(req, "{\"v\":2,\"faces\":[]}");
  securityHeaders(req);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char *buf = (char *)malloc(4096);
  if (!buf) {
    f.close();
    return sendError(req, "500 Internal Server Error", "sem memoria");
  }
  size_t n;
  esp_err_t err = ESP_OK;
  while ((n = f.read((uint8_t *)buf, 4096)) > 0 && err == ESP_OK) err = httpd_resp_send_chunk(req, buf, n);
  free(buf);
  f.close();
  if (err == ESP_OK) err = httpd_resp_send_chunk(req, nullptr, 0);
  return err;
}

static esp_err_t handleFacesPost(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  if (req->content_len < 2 || req->content_len > FACES_MAX_BYTES)
    return sendError(req, "413 Payload Too Large", "tamanho invalido");
  File f = LittleFS.open("/faces.tmp", "w");
  if (!f) return sendError(req, "500 Internal Server Error", "falha ao gravar");
  char *buf = (char *)malloc(4096);
  size_t left = req->content_len;
  bool ok = buf != nullptr, first = true;
  int retries = 3;
  while (ok && left) {
    const int n = httpd_req_recv(req, buf, left < 4096 ? left : 4096);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && retries-- > 0) continue;
    if (n <= 0) {
      ok = false;
      break;
    }
    if (first && buf[0] != '{') ok = false;  // formato: {"v":2,"faces":[...]}
    first = false;
    if (ok && f.write((uint8_t *)buf, n) != (size_t)n) ok = false;
    left -= (size_t)n;
  }
  free(buf);
  f.close();
  if (!ok) {
    LittleFS.remove("/faces.tmp");
    return sendError(req, "400 Bad Request", "dados invalidos");
  }
  LittleFS.remove(kFacesPath);  // troca atômica o suficiente: grava tmp e renomeia
  LittleFS.rename("/faces.tmp", kFacesPath);
  return sendJson(req, "{\"ok\":true}");
}

static esp_err_t handleKnown(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  StatusLed::knownFace(SettingsStore::get().knownHoldMs);
  return sendJson(req, "{\"ok\":true}");
}

static void restartTask(void *) {
  vTaskDelay(pdMS_TO_TICKS(800));
  ESP.restart();
}

static esp_err_t handleOta(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  if (req->content_len < 1024) return sendError(req, "400 Bad Request", "arquivo invalido");
  if (!Update.begin(req->content_len)) return sendError(req, "500 Internal Server Error", Update.errorString());
  StatusLed::setMode(StatusLed::Mode::Updating);
  Serial.printf("[ota] recebendo %u bytes\n", (unsigned)req->content_len);
  uint8_t *buf = (uint8_t *)malloc(4096);
  size_t left = req->content_len;
  bool ok = buf != nullptr;
  int retries = 5;
  while (ok && left) {
    const int n = httpd_req_recv(req, (char *)buf, left < 4096 ? left : 4096);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && retries-- > 0) continue;
    if (n <= 0 || Update.write(buf, n) != (size_t)n) ok = false;
    else left -= (size_t)n;
  }
  free(buf);
  if (!ok || !Update.end(true)) {
    Update.abort();
    StatusLed::setMode(StatusLed::Mode::Normal);
    return sendError(req, "500 Internal Server Error", Update.hasError() ? Update.errorString() : "falha no envio");
  }
  Serial.println("[ota] ok, reiniciando");
  sendJson(req, "{\"ok\":true}");
  xTaskCreate(restartTask, "restart", 2048, nullptr, 1, nullptr);
  return ESP_OK;
}

static esp_err_t handleReboot(httpd_req_t *req) {
  REQUIRE_AUTH(req);
  sendJson(req, "{\"ok\":true}");
  xTaskCreate(restartTask, "restart", 2048, nullptr, 1, nullptr);
  return ESP_OK;
}

// ---------------- registro ----------------
static void route(httpd_handle_t h, const char *uri, httpd_method_t m, esp_err_t (*fn)(httpd_req_t *)) {
  httpd_uri_t u = {};
  u.uri = uri;
  u.method = m;
  u.handler = fn;
  if (httpd_register_uri_handler(h, &u) != ESP_OK) Serial.printf("[http] falha ao registrar %s\n", uri);
}

void begin() {
  LittleFS.begin(true);
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = HTTP_PORT;
  cfg.ctrl_port = 32768;
  cfg.max_uri_handlers = 20;
  cfg.max_open_sockets = 5;
  cfg.lru_purge_enable = true;
  cfg.stack_size = 10240;
  cfg.recv_wait_timeout = 10;
  cfg.send_wait_timeout = 10;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  httpd_handle_t h = nullptr;
  if (httpd_start(&h, &cfg) != ESP_OK) {
    Serial.println("[http] falha ao iniciar");
    return;
  }
  route(h, "/", HTTP_GET, handleRoot);
  route(h, "/api/login", HTTP_POST, handleLogin);
  route(h, "/api/logout", HTTP_POST, handleLogout);
  route(h, "/api/status", HTTP_GET, handleStatus);
  route(h, "/api/events", HTTP_GET, handleEvents);
  route(h, "/api/snapshot", HTTP_GET, handleSnapshotGet);
  route(h, "/api/snapshot", HTTP_POST, handleSnapshotPost);
  route(h, "/api/frame.jpg", HTTP_GET, handleFrame);
  route(h, "/api/settings", HTTP_GET, handleSettingsGet);
  route(h, "/api/settings", HTTP_POST, handleSettingsPost);
  route(h, "/api/faces", HTTP_GET, handleFacesGet);
  route(h, "/api/faces", HTTP_POST, handleFacesPost);
  route(h, "/api/known", HTTP_POST, handleKnown);
  route(h, "/api/ota", HTTP_POST, handleOta);
  route(h, "/api/reboot", HTTP_POST, handleReboot);
  route(h, "/*", HTTP_GET, handleStatic);  // por último: assets estáticos
}

}  // namespace HttpApi
