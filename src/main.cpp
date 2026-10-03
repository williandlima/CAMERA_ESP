#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_camera.h>
#include <img_converters.h>
#include <esp_random.h>
#include <time.h>
#if USE_SD
#include <SD_MMC.h>
#endif
#include "config.h"
#include "secrets.h"
#include "page.h"

// ---------- estado compartilhado ----------
struct Snap { uint8_t *buf = nullptr; size_t len = 0; time_t ts = 0; uint32_t id = 0; };

static Snap snaps[MAX_SNAPSHOTS];
static uint32_t snapCounter = 0;
static uint8_t *liveBuf = nullptr;
static size_t liveLen = 0, liveCap = 0;
static SemaphoreHandle_t mtx;
static volatile bool motionNow = false;
static volatile int motionPct = 0;
static bool sdOk = false;

static String sessions[SESSION_MAX];
static uint8_t sessNext = 0;
static WebServer server(80);

// ---------- câmera ----------
static bool initCamera() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM; c.pin_d1 = Y3_GPIO_NUM; c.pin_d2 = Y4_GPIO_NUM; c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM; c.pin_d5 = Y7_GPIO_NUM; c.pin_d6 = Y8_GPIO_NUM; c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM; c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAMESIZE_VGA;   // 640x480 -> 80x60 com escala 1/8
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  return esp_camera_init(&c) == ESP_OK;
}

// ---------- detecção de movimento ----------
static const int MW = 80, MH = 60;
static uint8_t *prevFrame = nullptr, *curFrame = nullptr;
static int warmup = MOTION_WARMUP_FRAMES;
static uint32_t lastTrigger = 0;

static void saveSnapshot(const uint8_t *jpg, size_t len) {
  time_t now = time(nullptr);
  Snap &s = snaps[snapCounter % MAX_SNAPSHOTS];
  uint8_t *nb = (uint8_t *)ps_malloc(len);
  if (!nb) return;
  memcpy(nb, jpg, len);
  xSemaphoreTake(mtx, portMAX_DELAY);
  free(s.buf);
  s.buf = nb; s.len = len; s.ts = now; s.id = ++snapCounter;
  xSemaphoreGive(mtx);
#if USE_SD
  if (sdOk && now > 100000) {
    struct tm t; localtime_r(&now, &t);
    char path[48];
    strftime(path, sizeof(path), "/mov_%Y%m%d_%H%M%S.jpg", &t);
    File f = SD_MMC.open(path, FILE_WRITE);
    if (f) { f.write(jpg, len); f.close(); }
  }
#endif
  Serial.printf("Movimento! print #%u (%u bytes)\n", (unsigned)s.id, (unsigned)len);
}

static void detectMotion(const uint8_t *jpg, size_t len) {
  if (!jpg2rgb565(jpg, len, curFrame, JPG_SCALE_8X)) return;
  if (warmup > 0) { warmup--; memcpy(prevFrame, curFrame, MW * MH * 2); return; }
  int changed = 0;
  for (int i = 0; i < MW * MH; i++) {
    int d = abs((int)curFrame[2 * i] - prevFrame[2 * i]) + abs((int)curFrame[2 * i + 1] - prevFrame[2 * i + 1]);
    if (d > MOTION_PIXEL_THRESHOLD) changed++;
  }
  memcpy(prevFrame, curFrame, MW * MH * 2);
  motionPct = changed * 100 / (MW * MH);
  motionNow = motionPct >= MOTION_AREA_PERCENT;
  if (motionNow && millis() - lastTrigger > MOTION_COOLDOWN_MS) {
    lastTrigger = millis();
    saveSnapshot(jpg, len);
  }
}

static void captureTask(void *) {
  for (;;) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (fb->len > liveCap) {
      free(liveBuf);
      liveBuf = (uint8_t *)ps_malloc(fb->len + 16384);
      liveCap = liveBuf ? fb->len + 16384 : 0;
    }
    if (liveBuf && fb->len <= liveCap) { memcpy(liveBuf, fb->buf, fb->len); liveLen = fb->len; }
    xSemaphoreGive(mtx);
    detectMotion(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(60));
  }
}

// ---------- autenticação ----------
static String cookieToken() {
  String c = server.header("Cookie");
  int i = c.indexOf("sid=");
  if (i < 0) return "";
  int e = c.indexOf(';', i);
  return c.substring(i + 4, e < 0 ? c.length() : e);
}

static bool authed() {
  String t = cookieToken();
  if (t.length() < 16) return false;
  for (auto &s : sessions) if (s.length() && s == t) return true;
  return false;
}

