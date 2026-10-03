#include "settings.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace SettingsStore {

static Settings s_cfg;
static Preferences s_prefs;
static SemaphoreHandle_t s_mtx;

static void save() {
  s_prefs.putBytes("cfg", &s_cfg, sizeof(s_cfg));
}

void begin() {
  s_mtx = xSemaphoreCreateMutex();
  s_prefs.begin("cam", false);
  Settings stored;
  const size_t n = s_prefs.getBytes("cfg", &stored, sizeof(stored));
  if (n == sizeof(stored) && stored.version == Settings().version) s_cfg = stored;
  // migração da v1: opção "capture" salva isoladamente
  if (n == 0 && s_prefs.isKey("capture")) s_cfg.motion.capture = s_prefs.getBool("capture", true);
}

Settings get() {
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  Settings c = s_cfg;
  xSemaphoreGive(s_mtx);
  return c;
}

template <typename T>
static void clampSet(JsonVariantConst v, T &dst, long lo, long hi) {
  if (v.is<long>()) {
    long x = v.as<long>();
    dst = (T)(x < lo ? lo : x > hi ? hi : x);
  }
}

static void boolSet(JsonVariantConst v, bool &dst) {
  if (v.is<bool>()) dst = v.as<bool>();
}

bool update(const char *json, size_t len, String &err) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, json, len);
  if (e) {
    err = e.c_str();
    return false;
  }
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  Settings c = s_cfg;
  JsonVariantConst cam = doc["cam"];
  clampSet(cam["framesize"], c.cam.framesize, 1, 13);
  clampSet(cam["quality"], c.cam.quality, 4, 63);
  clampSet(cam["brightness"], c.cam.brightness, -2, 2);
  clampSet(cam["contrast"], c.cam.contrast, -2, 2);
  clampSet(cam["saturation"], c.cam.saturation, -2, 2);
  clampSet(cam["aeLevel"], c.cam.aeLevel, -2, 2);
  boolSet(cam["vflip"], c.cam.vflip);
  boolSet(cam["hmirror"], c.cam.hmirror);
  boolSet(cam["awb"], c.cam.awb);
  JsonVariantConst m = doc["motion"];
  boolSet(m["enabled"], c.motion.enabled);
  boolSet(m["capture"], c.motion.capture);
  clampSet(m["pixelThreshold"], c.motion.pixelThreshold, 5, 120);
  if (m["minAreaPercent"].is<float>()) {
    float a = m["minAreaPercent"].as<float>();
    c.motion.minAreaPercent = a < 0.1f ? 0.1f : a > 50.0f ? 50.0f : a;
  }
  clampSet(m["confirmFrames"], c.motion.confirmFrames, 1, 10);
  clampSet(m["cooldownS"], c.motion.cooldownS, 1, 3600);
  clampSet(doc["ledBrightness"], c.ledBrightness, 0, 255);
  clampSet(doc["knownHoldMs"], c.knownHoldMs, 500, 60000);
  s_cfg = c;
  save();
  xSemaphoreGive(s_mtx);
  return true;
}

String toJson() {
  const Settings c = get();
  JsonDocument doc;
  doc["cam"]["framesize"] = c.cam.framesize;
  doc["cam"]["quality"] = c.cam.quality;
  doc["cam"]["brightness"] = c.cam.brightness;
  doc["cam"]["contrast"] = c.cam.contrast;
  doc["cam"]["saturation"] = c.cam.saturation;
  doc["cam"]["aeLevel"] = c.cam.aeLevel;
  doc["cam"]["vflip"] = c.cam.vflip;
  doc["cam"]["hmirror"] = c.cam.hmirror;
  doc["cam"]["awb"] = c.cam.awb;
  doc["motion"]["enabled"] = c.motion.enabled;
  doc["motion"]["capture"] = c.motion.capture;
  doc["motion"]["pixelThreshold"] = c.motion.pixelThreshold;
  doc["motion"]["minAreaPercent"] = c.motion.minAreaPercent;
  doc["motion"]["confirmFrames"] = c.motion.confirmFrames;
  doc["motion"]["cooldownS"] = c.motion.cooldownS;
  doc["ledBrightness"] = c.ledBrightness;
  doc["knownHoldMs"] = c.knownHoldMs;
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace SettingsStore
