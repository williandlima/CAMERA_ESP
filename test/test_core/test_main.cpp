// Testes nativos da lógica pura:  pio test -e native
#include <string.h>
#include <unity.h>

#include <vector>

#include "motion.h"
#include "session.h"

using namespace core;

static const int W = 80, H = 60;

static std::vector<uint8_t> scene(uint8_t base, int seed = 0) {
  std::vector<uint8_t> img(W * H);
  for (int i = 0; i < W * H; i++) img[i] = (uint8_t)(base + ((i * 7 + seed) % 5));  // textura leve
  return img;
}

static void drawBox(std::vector<uint8_t> &img, int x, int y, int bw, int bh, uint8_t v) {
  for (int j = y; j < y + bh; j++)
    for (int i = x; i < x + bw; i++) img[j * W + i] = v;
}

static MotionResult feed(MotionDetector &d, const std::vector<uint8_t> &img, int times) {
  MotionResult r;
  for (int k = 0; k < times; k++) r = d.process(img.data(), W, H);
  return r;
}

void test_static_scene_has_no_motion() {
  MotionDetector d;
  auto r = feed(d, scene(100), 30);
  TEST_ASSERT_FALSE(r.motion);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.percent);
}

void test_sensor_noise_is_ignored() {
  MotionDetector d;
  feed(d, scene(100), 10);
  MotionResult r;
  for (int k = 0; k < 20; k++) {
    auto img = scene(100, k);  // ruído de ±4 níveis variando a cada quadro
    r = d.process(img.data(), W, H);
    TEST_ASSERT_FALSE(r.motion);
  }
}

void test_moving_object_detected_with_box() {
  MotionDetector d;
  feed(d, scene(100), 10);
  auto img = scene(100);
  drawBox(img, 20, 10, 12, 16, 220);
  auto r1 = d.process(img.data(), W, H);
  TEST_ASSERT_TRUE(r1.raw);
  TEST_ASSERT_FALSE(r1.motion);  // precisa de 2 quadros para confirmar
  auto r2 = d.process(img.data(), W, H);
  TEST_ASSERT_TRUE(r2.motion);
  TEST_ASSERT_INT_WITHIN(1, 20, r2.x0);
  TEST_ASSERT_INT_WITHIN(1, 31, r2.x1);
  TEST_ASSERT_INT_WITHIN(1, 10, r2.y0);
  TEST_ASSERT_INT_WITHIN(1, 25, r2.y1);
  TEST_ASSERT_TRUE(r2.percent > 3.0f);
}

void test_global_brightness_drift_is_compensated() {
  MotionDetector d;
  feed(d, scene(100), 10);
  auto r = feed(d, scene(130), 1);  // auto-exposição: tudo +30
  TEST_ASSERT_FALSE(r.raw);
  TEST_ASSERT_FALSE(r.motion);
}

void test_light_switch_is_not_motion() {
  MotionDetector d;
  auto dark = scene(40);
  feed(d, dark, 10);
  // Luz acesa só em parte da imagem (não uniforme): grande área muda de uma vez.
  auto lit = dark;
  for (int i = 0; i < W * H * 3 / 4; i++) lit[i] = 200;
  auto r = d.process(lit.data(), W, H);
  TEST_ASSERT_TRUE(r.lightChange);
  TEST_ASSERT_FALSE(r.motion);
  r = d.process(lit.data(), W, H);  // fundo reaprendido
  TEST_ASSERT_FALSE(r.raw);
}

void test_warmup_suppresses_detection() {
  MotionDetector d;
  auto img = scene(100);
  d.process(img.data(), W, H);
  drawBox(img, 0, 0, 30, 30, 250);
  auto r = d.process(img.data(), W, H);
  TEST_ASSERT_FALSE(r.raw);
}

void test_resolution_change_resets() {
  MotionDetector d;
  feed(d, scene(100), 10);
  std::vector<uint8_t> small(40 * 30, 100);
  auto r = d.process(small.data(), 40, 30);
  TEST_ASSERT_FALSE(r.motion);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r.percent);
}

void test_null_input_is_safe() {
  MotionDetector d;
  auto r = d.process(nullptr, W, H);
  TEST_ASSERT_FALSE(r.motion);
}

// ---------------- sessões ----------------
static uint8_t g_seed = 1;
static void fakeRandom(uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(g_seed * 31 + i * 17);
  g_seed++;
}

void test_const_time_equals() {
  TEST_ASSERT_TRUE(constTimeEquals("abc", 3, "abc", 3));
  TEST_ASSERT_FALSE(constTimeEquals("abd", 3, "abc", 3));
  TEST_ASSERT_FALSE(constTimeEquals("ab", 2, "abc", 3));
  TEST_ASSERT_FALSE(constTimeEquals("abcd", 4, "abc", 3));
  TEST_ASSERT_TRUE(constTimeEquals("", 0, "", 0));
}

