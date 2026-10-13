// 端末ID。Drive 共通仕様「01_IDと定型信号」:
//   MQTT/サーバー: 文字列 "dev-001"(dev- + 3桁以上)  /  LoRa: u16 の 1
//   0 = 管理PC("pc")、65535(0xFFFF) = 全員("all")。端末には割り当てない。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace reray {

constexpr uint16_t kIdServer = 0;
constexpr uint16_t kIdBroadcast = 0xFFFF;

// out に書き込み、文字数を返す。バッファ不足なら 0。
inline size_t formatDeviceId(uint16_t id, char* out, size_t cap) {
  int n;
  if (id == kIdServer) n = snprintf(out, cap, "pc");
  else if (id == kIdBroadcast) n = snprintf(out, cap, "all");
  else n = snprintf(out, cap, "dev-%03u", (unsigned)id);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

// "dev-001" / "pc" / "all" を u16 にする。不正なら false。"dev-0" と "dev-65535" 以上は端末IDとして不正。
inline bool parseDeviceId(const char* s, uint16_t& id) {
  if (strcmp(s, "pc") == 0) { id = kIdServer; return true; }
  if (strcmp(s, "all") == 0) { id = kIdBroadcast; return true; }
  if (strncmp(s, "dev-", 4) != 0) return false;
  const char* p = s + 4;
  if (*p == '\0') return false;
  uint32_t v = 0;
  for (; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (uint32_t)(*p - '0');
    if (v > 0xFFFF) return false;
  }
  if (v == kIdServer || v == kIdBroadcast) return false;
  id = (uint16_t)v;
  return true;
}

}  // namespace reray
