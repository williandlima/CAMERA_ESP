// Acesso remoto opcional: a própria câmera é um cliente WireGuard (sem PC ligado).
// Ativado quando WG_* estiver definido em secrets.h; senão, não compila nada extra.
#pragma once

namespace VpnService {
void begin();            // cria a tarefa que sobe o túnel após Wi-Fi + NTP
bool enabled();          // configurado em secrets.h
bool up();               // túnel criado (o handshake ocorre em seguida)
const char *ip();        // IP da câmera dentro da VPN ("" se desativado)
}  // namespace VpnService
