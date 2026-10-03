#include "session.h"

#include <string.h>

namespace core {

bool constTimeEquals(const char *a, size_t alen, const char *b, size_t blen) {
  // Percorre sempre o comprimento de `b` (o segredo) para não revelar o tamanho.
  uint8_t diff = (uint8_t)(alen != blen);
  for (size_t i = 0; i < blen; i++) {
    const uint8_t ca = i < alen ? (uint8_t)a[i] : 0;
    diff |= (uint8_t)(ca ^ (uint8_t)b[i]);
  }
  return diff == 0;
}

bool cookieValue(const char *header, const char *name, char *out, size_t outLen) {
  if (!header || !name || !out || outLen == 0) return false;
  const size_t nl = strlen(name);
  const char *p = header;
  while (*p) {
    while (*p == ' ' || *p == ';') p++;
    if (strncmp(p, name, nl) == 0 && p[nl] == '=') {
      p += nl + 1;
      size_t i = 0;
      while (p[i] && p[i] != ';') {
        if (i + 1 >= outLen) return false;
        out[i] = p[i];
        i++;
      }
      out[i] = 0;
      return i > 0;
    }
    while (*p && *p != ';') p++;
  }
  return false;
}

bool SessionStore::expired(const Slot &s, uint32_t now) const {
  return !s.used || (now - s.lastSeen) > idle_ || (now - s.created) > maxAge_;
}

void SessionStore::create(uint32_t nowMs, RandomFn rnd, char *out) {
  Slot *dst = nullptr;
  for (auto &s : slots_) {
    if (expired(s, nowMs)) { dst = &s; break; }
  }
  if (!dst) {  // todas ativas: substitui a menos usada recentemente
    dst = &slots_[0];
    for (auto &s : slots_)
      if ((nowMs - s.lastSeen) > (nowMs - dst->lastSeen)) dst = &s;
  }
  uint8_t raw[kTokenLen / 2];
  rnd(raw, sizeof(raw));
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(raw); i++) {
    dst->token[2 * i] = hex[raw[i] >> 4];
    dst->token[2 * i + 1] = hex[raw[i] & 15];
  }
  dst->token[kTokenLen] = 0;
  dst->created = dst->lastSeen = nowMs;
  dst->used = true;
  memcpy(out, dst->token, kTokenLen + 1);
}

bool SessionStore::validate(const char *token, uint32_t nowMs) {
  if (!token) return false;
  const size_t tl = strnlen(token, kTokenLen + 1);
  if (tl != kTokenLen) return false;
  Slot *hit = nullptr;
  for (auto &s : slots_) {  // percorre todas, sem atalho, em tempo constante
    if (constTimeEquals(token, tl, s.token, kTokenLen) && !expired(s, nowMs)) hit = &s;
  }
  if (!hit) return false;
  hit->lastSeen = nowMs;
  return true;
}

void SessionStore::revoke(const char *token) {
  if (!token) return;
  const size_t tl = strnlen(token, kTokenLen + 1);
  for (auto &s : slots_)
    if (s.used && constTimeEquals(token, tl, s.token, kTokenLen)) s = Slot();
}

void SessionStore::revokeAll() {
  for (auto &s : slots_) s = Slot();
}

size_t SessionStore::active(uint32_t nowMs) const {
  size_t n = 0;
  for (const auto &s : slots_)
    if (!expired(s, nowMs)) n++;
  return n;
}

uint32_t LoginThrottle::retryAfterMs(uint32_t nowMs) const {
  if (!locked_) return 0;
  const int32_t left = (int32_t)(until_ - nowMs);
  return left > 0 ? (uint32_t)left : 0;
}

void LoginThrottle::failure(uint32_t nowMs) {
  if (fails_ < 255) fails_++;
  if (fails_ >= 3) {
    uint32_t lock = 5000u << (fails_ - 3 < 6 ? fails_ - 3 : 6);
    if (lock > 300000u) lock = 300000u;
    locked_ = true;
    until_ = nowMs + lock;
  }
}

}  // namespace core
