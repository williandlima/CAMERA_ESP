// Câmera: inicialização, ajustes do sensor e tarefa de captura.
#pragma once
#include "settings.h"

namespace CameraService {
bool begin(const CameraSettings &cfg);
void apply(const CameraSettings &cfg);  // pode ser chamado em execução
void startTask();
const char *sensorName();
}  // namespace CameraService