static bool requireAuth() {
  if (authed()) return true;
  server.send(401, "text/plain", "Nao autorizado");
  return false;
}

static uint32_t loginFails = 0, lockUntil = 0;

static void handleLogin() {
  if (millis() < lockUntil) { server.send(429, "text/plain", "Aguarde e tente novamente"); return; }
  if (server.arg("password") == WEB_PASSWORD) {
    loginFails = 0;
    char tok[33];
    for (int i = 0; i < 4; i++) snprintf(tok + i * 8, 9, "%08x", (unsigned)esp_random());
    sessions[sessNext] = tok;
    sessNext = (sessNext + 1) % SESSION_MAX;
    server.sendHeader("Set-Cookie", String("sid=") + tok + "; Path=/; HttpOnly; SameSite=Strict");
    server.sendHeader("Location", "/");
    server.send(303);
  } else {
    if (++loginFails >= 5) { lockUntil = millis() + 60000; loginFails = 0; }
    delay(1000);
    server.sendHeader("Location", "/?err=1");
    server.send(303);
  }
}

static void handleLogout() {
  String t = cookieToken();
  for (auto &s : sessions) if (s == t) s = "";
  server.sendHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
  server.sendHeader("Location", "/");
  server.send(303);
}

// ---------- rotas ----------
static void sendJpeg(const uint8_t *b, size_t n) {
  server.setContentLength(n);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "image/jpeg", "");
  server.client().write(b, n);
}

static void handleLive() {
  if (!requireAuth()) return;
  xSemaphoreTake(mtx, portMAX_DELAY);
  if (!liveLen) { xSemaphoreGive(mtx); server.send(503, "text/plain", "sem imagem"); return; }
  sendJpeg(liveBuf, liveLen);   // mantém o mutex durante o envio (frame consistente)
  xSemaphoreGive(mtx);
}

static void handleEvents() {
  if (!requireAuth()) return;
  String j = String("{\"motion\":") + (motionNow ? "true" : "false") + ",\"pct\":" + motionPct + ",\"events\":[";
  bool first = true;
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (uint32_t id = snapCounter; id > 0 && id + MAX_SNAPSHOTS > snapCounter; id--) {
    Snap &s = snaps[(id - 1) % MAX_SNAPSHOTS];
    if (s.id != id) continue;
    struct tm t; localtime_r(&s.ts, &t);
    char ts[24]; strftime(ts, sizeof(ts), "%d/%m/%Y %H:%M:%S", &t);
    if (!first) j += ",";
    first = false;
    j += String("{\"id\":") + id + ",\"time\":\"" + ts + "\"}";
  }
  xSemaphoreGive(mtx);
  j += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static void handleEvent() {
  if (!requireAuth()) return;
  uint32_t id = server.arg("id").toInt();
  xSemaphoreTake(mtx, portMAX_DELAY);
  Snap &s = snaps[(id + MAX_SNAPSHOTS - 1) % MAX_SNAPSHOTS];
  if (id == 0 || s.id != id || !s.buf) { xSemaphoreGive(mtx); server.send(404, "text/plain", "nao encontrado"); return; }
  sendJpeg(s.buf, s.len);
  xSemaphoreGive(mtx);
}

static void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", authed() ? PAGE_MAIN : PAGE_LOGIN);
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(115200);
  mtx = xSemaphoreCreateMutex();
  if (!psramFound() || !initCamera()) { Serial.println("Falha na camera/PSRAM"); delay(5000); ESP.restart(); }
  prevFrame = (uint8_t *)ps_malloc(MW * MH * 2);
  curFrame = (uint8_t *)ps_malloc(MW * MH * 2);

#if USE_SD
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
  sdOk = SD_MMC.begin("/sdcard", true);
  Serial.println(sdOk ? "SD ok" : "SD ausente (usando so PSRAM)");
#endif

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print('.'); }
  Serial.printf("\nIP: http://%s/\n", WiFi.localIP().toString().c_str());
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");

  const char *hdrs[] = {"Cookie"};
  server.collectHeaders(hdrs, 1);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/live.jpg", HTTP_GET, handleLive);
  server.on("/events", HTTP_GET, handleEvents);
  server.on("/event.jpg", HTTP_GET, handleEvent);
  server.begin();

  xTaskCreatePinnedToCore(captureTask, "cap", 8192, nullptr, 2, nullptr, 0);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); delay(2000); }
  server.handleClient();
  delay(2);
}
