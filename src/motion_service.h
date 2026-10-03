// Liga a captura ao detector de movimento e aos prints.
#pragma once
#include "frame_hub.h"
#include "settings.h"

namespace MotionService {
struct Status {
  bool motion = false;
  bool lightChange = false;
  float percent = 0;
  float box[4] = {0, 0, 0, 0};  // x0,y0,x1,y1 normalizados 0..1
  uint32_t events = 0;
};
void begin();
void configure(const MotionSettings &m);
void onFrame(const FramePtr &f);  // chamado pela tarefa de captura
Status status();
}  // namespace MotionService
