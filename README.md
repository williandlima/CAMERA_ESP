# Monitor de ambiente – GOOUUU ESP32-S3-CAM

Câmera de monitoramento com página web protegida por senha. Ela mostra vídeo ao vivo para vários espectadores, detecta movimento com prints automáticos e reconhece rostos (com antifraude). O LED fica verde para pessoas conhecidas, o firmware atualiza pelo Wi-Fi e o firmware passa por CI.

## Instalação (primeira vez, por USB)

```powershell
cd C:\Dev\CAMERA_ESP
git pull origin claude/gracious-noether-mqmo04
Copy-Item src\secrets.h.example src\secrets.h   # se ainda não existir
notepad src\secrets.h                           # Wi-Fi 2,4 GHz + senha (mín. 8 caracteres)
pio run -t upload
pio device monitor                              # mostra http://<ip>/ a cada 10 s
```

Depois, abra **http://esp32cam.local/** ou o IP mostrado no monitor.

## Atualizações seguintes (sem cabo)

- **Pela página:** na aba *Sistema → Atualizar firmware*, envie `.pio/build/goouuu_esp32s3_cam/firmware.bin`.
- **Pelo PlatformIO via Wi-Fi:**
  ```powershell
  $env:CAM_PASSWORD="sua-senha"; pio run -e ota -t upload
  ```

## Funcionalidades

| Área | O que faz |
|---|---|
| Vídeo | MJPEG na porta 81, até 3 espectadores simultâneos, sem cópia de quadros |
| Movimento | Fundo adaptativo, compensação de auto-exposição, filtro de ruído, ignora acender/apagar luz, confirmação em N quadros, área do movimento desenhada no vídeo |
| Prints | Galeria com os últimos 12 na memória. No cartão SD (`/motion`), as gravações mais antigas são apagadas acima de 90% de uso |
| Rostos | Detecção e reconhecimento no navegador (Human/FaceRes em Web Worker), cadastro com 5 amostras, nome estável por votação e antifraude contra foto ou tela |
| LED | Azul piscando = conectando; vermelho = normal; verde = rosto conhecido; roxo = atualizando |
| Ajustes | Resolução, qualidade, brilho, contraste, espelhar/inverter, sensibilidade e intervalo dos prints, salvos na placa |
| Segurança | Sessão por cookie `HttpOnly`/`SameSite=Strict` com expiração, senha comparada em tempo constante, bloqueio progressivo após erros, CORS restrito à própria placa |
| Robustez | Watchdog na captura, reconexão automática do Wi-Fi, reinício se a câmera travar |

## Arquitetura

```
captura (core 1) ─► FrameHub (quadros com contagem de referência, PSRAM)
                       ├─► stream MJPEG :81   (1 tarefa por espectador)
                       ├─► API /api/*  :80    (esp_http_server)
                       └─► MotionService ─► SnapshotStore ─► tarefa do SD
navegador: app.js ─► face-worker.js (Human) ─► /api/known ─► LED verde
```

| Pasta | Conteúdo |
|---|---|
| `src/` | Firmware em módulos (`camera_service`, `frame_hub`, `motion_service`, `snapshot_store`, `http_api`, `stream_server`, `auth_service`, `settings`, `net_service`, `status_led`) |
| `lib/core/` | Lógica pura, sem hardware (detector de movimento e sessões), testada no PC |
| `web/` | Interface. É compactada (gzip) e embutida no firmware automaticamente no build |
| `test/` | Testes unitários: `pio test -e native` |
| `recognizer/` | Alternativa de reconhecimento em Python no PC (opcional) |

## API

Todas as rotas exigem login, exceto `/api/login`.

| Método | Rota | Descrição |
|---|---|---|
| POST | `/api/login` | `{"password": "..."}`; define o cookie de sessão |
| POST | `/api/logout` | Encerra a sessão |
| GET | `/api/status` | fps, RSSI, memória, temperatura, movimento, SD |
| GET/POST | `/api/settings` | Ler ou alterar ajustes (JSON parcial) |
| GET | `/api/events` | Lista de prints |
| GET | `/api/snapshot?id=N` | Imagem de um print |
| POST | `/api/snapshot` | Tira um print agora |
| GET | `/api/frame.jpg` | Quadro atual |
| GET/POST | `/api/faces` | Rostos cadastrados |
| POST | `/api/known` | Acende o LED verde |
| POST | `/api/ota` | Envia novo firmware |
| POST | `/api/reboot` | Reinicia a placa |
| GET | `:81/stream` | Vídeo MJPEG |

## Observações

- **Internet no aparelho:** o reconhecimento facial roda no navegador e baixa os modelos (~10 MB, ficam em cache) do jsDelivr. O aparelho que abre a página precisa de internet; a câmera não.
- **Cadastros antigos:** os rostos cadastrados na versão anterior (face-api) não são compatíveis. Cadastre as pessoas de novo.
- **Rede:** a conexão é HTTP sem TLS. Use só em rede local confiável ou por VPN (por exemplo, Tailscale). Não exponha as portas 80 e 81 na internet. Para acesso de fora sem computador ligado, a câmera tem cliente WireGuard embutido: veja [docs/ACESSO_REMOTO.md](docs/ACESSO_REMOTO.md).
- **Antifraude:** reduz, mas não elimina, a chance de uma foto enganar o reconhecimento. Não use como controle de acesso.
