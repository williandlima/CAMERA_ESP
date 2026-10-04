#include "motion_service.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_jpg_decode.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config.h"
#include "motion.h"
#include "snapshot_store.h"
#include "telegram_service.h"

namespace MotionService {

static core::MotionDetector s_det;
static MotionSettings s_cfg;
static Status s_status;
static SemaphoreHandle_t s_mtx;
static uint8_t *s_gray = nullptr;
static size_t s_grayCap = 0;
static uint32_t s_lastShotMs = 0;
static bool s_wasMotion = false;
static volatile bool s_reconfigure = false;
static MotionSettings s_pending;

// ---- decodificação JPEG -> tons de cinza, já reduzida (1/2..1/8) ----
struct GrayCtx {
  const uint8_t *jpg;
  uint8_t *out;
  size_t cap;
  uint16_t w, h;
  bool overflow;
};

static size_t jpgRead(void *arg, size_t index, uint8_t *buf, size_t len) {
  auto *c = (GrayCtx *)arg;
  if (buf) memcpy(buf, c->jpg + index, len);
  return len;
}

static bool grayWrite(void *arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
  auto *c = (GrayCtx *)arg;
  if (!data) {  // início (x=y=0 traz o tamanho final) ou fim
    if (x == 0 && y == 0) {
      c->w = w;
      c->h = h;
      c->overflow = (size_t)w * h > c->cap;
    }
    return !c->overflow;
  }
  if (c->overflow) return false;
  for (uint16_t iy = 0; iy < h; iy++) {
    const uint32_t oy = y + iy;
    if (oy >= c->h) break;
    uint8_t *o = c->out + oy * c->w + x;
    for (uint16_t ix = 0; ix < w; ix++, data += 3) {
      if (x + ix >= c->w) continue;
      // (R + 2G + B) / 4: independe da ordem RGB/BGR do decodificador
      o[ix] = (uint8_t)((data[0] + 2 * data[1] + data[2]) >> 2);
    }
  }
  return true;
}

static bool decodeGray(const Frame &f, uint16_t &w, uint16_t &h) {
  jpg_scale_t scale = JPG_SCALE_NONE;
  uint16_t sw = f.width;
  while (sw > MOTION_MAX_WIDTH && scale < JPG_SCALE_8X) {
    scale = (jpg_scale_t)(scale + 1);
    sw >>= 1;
  }
  const size_t need = (size_t)(f.width >> scale) * (f.height >> scale) + 64;
  if (need > s_grayCap) {
    heap_caps_free(s_gray);
    s_gray = (uint8_t *)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_grayCap = s_gray ? need : 0;
    if (!s_gray) return false;
  }
  GrayCtx c = {f.data, s_gray, s_grayCap, 0, 0, false};
  if (esp_jpg_decode(f.len, scale, jpgRead, grayWrite, &c) != ESP_OK || c.overflow) return false;
  w = c.w;
  h = c.h;
  return w && h;
}

static void applyParams(const MotionSettings &m) {
  core::MotionParams p;
  p.pixelThreshold = m.pixelThreshold;
  p.minAreaPercent = m.minAreaPercent;
  p.confirmFrames = m.confirmFrames;
  s_det.setParams(p);
  s_cfg = m;
}

void begin() {
  s_mtx = xSemaphoreCreateMutex();
  applyParams(SettingsStore::get().motion);
}

void configure(const MotionSettings &m) {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_pending = m;
  s_reconfigure = true;  // aplicado pela tarefa de captura (sem corrida com o detector)
  xSemaphoreGive(s_mtx);
}

void onFrame(const FramePtr &f) {
  if (s_reconfigure) {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    applyParams(s_pending);
    s_reconfigure = false;
    xSemaphoreGive(s_mtx);
  }
  if (!s_cfg.enabled) {
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_status.motion = false;
    s_status.percent = 0;
    xSemaphoreGive(s_mtx);
    return;
  }
  // Analisa no máximo ~10 vezes por segundo.
  static int64_t lastUs = 0;
  if (f->timeUs - lastUs < 100000) return;
  lastUs = f->timeUs;

  uint16_t w, h;
  if (!decodeGray(*f, w, h)) return;
  const core::MotionResult r = s_det.process(s_gray, w, h);

  xSemaphoreTake(s_mtx, portMAX_DELAY);
  if (r.motion && !s_wasMotion) s_status.events++;
  s_status.motion = r.motion;
  s_status.lightChange = r.lightChange;
  s_status.percent = r.percent;
  if (r.motion) {
    s_status.box[0] = (float)r.x0 / w;
    s_status.box[1] = (float)r.y0 / h;
    s_status.box[2] = (float)(r.x1 + 1) / w;
    s_status.box[3] = (float)(r.y1 + 1) / h;
  }
  xSemaphoreGive(s_mtx);

  s_wasMotion = r.motion;
  if (r.motion) TelegramService::onMotion(f, r.percent);  // alerta no celular (independe de salvar prints)
  const uint32_t now = millis();
  if (r.motion && s_cfg.capture && (s_lastShotMs == 0 || now - s_lastShotMs >= s_cfg.cooldownS * 1000UL)) {
    s_lastShotMs = now;
    SnapshotStore::add(f, r.percent, false);
  }
}

Status status() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  Status s = s_status;
  xSemaphoreGive(s_mtx);
  return s;
}

}  // namespace MotionService
