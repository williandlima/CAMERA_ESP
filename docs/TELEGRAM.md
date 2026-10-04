# Alertas e foto sob demanda pelo Telegram

A câmera só faz conexões de saída (HTTPS para `api.telegram.org`), então funciona de qualquer rede, sem servidor, VPN nem computador ligado. Não há vídeo ao vivo; para isso use a página (rede local ou VPN).

## O que faz
- **Movimento:** envia uma foto ao seu Telegram, com horário e porcentagem de movimento (no máximo uma a cada `TG_COOLDOWN_S` segundos, padrão 60).
- **Comandos** (só o seu usuário é atendido; mensagens de outros chats são ignoradas):

| Comando | Resposta |
|---|---|
| `/foto` | foto do momento |
| `/status` | FPS, Wi-Fi, movimento, tempo ligada, endereço |
| `/pausar` | para os alertas (volta ao normal ao reiniciar ou com `/ativar`) |
| `/ativar` | retoma os alertas |
| `/ajuda` | lista os comandos |

Os comandos são lidos a cada ~5 s, então a resposta pode demorar alguns segundos.

## Configuração (uma vez, ~5 min)

1. **Criar o bot:** no Telegram, abra a conversa com **@BotFather**, envie `/newbot`, escolha um nome e um usuário terminado em `bot`. Ele responde com o **token** (`123456789:AA...`). Guarde-o: quem tem o token controla o bot.
2. **Falar com o bot:** abra o bot recém-criado e envie `/start` (ele ainda não responde, é normal).
3. **Descobrir seu ID:** no navegador do computador, abra (trocando `SEU_TOKEN` pelo token real):
   ```
   https://api.telegram.org/botSEU_TOKEN/getUpdates
   ```
   Procure `"chat":{"id":123456789` e copie o número (pode ser negativo se usar um grupo).
4. **Preencher o `src/secrets.h`** (copie de `secrets.h.example`; o arquivo não vai para o git):
   ```c
   #define TG_BOT_TOKEN  "123456789:AAxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
   #define TG_CHAT_ID    "123456789"
   #define TG_COOLDOWN_S 60
   ```
5. **Gravar o firmware** (`pio run -t upload` ou pela página). A câmera envia "📷 Câmera online" quando conectar.

## Observações
- **Movimento:** o alerta depende do detector estar ativado na aba de ajustes. Ele não exige que a opção de salvar prints esteja ligada.
- **Privacidade:** as fotos passam pelos servidores do Telegram (sem criptografia de ponta a ponta). Evite apontar a câmera para áreas íntimas.
- **Segurança:** a conexão valida o certificado do Telegram, e a câmera só aceita comandos do `TG_CHAT_ID`. Se o token vazar, gere outro com `/revoke` no BotFather.
- **Sem internet:** se a rede cair, os alertas pendentes são descartados e os comandos voltam sozinhos quando a conexão voltar.
- **Reconhecimento facial:** ele roda no navegador, então não gera alerta no Telegram. Os alertas são de movimento.
