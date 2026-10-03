// Autenticação: senha, sessões por cookie e limitação de tentativas.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace AuthService {
static constexpr const char *kCookie = "sid";
void begin();
// Retorna 0 = ok (token em out[33]), >0 = bloqueado por N ms, -1 = senha errada
int32_t login(const char *password, size_t len, char *tokenOut);
bool validateCookieHeader(const char *cookieHeader);
void logout(const char *cookieHeader);
}  // namespace AuthService
