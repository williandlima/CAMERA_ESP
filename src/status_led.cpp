#include "status_led.h"

#include <Arduino.h>

#include "config.h"
#include "settings.h"

namespace StatusLed {

static volatile Mode s_mode = Mode::Connecting;
static volatile uint32_t s_knownUntil = 0;
static uint32_t s_lastColor = 0xFFFFFFFF;

void begin() { neopixelWrite(LED_PIN, 0, 0, 0); }
void setMode(Mode m) { s_mode = m; }
void knownFace(uint16_t holdMs) { s_knownUntil = millis() + holdMs; }

static void show(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t c = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  if (c == s_lastColor) return;  // evita reescrever o WS2812 à toa
  s_lastColor = c;
  neopixelWrite(LED_PIN, r, g, b);
}

void update() {
  const uint8_t v = SettingsStore::get().ledBrightness;
  const uint32_t now = millis();
  switch (s_mode) {
    case Mode::Connecting:
      show(0, 0, (now / 400) % 2 ? v : 0);
      break;
    case Mode::Updating:
      show(v, 0, v);
      break;
    default:
      if ((int32_t)(s_knownUntil - now) > 0) show(0, v, 0);
      else show(v, 0, 0);
      break;
  }
}

}  // namespace StatusLed
