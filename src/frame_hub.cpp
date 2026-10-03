#include "frame_hub.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

Frame::~Frame() {
  if (data) heap_caps_free(data);
}

namespace FrameHub {

static SemaphoreHandle_t s_mtx;
static FramePtr s_latest;
static uint32_t s_seq = 0;
static volatile uint32_t s_latestSeq = 0;
static float s_fps = 0;
static int64_t s_lastUs = 0;

void begin() { s_mtx = xSemaphoreCreateMutex(); }

FramePtr make(const uint8_t *jpg, size_t len, uint16_t w, uint16_t h) {
  auto *f = new (std::nothrow) Frame();
  if (!f) return nullptr;
  f->data = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!f->data) {
    delete f;
    return nullptr;
  }
  memcpy(f->data, jpg, len);
  f->len = len;
  f->width = w;
  f->height = h;
  f->timeUs = esp_timer_get_time();
  return FramePtr(f);
}

void publish(FramePtr f) {
  if (!f) return;
  const int64_t now = f->timeUs;
  if (s_lastUs) {
    const float inst = 1e6f / (float)(now - s_lastUs + 1);
    s_fps = s_fps ? s_fps * 0.9f + inst * 0.1f : inst;
  }
  s_lastUs = now;
  FramePtr old;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  const_cast<Frame *>(f.get())->seq = ++s_seq;
  old = std::move(s_latest);
  s_latest = std::move(f);
  s_latestSeq = s_seq;
  xSemaphoreGive(s_mtx);
  // `old` é liberado aqui, fora da trava
}

FramePtr latest() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  FramePtr f = s_latest;
  xSemaphoreGive(s_mtx);
  return f;
}

FramePtr waitNewer(uint32_t lastSeq, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (s_latestSeq == lastSeq) {
    if (millis() - start >= timeoutMs) return nullptr;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return latest();
}

float fps() {
  // Sem quadros há mais de 2 s: considera parado.
  return (esp_timer_get_time() - s_lastUs) > 2000000 ? 0.0f : s_fps;
}

}  // namespace FrameHub
