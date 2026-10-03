// Configurações ajustáveis pela página, persistidas na NVS.
#pragma once
#include <Arduino.h>
#include <stdint.h>

struct CameraSettings {
  uint8_t framesize = 8;   // framesize_t (8 = VGA 640x480)
  uint8_t quality = 12;    // JPEG 4-63 (menor = melhor)
  int8_t brightness = 0;   // -2..2
  int8_t contrast = 0;     // -2..2
  int8_t saturation = 0;   // -2..2
  int8_t aeLevel = 0;      // -2..2
  bool vflip = false;
  bool hmirror = false;
  bool awb = true;         // balanço de branco automático
};

struct MotionSettings {
  bool enabled = true;          // detecção ligada
  bool capture = true;          // salvar print ao detectar
  uint8_t pixelThreshold = 22;  // sensibilidade por pixel (menor = mais sensível)
  float minAreaPercent = 1.0f;  // % mínima da imagem
  uint8_t confirmFrames = 2;
  uint16_t cooldownS = 5;       // intervalo mínimo entre prints
};

struct Settings {
  uint32_t version = 3;
  CameraSettings cam;
  MotionSettings motion;
  uint8_t ledBrightness = 25;
  uint16_t knownHoldMs = 5000;
};

namespace SettingsStore {
void begin();
Settings get();  // cópia thread-safe
// Aplica JSON parcial; retorna false com mensagem em `err`.
bool update(const char *json, size_t len, String &err);
String toJson();
}  // namespace SettingsStore
