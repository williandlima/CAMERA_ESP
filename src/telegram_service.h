// Alertas e comandos pelo Telegram (opcional): foto quando há movimento e
// /foto, /status, /pausar, /ativar. Habilitado quando TG_BOT_TOKEN e TG_CHAT_ID
// estiverem definidos em secrets.h. A câmera só faz conexões de saída (HTTPS).
#pragma once
#include "frame_hub.h"

namespace TelegramService {
void begin();                                       // cria a tarefa (após Wi-Fi + NTP)
bool enabled();                                     // configurado em secrets.h
void onMotion(const FramePtr &f, float percent);    // chamado pelo detector; tem limite de frequência
}  // namespace TelegramService
