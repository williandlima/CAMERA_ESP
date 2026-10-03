// Detecção de movimento independente de hardware (testável no PC).
//
// Modelo de fundo com média móvel exponencial em ponto fixo, compensação do
// brilho global (auto-exposição), filtro de ruído por vizinhança, rejeição de
// mudança brusca de luz e confirmação por quadros consecutivos.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace core {

struct MotionParams {
  uint8_t pixelThreshold = 22;        // diferença mínima de luminância (0-255) por pixel
  float minAreaPercent = 1.0f;        // % mínima da imagem em movimento
  float lightChangePercent = 55.0f;   // acima disso é variação de luz, não movimento
  uint8_t learnShift = 4;             // aprendizado do fundo = 1/2^learnShift por quadro
  uint8_t confirmFrames = 2;          // quadros seguidos para confirmar
  uint8_t warmupFrames = 8;           // quadros iniciais só para aprender o fundo
};

struct MotionResult {
  bool motion = false;       // movimento confirmado
  bool raw = false;          // acima do limiar neste quadro
  bool lightChange = false;  // variação global de luz (ignorada)
  float percent = 0.0f;      // % de pixels em movimento (após filtro)
  int16_t x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // caixa do movimento na imagem analisada
};

class MotionDetector {
 public:
  void setParams(const MotionParams &p) { params_ = p; }
  const MotionParams &params() const { return params_; }
  void reset();
  // gray: imagem w*h em tons de cinza (1 byte/pixel)
  MotionResult process(const uint8_t *gray, int w, int h);

 private:
  MotionParams params_;
  std::vector<uint16_t> bg_;   // fundo em ponto fixo (valor * 16)
  std::vector<uint8_t> mask_;
  int w_ = 0, h_ = 0;
  uint32_t frames_ = 0;
  uint32_t streak_ = 0;
};

}  // namespace core
