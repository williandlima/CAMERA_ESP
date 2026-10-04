#include "stream_server.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <strings.h>

#include "auth_service.h"
#include "config.h"
#include "vpn_service.h"
#include "frame_hub.h"

namespace StreamServer {

static volatile uint8_t s_clients = 0;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static const char *kBoundary = "esp32camframe";

uint8_t clients() { return s_clients; }

static bool sendAll(int sock, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  while (len) {
    const int n = send(sock, p, len, 0);
    if (n <= 0) return false;
    p += n;
    len -= (size_t)n;
  }
  return true;
}

static void sendStatus(int sock, const char *status) {
  char buf[160];
  const int n = snprintf(buf, sizeof(buf),
                         "HTTP/1.1 %s\r\nContent-Type: text/plain\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s",
                         status, (unsigned)strlen(status), status);
  sendAll(sock, buf, (size_t)n);
}

// Valor de um cabeçalho (sem diferenciar maiúsculas); `req` termina em "\r\n\r\n".
static bool header(const char *req, const char *name, char *out, size_t outLen) {
  const size_t nl = strlen(name);
  for (const char *line = strstr(req, "\r\n"); line && line[2]; line = strstr(line + 2, "\r\n")) {
    const char *h = line + 2;
    if (strncasecmp(h, name, nl) == 0 && h[nl] == ':') {
      h += nl + 1;
      while (*h == ' ') h++;
      size_t i = 0;
      while (h[i] && h[i] != '\r' && i + 1 < outLen) {
        out[i] = h[i];
        i++;
      }
      out[i] = 0;
      return true;
    }
  }
  return false;
}

// Libera CORS só para a própria placa (IP, IP da VPN ou nome mDNS), em qualquer porta.
static bool originAllowed(const char *origin) {
  if (strncmp(origin, "http://", 7)) return false;
  const char *host = origin + 7;
  const char *end = strchr(host, ':');
  const size_t hl = end ? (size_t)(end - host) : strlen(host);
  const String ip = WiFi.localIP().toString();
  const String mdns = String(HOSTNAME) + ".local";
  const char *vpn = VpnService::ip();  // acesso pelo túnel WireGuard
  return (hl == ip.length() && strncmp(host, ip.c_str(), hl) == 0) ||
         (*vpn && hl == strlen(vpn) && strncmp(host, vpn, hl) == 0) ||
         (hl == mdns.length() && strncasecmp(host, mdns.c_str(), hl) == 0);
}

static void clientTask(void *arg) {
  const int sock = (int)(intptr_t)arg;
  timeval tv = {3, 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  tv.tv_sec = 5;
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  int one = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  static const size_t kReqMax = 2048;
  char *req = (char *)malloc(kReqMax);
  size_t got = 0;
  bool complete = false;
  while (req && got < kReqMax - 1) {
    const int n = recv(sock, req + got, kReqMax - 1 - got, 0);
    if (n <= 0) break;
    got += (size_t)n;
    req[got] = 0;
    if (strstr(req, "\r\n\r\n")) {
      complete = true;
      break;
    }
  }

  char cookie[256] = "", origin[96] = "";
  bool ok = false;
  if (complete && strncmp(req, "GET /stream", 11) == 0) {
    header(req, "Cookie", cookie, sizeof(cookie));
    header(req, "Origin", origin, sizeof(origin));
    ok = AuthService::validateCookieHeader(cookie);
    if (!ok) sendStatus(sock, "401 Unauthorized");
  } else if (complete) {
    sendStatus(sock, "404 Not Found");
  }
  free(req);

  if (ok) {
    char hdr[384];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: multipart/x-mixed-replace;boundary=%s\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n",
                     kBoundary);
    if (origin[0] && originAllowed(origin)) {
      n += snprintf(hdr + n, sizeof(hdr) - n,
                    "Access-Control-Allow-Origin: %s\r\nAccess-Control-Allow-Credentials: true\r\nVary: Origin\r\n",
                    origin);
    }
    n += snprintf(hdr + n, sizeof(hdr) - n, "\r\n");
    uint32_t seq = 0;
    if (sendAll(sock, hdr, (size_t)n)) {
      for (;;) {
        FramePtr f = FrameHub::waitNewer(seq, 2000);
        if (!f) continue;
        seq = f->seq;
        char part[128];
        const int pn = snprintf(part, sizeof(part), "--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                                kBoundary, (unsigned)f->len);
        if (!sendAll(sock, part, (size_t)pn) || !sendAll(sock, f->data, f->len) || !sendAll(sock, "\r\n", 2)) break;
      }
    }
  }
  shutdown(sock, SHUT_RDWR);
  close(sock);
  portENTER_CRITICAL(&s_mux);
  s_clients--;
  portEXIT_CRITICAL(&s_mux);
  vTaskDelete(nullptr);
}

static void listenTask(void *) {
  const int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(STREAM_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (srv < 0 || bind(srv, (sockaddr *)&addr, sizeof(addr)) != 0 || listen(srv, 4) != 0) {
    Serial.println("[stream] falha ao abrir porta");
    vTaskDelete(nullptr);
    return;
  }
  for (;;) {
    sockaddr_in peer;
    socklen_t pl = sizeof(peer);
    const int c = accept(srv, (sockaddr *)&peer, &pl);
    if (c < 0) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    bool full;
    portENTER_CRITICAL(&s_mux);
    full = s_clients >= STREAM_MAX_CLIENTS;
    if (!full) s_clients++;
    portEXIT_CRITICAL(&s_mux);
    if (full) {
      sendStatus(c, "503 Service Unavailable");
      close(c);
      continue;
    }
    if (xTaskCreatePinnedToCore(clientTask, "stream", 6144, (void *)(intptr_t)c, 3, nullptr, 0) != pdPASS) {
      close(c);
      portENTER_CRITICAL(&s_mux);
      s_clients--;
      portEXIT_CRITICAL(&s_mux);
    }
  }
}

void begin() { xTaskCreatePinnedToCore(listenTask, "stream-listen", 4096, nullptr, 2, nullptr, 0); }

}  // namespace StreamServer
