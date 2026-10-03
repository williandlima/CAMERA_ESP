// Sessões de login, comparação em tempo constante e limitação de tentativas.
// Sem dependência de hardware: o relógio e a fonte aleatória são injetados.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace core {

// Compara dois buffers sem vazar, pelo tempo, onde está a diferença.
bool constTimeEquals(const char *a, size_t alen, const char *b, size_t blen);

// Extrai o valor do cookie `name` de um cabeçalho "Cookie: a=1; b=2".
bool cookieValue(const char *header, const char *name, char *out, size_t outLen);

class SessionStore {
 public:
  static constexpr size_t kTokenLen = 32;  // 128 bits em hexadecimal
  static constexpr size_t kMax = 8;
  using RandomFn = void (*)(uint8_t *buf, size_t len);

  SessionStore(uint32_t idleTimeoutMs, uint32_t maxAgeMs) : idle_(idleTimeoutMs), maxAge_(maxAgeMs) {}

  // Cria sessão; escreve o token em `out` (kTokenLen + 1 bytes). Substitui a mais antiga se cheio.
  void create(uint32_t nowMs, RandomFn rnd, char *out);
  // Valida e renova a atividade da sessão.
  bool validate(const char *token, uint32_t nowMs);
  void revoke(const char *token);
  void revokeAll();
  size_t active(uint32_t nowMs) const;

 private:
  struct Slot {
    char token[kTokenLen + 1] = {0};
    uint32_t created = 0;
    uint32_t lastSeen = 0;
    bool used = false;
  };
  bool expired(const Slot &s, uint32_t now) const;
  Slot slots_[kMax];
  uint32_t idle_, maxAge_;
};

// Backoff exponencial: após 3 falhas, bloqueia 5 s, 10 s, 20 s ... até 5 min.
class LoginThrottle {
 public:
  bool allowed(uint32_t nowMs) const { return retryAfterMs(nowMs) == 0; }
  uint32_t retryAfterMs(uint32_t nowMs) const;
  void failure(uint32_t nowMs);
  void success() { fails_ = 0; locked_ = false; }

 private:
  uint8_t fails_ = 0;
  bool locked_ = false;
  uint32_t until_ = 0;
};

}  // namespace core
