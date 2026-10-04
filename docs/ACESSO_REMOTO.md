# Acesso remoto sem computador (WireGuard na câmera)

A câmera conecta sozinha a um servidor WireGuard na internet. Seu celular entra no mesmo servidor, de qualquer rede (Wi-Fi ou dados), e abre a câmera pelo IP da VPN (`http://10.8.0.2/`). Nenhuma porta é aberta no seu roteador e nenhum computador precisa ficar ligado.

```
celular (10.8.0.3) ──► servidor WireGuard (10.8.0.1, IP público) ◄── câmera (10.8.0.2)
```

## 1. Servidor (uma vez)

Qualquer VPS Linux com IP público serve (por exemplo Oracle Cloud *Always Free* ou um VPS de ~US$ 4/mês). Libere a porta **UDP 51820** no firewall do provedor.

```bash
sudo apt update && sudo apt install -y wireguard qrencode
cd /etc/wireguard && umask 077
wg genkey | tee server.key | wg pubkey > server.pub
wg genkey | tee cam.key    | wg pubkey > cam.pub
wg genkey | tee phone.key  | wg pubkey > phone.pub
```

Crie `/etc/wireguard/wg0.conf` (troque `ETH` pela interface de rede, veja com `ip -br a`; só é necessário para o celular alcançar a câmera, não para internet):

```ini
[Interface]
Address = 10.8.0.1/24
ListenPort = 51820
PrivateKey = <conteúdo de server.key>
# permite celular <-> câmera passando pelo servidor
PostUp   = sysctl -w net.ipv4.ip_forward=1
PostUp   = iptables -A FORWARD -i wg0 -o wg0 -j ACCEPT
PostDown = iptables -D FORWARD -i wg0 -o wg0 -j ACCEPT

[Peer]  # câmera
PublicKey = <conteúdo de cam.pub>
AllowedIPs = 10.8.0.2/32

[Peer]  # celular
PublicKey = <conteúdo de phone.pub>
AllowedIPs = 10.8.0.3/32
```

```bash
sudo systemctl enable --now wg-quick@wg0
sudo wg show        # confira que subiu
```

## 2. Câmera

Em `src/secrets.h` (copie de `secrets.h.example`), descomente e preencha:

```c
#define WG_LOCAL_IP         "10.8.0.2"
#define WG_PRIVATE_KEY      "<conteúdo de cam.key>"
#define WG_PEER_PUBLIC_KEY  "<conteúdo de server.pub>"
#define WG_ENDPOINT         "IP-ou-nome-do-servidor"
#define WG_PORT             51820
```

Grave o firmware (USB ou pela página). No monitor serial deve aparecer `[vpn] tunel WireGuard ativo`. No servidor, `sudo wg show` passa a mostrar *latest handshake* para a câmera.

## 3. Celular

No servidor, gere o arquivo do celular e leia o QR code com o app **WireGuard** (Android/iOS):

```bash
cat > phone.conf <<CONF
[Interface]
PrivateKey = $(cat /etc/wireguard/phone.key)
Address = 10.8.0.3/32

[Peer]
PublicKey = $(cat /etc/wireguard/server.pub)
Endpoint = IP-ou-nome-do-servidor:51820
AllowedIPs = 10.8.0.0/24
PersistentKeepalive = 25
CONF
qrencode -t ansiutf8 < phone.conf
```

`AllowedIPs = 10.8.0.0/24` envia pelo túnel só o tráfego da câmera; o resto da internet do celular continua normal (o reconhecimento facial baixa os modelos pela internet do próprio celular). Ligue o túnel e abra **http://10.8.0.2/**.

Mais celulares: repita o par de chaves e um novo `[Peer]` com `10.8.0.4/32`, etc.

## Observações

- **Quando a câmera não está na VPN:** o acesso local (`http://IP-da-câmera/` e `esp32cam.local`) continua funcionando normalmente.
- **Desempenho:** a criptografia roda no ESP32, então o vídeo pela VPN é mais pesado. Se travar, reduza a resolução/qualidade na aba de ajustes.
- **Segurança:** o tráfego dentro do túnel é criptografado. Mantenha `secrets.h` fora do git (já está no `.gitignore`) e não compartilhe `cam.key` nem `phone.key`.
- **Hora:** a VPN só sobe depois do NTP sincronizar (o handshake exige data correta).
