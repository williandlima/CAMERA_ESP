#include "vpn_service.h"

#include <Arduino.h>
#include <time.h>

#include "net_service.h"
#include "secrets.h"

#if defined(WG_LOCAL_IP) && defined(WG_PRIVATE_KEY) && defined(WG_PEER_PUBLIC_KEY) && defined(WG_ENDPOINT)
#define VPN_ENABLED 1
#include <WireGuard-ESP32.h>
#include <lwip/netif.h>
#ifndef WG_PORT
#define WG_PORT 51820
#endif
#ifndef WG_NETMASK
#define WG_NETMASK "255.255.255.0"
#endif
#else
#define VPN_ENABLED 0
#endif

namespace VpnService {

#if VPN_ENABLED
static WireGuard s_wg;
static volatile bool s_up = false;

static void task(void *) {
  IPAddress local, mask;
  local.fromString(WG_LOCAL_IP);
  mask.fromString(WG_NETMASK);
  for (;;) {
    // O handshake carrega um timestamp: sem NTP o servidor rejeita (replay).
    if (!NetService::connected() || time(nullptr) < 1700000000) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    netif *wifi = netif_default;  // a biblioteca troca a rota padrão para o túnel
    const bool ok = s_wg.begin(local, mask, IPAddress(0, 0, 0, 0), WG_PRIVATE_KEY, WG_ENDPOINT, WG_PEER_PUBLIC_KEY, WG_PORT);
    if (wifi) netif_set_default(wifi);  // NTP e demais saídas continuam pelo Wi-Fi
    if (ok) {
      s_up = true;
      Serial.printf("[vpn] tunel WireGuard ativo: http://%s/\n", WG_LOCAL_IP);
      break;
    }
    Serial.println("[vpn] falha ao iniciar (DNS do servidor?); nova tentativa em 30 s");
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
  vTaskDelete(nullptr);
}

void begin() { xTaskCreatePinnedToCore(task, "vpn", 8192, nullptr, 1, nullptr, 0); }
bool enabled() { return true; }
bool up() { return s_up; }
const char *ip() { return WG_LOCAL_IP; }
#else
void begin() {}
bool enabled() { return false; }
bool up() { return false; }
const char *ip() { return ""; }
#endif

}  // namespace VpnService
