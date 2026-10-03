#include "motion.h"

namespace core {

void MotionDetector::reset() {
  frames_ = 0;
  streak_ = 0;
}

MotionResult MotionDetector::process(const uint8_t *gray, int w, int h) {
  MotionResult r;
  if (!gray || w <= 0 || h <= 0) return r;
  const size_t n = (size_t)w * (size_t)h;
  if (w != w_ || h != h_ || bg_.size() != n) {
    w_ = w;
    h_ = h;
    bg_.assign(n, 0);
    mask_.assign(n, 0);
    reset();
  }
  if (frames_ == 0) {
    for (size_t i = 0; i < n; i++) bg_[i] = (uint16_t)(gray[i] << 4);
    frames_ = 1;
    return r;
  }

  // Compensa variação global de brilho (auto-exposição/ganho).
  int64_t sumG = 0, sumB = 0;
  for (size_t i = 0; i < n; i++) {
    sumG += gray[i];
    sumB += bg_[i];
  }
  const int offset = (int)((sumG * 16 - sumB) / (int64_t)n);
  const int thr = (int)params_.pixelThreshold * 16;

  for (size_t i = 0; i < n; i++) {
    int d = ((int)gray[i] << 4) - (int)bg_[i] - offset;
    if (d < 0) d = -d;
    mask_[i] = d > thr ? 1 : 0;
  }

  // Conta só pixels com ao menos um vizinho (remove ruído isolado).
  size_t count = 0;
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; y++) {
    const size_t row = (size_t)y * w;
    for (int x = 0; x < w; x++) {
      const size_t i = row + x;
      if (!mask_[i]) continue;
      const bool nb = (x > 0 && mask_[i - 1]) || (x < w - 1 && mask_[i + 1]) ||
                      (y > 0 && mask_[i - w]) || (y < h - 1 && mask_[i + w]);
      if (!nb) continue;
      count++;
      if (x < x0) x0 = x;
      if (x > x1) x1 = x;
      if (y < y0) y0 = y;
      if (y > y1) y1 = y;
    }
  }
  r.percent = 100.0f * (float)count / (float)n;
  const bool warm = frames_ < params_.warmupFrames;

  if (r.percent >= params_.lightChangePercent) {
    // Mudança de luz (lâmpada acesa, nuvem): reaprende o fundo de imediato.
    r.lightChange = true;
    for (size_t i = 0; i < n; i++) bg_[i] = (uint16_t)(gray[i] << 4);
    streak_ = 0;
    frames_++;
    return r;
  }

  r.raw = !warm && r.percent >= params_.minAreaPercent;
  streak_ = r.raw ? streak_ + 1 : 0;
  const uint32_t need = params_.confirmFrames ? params_.confirmFrames : 1;
  r.motion = streak_ >= need;
  if (r.motion) {
    r.x0 = (int16_t)x0;
    r.y0 = (int16_t)y0;
    r.x1 = (int16_t)x1;
    r.y1 = (int16_t)y1;
  }

  // Atualização seletiva: regiões em movimento aprendem 4x mais devagar,
  // para um objeto que passa não "entrar" no fundo.
  const int fast = 1 << params_.learnShift;
  const int slow = 1 << (params_.learnShift + 2);
  for (size_t i = 0; i < n; i++) {
    const int target = (int)gray[i] << 4;
    const int b = bg_[i];
    bg_[i] = (uint16_t)(b + (target - b) / (mask_[i] ? slow : fast));
  }
  frames_++;
  return r;
}

}  // namespace core
