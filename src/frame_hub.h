// Quadros JPEG imutáveis com contagem de referência.
// A captura publica; stream, galeria e detector compartilham o mesmo buffer
// sem cópias e sem segurar trava durante o envio pela rede.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <memory>

struct Frame {
  uint8_t *data = nullptr;
  size_t len = 0;
  uint32_t seq = 0;
  uint16_t width = 0, height = 0;
  int64_t timeUs = 0;
  Frame() = default;
  Frame(const Frame &) = delete;
  Frame &operator=(const Frame &) = delete;
  ~Frame();
};
using FramePtr = std::shared_ptr<const Frame>;

namespace FrameHub {
void begin();
// Copia o JPEG para a PSRAM. Retorna nullptr se faltar memória.
FramePtr make(const uint8_t *jpg, size_t len, uint16_t w, uint16_t h);
void publish(FramePtr f);
FramePtr latest();
// Aguarda um quadro com seq diferente de lastSeq; nullptr no timeout.
FramePtr waitNewer(uint32_t lastSeq, uint32_t timeoutMs);
float fps();
}  // namespace FrameHub
