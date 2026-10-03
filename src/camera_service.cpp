#include "camera_service.h"

#include <Arduino.h>
#include <esp_camera.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "frame_hub.h"
#include "motion_service.h"

namespace CameraService {

static const char *s_sensor = "?";

bool begin(const CameraSettings &cfg) {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = CAM_XCLK_HZ;
  c.pixel_format = PIXFORMAT_JPEG;
  // Buffers dimensionados para a resolução máxima: permite trocar a resolução
  // pela página sem reiniciar a câmera.
  c.frame_size = FRAMESIZE_UXGA;
  c.jpeg_quality = cfg.quality;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  if (esp_camera_init(&c) != ESP_OK) return false;

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    switch (s->id.PID) {
      case OV2640_PID: s_sensor = "OV2640"; break;
      case OV3660_PID: s_sensor = "OV3660"; break;
      case OV5640_PID: s_sensor = "OV5640"; break;
      default: s_sensor = "desconhecido"; break;
    }
  }
  apply(cfg);
  return true;
}

void apply(const CameraSettings &cfg) {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;
  s->set_framesize(s, (framesize_t)cfg.framesize);
  s->set_quality(s, cfg.quality);
  s->set_brightness(s, cfg.brightness);
  s->set_contrast(s, cfg.contrast);
  s->set_saturation(s, cfg.saturation);
  s->set_ae_level(s, cfg.aeLevel);
  s->set_vflip(s, cfg.vflip);
  s->set_hmirror(s, cfg.hmirror);
  s->set_whitebal(s, cfg.awb);
  s->set_awb_gain(s, cfg.awb);
}

static void captureTask(void *) {
  esp_task_wdt_add(nullptr);  // reinicia a placa se a câmera travar
  uint32_t failures = 0;
  for (;;) {
    esp_task_wdt_reset();
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      if (++failures > 50) {
        Serial.println("[cam] sem quadros, reiniciando");
        delay(100);
        ESP.restart();
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    failures = 0;
    FramePtr f = FrameHub::make(fb->buf, fb->len, fb->width, fb->height);
    esp_camera_fb_return(fb);  // devolve o buffer ao driver o quanto antes
    if (!f) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    FrameHub::publish(f);
    MotionService::onFrame(f);
    vTaskDelay(1);
  }
}

void startTask() {
  // Core 1: o core 0 fica com Wi-Fi/lwIP.
  xTaskCreatePinnedToCore(captureTask, "capture", 8192, nullptr, 5, nullptr, 1);
}

const char *sensorName() { return s_sensor; }

}  // namespace CameraService
