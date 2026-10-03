// LED RGB de status: azul piscando = conectando, vermelho = normal,
// verde = rosto conhecido, roxo = atualizando firmware.
#pragma once
#include <stdint.h>

namespace StatusLed {
enum class Mode : uint8_t { Connecting, Normal, Known, Updating };
void begin();
void setMode(Mode m);
void knownFace(uint16_t holdMs);  // verde por holdMs
void update();                    // chamar no loop
bool knownActive();
}  // namespace StatusLed
