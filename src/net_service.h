// Wi-Fi com reconexão automática, mDNS, NTP e OTA via PlatformIO/Arduino IDE.
#pragma once

namespace NetService {
void begin();   // bloqueia até conectar ou 20 s (segue tentando em segundo plano)
void loop();
bool connected();
}  // namespace NetService