void test_cookie_parsing() {
  char v[40];
  TEST_ASSERT_TRUE(cookieValue("a=1; sid=xyz; b=2", "sid", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("xyz", v);
  TEST_ASSERT_TRUE(cookieValue("sid=abc", "sid", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("abc", v);
  TEST_ASSERT_FALSE(cookieValue("xsid=abc", "sid", v, sizeof v));
  TEST_ASSERT_FALSE(cookieValue("a=1", "sid", v, sizeof v));
  TEST_ASSERT_FALSE(cookieValue("sid=", "sid", v, sizeof v));
  char tiny[3];
  TEST_ASSERT_FALSE(cookieValue("sid=abcdef", "sid", tiny, sizeof tiny));
  TEST_ASSERT_FALSE(cookieValue(nullptr, "sid", v, sizeof v));
}

void test_session_lifecycle() {
  SessionStore s(1000, 5000);
  char t[SessionStore::kTokenLen + 1];
  s.create(0, fakeRandom, t);
  TEST_ASSERT_EQUAL(SessionStore::kTokenLen, strlen(t));
  TEST_ASSERT_TRUE(s.validate(t, 500));
  TEST_ASSERT_TRUE(s.validate(t, 1400));   // atividade renovada em 500
  TEST_ASSERT_FALSE(s.validate(t, 2500));  // ociosa > 1000
  s.create(3000, fakeRandom, t);
  for (uint32_t now = 3500; now < 8000; now += 500) {
    if (now - 3000 > 5000) TEST_ASSERT_FALSE(s.validate(t, now));
    else TEST_ASSERT_TRUE(s.validate(t, now));
  }
  TEST_ASSERT_FALSE(s.validate(t, 8500));  // idade máxima
}

void test_session_revoke_and_bad_tokens() {
  SessionStore s(100000, 100000);
  char a[33], b[33];
  s.create(0, fakeRandom, a);
  s.create(0, fakeRandom, b);
  TEST_ASSERT_TRUE(strcmp(a, b) != 0);
  s.revoke(a);
  TEST_ASSERT_FALSE(s.validate(a, 1));
  TEST_ASSERT_TRUE(s.validate(b, 1));
  TEST_ASSERT_FALSE(s.validate("", 1));
  TEST_ASSERT_FALSE(s.validate("short", 1));
  TEST_ASSERT_FALSE(s.validate(nullptr, 1));
  s.revokeAll();
  TEST_ASSERT_FALSE(s.validate(b, 1));
  TEST_ASSERT_EQUAL(0, s.active(1));
}

void test_session_eviction_when_full() {
  SessionStore s(100000, 100000);
  char first[33], t[33];
  s.create(0, fakeRandom, first);
  for (uint32_t i = 1; i <= SessionStore::kMax; i++) s.create(i * 10, fakeRandom, t);
  TEST_ASSERT_FALSE(s.validate(first, 200));  // a mais antiga foi substituída
  TEST_ASSERT_TRUE(s.validate(t, 200));
  TEST_ASSERT_EQUAL(SessionStore::kMax, s.active(200));
}

void test_session_survives_millis_wraparound() {
  SessionStore s(1000, 5000);
  char t[33];
  s.create(0xFFFFFF00u, fakeRandom, t);
  TEST_ASSERT_TRUE(s.validate(t, 0x00000100u));  // 512 ms depois, com overflow
}

void test_login_throttle_backoff() {
  LoginThrottle th;
  TEST_ASSERT_TRUE(th.allowed(0));
  th.failure(0);
  th.failure(0);
  TEST_ASSERT_TRUE(th.allowed(0));
  th.failure(1000);  // 3ª falha: 5 s
  TEST_ASSERT_FALSE(th.allowed(1000));
  TEST_ASSERT_EQUAL_UINT32(5000, th.retryAfterMs(1000));
  TEST_ASSERT_TRUE(th.allowed(6000));
  th.failure(6000);  // 4ª: 10 s
  TEST_ASSERT_EQUAL_UINT32(10000, th.retryAfterMs(6000));
  for (int i = 0; i < 20; i++) th.failure(20000);
  TEST_ASSERT_EQUAL_UINT32(300000, th.retryAfterMs(20000));  // teto 5 min
  th.success();
  TEST_ASSERT_TRUE(th.allowed(20001));
}

void setUp() {}
void tearDown() {}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_static_scene_has_no_motion);
  RUN_TEST(test_sensor_noise_is_ignored);
  RUN_TEST(test_moving_object_detected_with_box);
  RUN_TEST(test_global_brightness_drift_is_compensated);
  RUN_TEST(test_light_switch_is_not_motion);
  RUN_TEST(test_warmup_suppresses_detection);
  RUN_TEST(test_resolution_change_resets);
  RUN_TEST(test_null_input_is_safe);
  RUN_TEST(test_const_time_equals);
  RUN_TEST(test_cookie_parsing);
  RUN_TEST(test_session_lifecycle);
  RUN_TEST(test_session_revoke_and_bad_tokens);
  RUN_TEST(test_session_eviction_when_full);
  RUN_TEST(test_session_survives_millis_wraparound);
  RUN_TEST(test_login_throttle_backoff);
  return UNITY_END();
}
