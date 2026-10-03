# Monitor de ambiente – GOOUUU ESP32-S3-CAM

Página web protegida por senha com imagem ao vivo, detecção de movimento e prints automáticos.

## Uso
1. `cp src/secrets.h.example src/secrets.h` e preencha Wi-Fi e senha da página.
2. `pio run -t upload && pio device monitor` (PlatformIO, placa N16R8 com PSRAM octal).
3. Abra o IP mostrado no serial (`http://<ip>/`) e entre com a senha.

## Como funciona
- Task na core 0 captura JPEG VGA (~10 fps), guarda o último quadro e decodifica em 80x60 (escala 1/8) para comparar com o quadro anterior.
- Se ≥ `MOTION_AREA_PERCENT` % dos pixels variam mais que `MOTION_PIXEL_THRESHOLD`, salva um print (cooldown `MOTION_COOLDOWN_MS`).
- Prints: últimos `MAX_SNAPSHOTS` na PSRAM (galeria na página) e, se houver cartão SD, arquivos `/mov_AAAAMMDD_HHMMSS.jpg`.
- Login por POST gera cookie de sessão aleatório (HttpOnly); 5 erros bloqueiam por 60 s. Todas as rotas de imagem exigem o cookie.
- Ao vivo é feito por polling de `/live.jpg` (sem bloquear o servidor).

Ajuste pinos, sensibilidade e fuso em `src/config.h`.
Obs.: HTTP sem TLS – use apenas em rede local confiável (ou atrás de VPN/proxy HTTPS).
Obs.: o código não foi compilado/testado em hardware nesta sessão (sem acesso à rede para baixar o toolchain).

## Reconhecimento facial (no PC)
O ESP32 só transmite o vídeo; o PC lê o stream, detecta rostos (YuNet), identifica (SFace) e escreve o nome na imagem.
```
cd recognizer
pip install -r requirements.txt
# coloque fotos em known_faces/ (Willian.jpg, Maria.jpg ...)
python recognize.py --host <IP_DA_PLACA> --password <WEB_PASSWORD> [--serve]
```
`--serve` publica o vídeo anotado em `http://<ip-do-pc>:5000/` (senha = WEB_PASSWORD). Os modelos ONNX são baixados na primeira execução.
