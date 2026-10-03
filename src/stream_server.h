// Stream MJPEG na porta 81, vários espectadores simultâneos.
#pragma once
#include <stdint.h>

namespace StreamServer {
void begin();
uint8_t clients();
}  // namespace StreamServer
