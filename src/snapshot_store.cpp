#include "snapshot_store.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"
#if USE_SD
#include <FS.h>
#include <SD_MMC.h>
#endif

namespace SnapshotStore {

struct Slot {
  FramePtr frame;
  Info info{};
};

static Slot s_ring[MAX_SNAPSHOTS];
static uint32_t s_nextId = 1;
static SemaphoreHandle_t s_mtx;
static QueueHandle_t s_sdQueue;
static volatile bool s_sdOk = false;

struct SdJob {
  FramePtr *frame;  // alocado com new; a tarefa libera
  time_t time;
  uint32_t id;
};

#if USE_SD
static void pruneIfFull() {
  const uint64_t total = SD_MMC.totalBytes();
  if (!total) return;
  for (int pass = 0; pass < 20 && SD_MMC.usedBytes() * 100 / total > SD_MAX_USED_PCT; pass++) {
    File dir = SD_MMC.open(SD_DIR);
    if (!dir) return;
    String oldest;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String n = f.name();  // nomes AAAAMMDD_HHMMSS: ordem alfabética = cronológica
      if (!f.isDirectory() && (oldest.isEmpty() || n < oldest)) oldest = n;
    }
    dir.close();
    if (oldest.isEmpty()) return;
    if (!oldest.startsWith("/")) oldest = String(SD_DIR) + "/" + oldest;
    SD_MMC.remove(oldest);
  }
}

static void sdTask(void *) {
  SdJob job;
  for (;;) {
    if (xQueueReceive(s_sdQueue, &job, portMAX_DELAY) != pdTRUE) continue;
    const FramePtr &f = *job.frame;
    char path[64];
    if (job.time > 1600000000) {
      struct tm t;
      localtime_r(&job.time, &t);
      strftime(path, sizeof(path), SD_DIR "/%Y%m%d_%H%M%S", &t);
      snprintf(path + strlen(path), sizeof(path) - strlen(path), "_%u.jpg", (unsigned)job.id);
    } else {  // relógio ainda não sincronizado
      snprintf(path, sizeof(path), SD_DIR "/nosync_%010lu_%u.jpg", (unsigned long)millis(), (unsigned)job.id);
    }
    File out = SD_MMC.open(path, FILE_WRITE);
    if (out) {
      out.write(f->data, f->len);
      out.close();
    } else {
      Serial.printf("[sd] falha ao gravar %s\n", path);
    }
    delete job.frame;
    static uint32_t writes = 0;
    if (++writes % 25 == 1) pruneIfFull();  // consultar espaço livre é lento em cartões grandes
  }
}
#endif

void begin() {
  s_mtx = xSemaphoreCreateMutex();
#if USE_SD
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
  if (SD_MMC.begin("/sdcard", true) && SD_MMC.cardType() != CARD_NONE) {
    if (!SD_MMC.exists(SD_DIR)) SD_MMC.mkdir(SD_DIR);
    s_sdQueue = xQueueCreate(6, sizeof(SdJob));
    xTaskCreatePinnedToCore(sdTask, "sd", 6144, nullptr, 2, nullptr, 0);
    s_sdOk = true;
  }
#endif
  Serial.println(s_sdOk ? "[sd] cartao ok" : "[sd] sem cartao (apenas PSRAM)");
}

void add(const FramePtr &f, float percent, bool manual) {
  if (!f) return;
  const time_t now = time(nullptr);
  uint32_t id;
  FramePtr evicted;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  id = s_nextId++;
  Slot &s = s_ring[(id - 1) % MAX_SNAPSHOTS];
  evicted = std::move(s.frame);
  s.frame = f;
  s.info = {id, now, percent, manual};
  xSemaphoreGive(s_mtx);
  Serial.printf("[motion] print #%u (%u bytes, %.1f%%)\n", (unsigned)id, (unsigned)f->len, percent);
#if USE_SD
  if (s_sdOk) {
    SdJob job{new FramePtr(f), now, id};
    if (xQueueSend(s_sdQueue, &job, 0) != pdTRUE) delete job.frame;  // fila cheia: descarta
  }
#endif
}

FramePtr get(uint32_t id) {
  if (!id) return nullptr;
  FramePtr f;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  const Slot &s = s_ring[(id - 1) % MAX_SNAPSHOTS];
  if (s.info.id == id) f = s.frame;
  xSemaphoreGive(s_mtx);
  return f;
}

size_t list(Info *out, size_t max) {
  size_t n = 0;
  xSemaphoreTake(s_mtx, portMAX_DELAY);
  for (uint32_t id = s_nextId - 1; id > 0 && n < max && id + MAX_SNAPSHOTS >= s_nextId; id--) {
    const Slot &s = s_ring[(id - 1) % MAX_SNAPSHOTS];
    if (s.info.id == id && s.frame) out[n++] = s.info;
  }
  xSemaphoreGive(s_mtx);
  return n;
}

uint32_t lastId() { return s_nextId - 1; }
bool sdReady() { return s_sdOk; }

uint64_t sdUsedBytes() {
#if USE_SD
  return s_sdOk ? SD_MMC.usedBytes() : 0;
#else
  return 0;
#endif
}

uint64_t sdTotalBytes() {
#if USE_SD
  return s_sdOk ? SD_MMC.totalBytes() : 0;
#else
  return 0;
#endif
}

}  // namespace SnapshotStore
