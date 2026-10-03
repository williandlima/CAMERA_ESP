// Prints de movimento: anel na PSRAM (galeria) + gravação assíncrona no SD.
#pragma once
#include <Arduino.h>
#include <time.h>

#include "frame_hub.h"

namespace SnapshotStore {
struct Info {
  uint32_t id;
  time_t time;
  float percent;
  bool manual;
};
void begin();
void add(const FramePtr &f, float percent, bool manual);
FramePtr get(uint32_t id);
size_t list(Info *out, size_t max);  // mais recente primeiro
uint32_t lastId();
bool sdReady();
uint64_t sdUsedBytes();
uint64_t sdTotalBytes();
}  // namespace SnapshotStore
