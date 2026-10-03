#pragma once

// Pinagem GOOUUU ESP32-S3-CAM (OV2640/OV5640)
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

// Cartão SD (SD_MMC 1-bit) – opcional
#define USE_SD         1
#define SD_CLK_PIN     39
#define SD_CMD_PIN     38
#define SD_D0_PIN      40

// Detecção de movimento
#define MOTION_PIXEL_THRESHOLD 40   // variação mínima por pixel (soma dos 2 bytes RGB565)
#define MOTION_AREA_PERCENT    3    // % de pixels alterados para disparar
#define MOTION_COOLDOWN_MS     5000 // intervalo mínimo entre prints
#define MOTION_WARMUP_FRAMES   10   // quadros ignorados após boot

#define MAX_SNAPSHOTS  8            // prints guardados na PSRAM
#define SESSION_MAX    4            // sessões simultâneas
#define TZ_INFO        "<-03>3"     // Brasília
