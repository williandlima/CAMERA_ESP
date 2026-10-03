// Configuração fixa de hardware e valores padrão.
// Ajustes de câmera/movimento podem ser alterados pela página e ficam salvos na placa.
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "2.0.0"
#endif

// ---------- Pinagem GOOUUU ESP32-S3-CAM (OV2640/OV5640) ----------
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y2_GPIO_NUM    11
#define Y3_GPIO_NUM    9
#define Y4_GPIO_NUM    8
#define Y5_GPIO_NUM    10
#define Y6_GPIO_NUM    12
#define Y7_GPIO_NUM    18
#define Y8_GPIO_NUM    17
#define Y9_GPIO_NUM    16
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13
#define CAM_XCLK_HZ    20000000

// ---------- Cartão SD (SD_MMC 1-bit) – opcional ----------
#define USE_SD         1
#define SD_CLK_PIN     39
#define SD_CMD_PIN     38
#define SD_D0_PIN      40
#define SD_DIR         "/motion"
#define SD_MAX_USED_PCT 90          // apaga as gravações mais antigas acima disso

// ---------- LED RGB (WS2812) ----------
#define LED_PIN        48
// Alguns LEDs desta placa recebem vermelho e verde invertidos (ordem RGB em vez de GRB).
// Se "liberado" acender vermelho e o normal acender verde, troque para 0.
#define LED_SWAP_RG    1

// ---------- Rede ----------
#define HOSTNAME       "esp32cam"   // http://esp32cam.local/
#define HTTP_PORT      80
#define STREAM_PORT    81
#define STREAM_MAX_CLIENTS 3
#define TZ_INFO        "<-03>3"     // Brasília
#define NTP_SERVER_1   "pool.ntp.org"
#define NTP_SERVER_2   "time.google.com"

// ---------- Sessões ----------
#define SESSION_IDLE_MS  (7UL * 24 * 3600 * 1000)   // 7 dias sem uso
#define SESSION_MAX_MS   (30UL * 24 * 3600 * 1000)  // 30 dias no máximo

// ---------- Memória ----------
#define MAX_SNAPSHOTS    12         // prints mantidos na PSRAM (galeria)
#define MOTION_MAX_WIDTH 160        // largura máxima da imagem analisada
#define FACES_MAX_BYTES  (512 * 1024)
