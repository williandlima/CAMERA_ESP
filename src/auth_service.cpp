#include "auth_service.h"

#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#include "config.h"
#include "secrets.h"
#include "session.h"

namespace AuthService {

static core::SessionStore s_sessions(SESSION_IDLE_MS, SESSION_MAX_MS);
static core::LoginThrottle s_throttle;
static SemaphoreHandle_t s_mtx;

static void hwRandom(uint8_t *buf, size_t len) { esp_fill_random(buf, len); }  // RNG de hardware

void begin() { s_mtx = xSemaphoreCreateMutex(); }

int32_t login(const char *password, size_t len, char *tokenOut) {
  const uint32_t now = millis();
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  const uint32_t wait = s_throttle.retryAfterMs(now);
  if (wait) {
    xSemaphoreGive(s_mtx);
    return (int32_t)wait;
  }
  const bool ok = core::constTimeEquals(password, len, WEB_PASSWORD, strlen(WEB_PASSWORD));
  if (ok) {
    s_throttle.success();
    s_sessions.create(now, hwRandom, tokenOut);
  } else {
    s_throttle.failure(now);
  }
  xSemaphoreGive(s_mtx);
  return ok ? 0 : -1;
}

bool validateCookieHeader(const char *cookieHeader) {
  char tok[core::SessionStore::kTokenLen + 2];
  if (!core::cookieValue(cookieHeader, kCookie, tok, sizeof(tok))) return false;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  const bool ok = s_sessions.validate(tok, millis());
  xSemaphoreGive(s_mtx);
  return ok;
}

void logout(const char *cookieHeader) {
  char tok[core::SessionStore::kTokenLen + 2];
  if (!core::cookieValue(cookieHeader, kCookie, tok, sizeof(tok))) return;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  s_sessions.revoke(tok);
  xSemaphoreGive(s_mtx);
}

}  // namespace AuthService
