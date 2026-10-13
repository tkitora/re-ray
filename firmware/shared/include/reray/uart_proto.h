// T-Beam ⇄ XIAO の UART プロトコル(docs/ARCHITECTURE.md §5)。1行1メッセージ、改行区切りの CSV。
//   T-Beam → XIAO:  STATE,<seq>,<SENT|NEAR|SERVER|RETRY|FAIL>
//                   INSTR,<seq>,<code>,<param>
//                   STATUS,<batt%>,<zone>,<role>,<lora_ok>,<wifi_ok>
//                   LOG,<text>
//   XIAO → T-Beam:  BTN,<id>
//                   ACK_INSTR,<seq>
// FAIL は仕様案になかったが、「未達」をグラスに出すために足した【変更案】。
// ピン・ボーレートは公式資料で確認してから確定する(ここでは行の形式だけを扱う)。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace reray {

enum class UartKind : uint8_t { None, State, Instr, Status, Log, Btn, AckInstr };
enum class LinkState : uint8_t { Sent, Near, Server, Retry, Fail };

constexpr size_t kUartLogMax = 120;  // LOG の本文の上限(バイト)。全角40文字 = UTF-8 で 120 バイト
constexpr size_t kUartLineMax = 160;

struct UartMsg {
  UartKind kind = UartKind::None;
  uint16_t seq = 0;
  LinkState state = LinkState::Sent;
  uint8_t code = 0, param = 0;
  uint8_t batt = 0, zone = 0, role = 0;
  bool loraOk = false, wifiOk = false;
  uint8_t btnId = 0;
  char text[kUartLogMax + 1] = {};
};

inline const char* linkStateName(LinkState s) {
  switch (s) {
    case LinkState::Sent: return "SENT";
    case LinkState::Near: return "NEAR";
    case LinkState::Server: return "SERVER";
    case LinkState::Retry: return "RETRY";
    case LinkState::Fail: return "FAIL";
  }
  return "SENT";
}

namespace detail {

inline size_t done(int n, size_t cap) { return (n > 0 && (size_t)n < cap) ? (size_t)n : 0; }

inline bool parseUint(const char*& p, uint32_t maxVal, uint32_t& out) {
  if (*p < '0' || *p > '9') return false;
  uint32_t v = 0;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (uint32_t)(*p - '0');
    if (v > maxVal) return false;
    p++;
  }
  out = v;
  return true;
}
inline bool eatComma(const char*& p) {
  if (*p != ',') return false;
  p++;
  return true;
}

}  // namespace detail

// ---- 送信側(改行 '\n' を含めて書く。戻り値は長さ、バッファ不足は 0) ----
inline size_t formatState(uint16_t seq, LinkState s, char* out, size_t cap) {
  return detail::done(snprintf(out, cap, "STATE,%u,%s\n", (unsigned)seq, linkStateName(s)), cap);
}
inline size_t formatInstr(uint16_t seq, uint8_t code, uint8_t param, char* out, size_t cap) {
  return detail::done(snprintf(out, cap, "INSTR,%u,%u,%u\n", (unsigned)seq, (unsigned)code, (unsigned)param), cap);
}
inline size_t formatStatus(uint8_t batt, uint8_t zone, uint8_t role, bool loraOk, bool wifiOk, char* out,
                           size_t cap) {
  return detail::done(snprintf(out, cap, "STATUS,%u,%u,%u,%u,%u\n", (unsigned)batt, (unsigned)zone,
                               (unsigned)role, loraOk ? 1u : 0u, wifiOk ? 1u : 0u), cap);
}
// 本文の改行は空白にし、kUartLogMax バイトで切る(UTF-8 の文字の途中では切らない)。
inline size_t formatLog(const char* text, char* out, size_t cap) {
  char body[kUartLogMax + 1];
  size_t n = 0;
  for (const char* p = text; *p && n < kUartLogMax; p++) body[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p;
  // 切った位置の次のバイト(text[n])が UTF-8 の継続バイトなら、文字の途中なので手前の文字境界まで戻る
  if (n == kUartLogMax) {
    while (n > 0 && ((uint8_t)text[n] & 0xC0) == 0x80) n--;
  }
  body[n] = '\0';
  return detail::done(snprintf(out, cap, "LOG,%s\n", body), cap);
}
inline size_t formatBtn(uint8_t id, char* out, size_t cap) {
  return detail::done(snprintf(out, cap, "BTN,%u\n", (unsigned)id), cap);
}
inline size_t formatAckInstr(uint16_t seq, char* out, size_t cap) {
  return detail::done(snprintf(out, cap, "ACK_INSTR,%u\n", (unsigned)seq), cap);
}

// ---- 受信側: 改行を含まない1行を解析する ----
inline bool parseLine(const char* line, UartMsg& m) {
  using namespace detail;
  UartMsg r;
  const char* p = line;
  uint32_t v;
  if (strncmp(p, "STATE,", 6) == 0) {
    p += 6;
    if (!parseUint(p, 65535, v) || !eatComma(p)) return false;
    r.seq = (uint16_t)v;
    static const struct { const char* name; LinkState s; } names[] = {
        {"SENT", LinkState::Sent}, {"NEAR", LinkState::Near}, {"SERVER", LinkState::Server},
        {"RETRY", LinkState::Retry}, {"FAIL", LinkState::Fail}};
    bool ok = false;
    for (const auto& n : names)
      if (strcmp(p, n.name) == 0) { r.state = n.s; ok = true; }
    if (!ok) return false;
    r.kind = UartKind::State;
  } else if (strncmp(p, "INSTR,", 6) == 0) {
    p += 6;
    if (!parseUint(p, 65535, v) || !eatComma(p)) return false;
    r.seq = (uint16_t)v;
    if (!parseUint(p, 255, v) || !eatComma(p)) return false;
    r.code = (uint8_t)v;
    if (!parseUint(p, 255, v) || *p != '\0') return false;
    r.param = (uint8_t)v;
    r.kind = UartKind::Instr;
  } else if (strncmp(p, "STATUS,", 7) == 0) {
    p += 7;
    uint32_t f[5];
    const uint32_t lim[5] = {100, 255, 255, 1, 1};
    for (int i = 0; i < 5; i++) {
      if (!parseUint(p, lim[i], f[i])) return false;
      if (i < 4 ? !eatComma(p) : *p != '\0') return false;
    }
    r.batt = (uint8_t)f[0]; r.zone = (uint8_t)f[1]; r.role = (uint8_t)f[2];
    r.loraOk = f[3] == 1; r.wifiOk = f[4] == 1;
    r.kind = UartKind::Status;
  } else if (strncmp(p, "LOG,", 4) == 0) {
    p += 4;
    const size_t n = strlen(p);
    if (n > kUartLogMax) return false;
    memcpy(r.text, p, n + 1);
    r.kind = UartKind::Log;
  } else if (strncmp(p, "BTN,", 4) == 0) {
    p += 4;
    if (!parseUint(p, 255, v) || *p != '\0') return false;
    r.btnId = (uint8_t)v;
    r.kind = UartKind::Btn;
  } else if (strncmp(p, "ACK_INSTR,", 10) == 0) {
    p += 10;
    if (!parseUint(p, 65535, v) || *p != '\0') return false;
    r.seq = (uint16_t)v;
    r.kind = UartKind::AckInstr;
  } else {
    return false;
  }
  m = r;
  return true;
}

// バイトを1つずつ与えて、行にまとめる。'\r' は無視。長すぎる行は、次の改行まで捨てる(overflows に数える)。
class LineReader {
 public:
  // 行が完成したら true を返し、line が内部バッファ(次の feed まで有効)を指す。空行は無視する。
  bool feed(uint8_t c, const char*& line) {
    if (c == '\r') return false;
    if (c == '\n') {
      const bool ok = !discarding_ && len_ > 0;
      buf_[len_] = '\0';
      const size_t had = len_;
      len_ = 0;
      discarding_ = false;
      if (ok && had > 0) { line = buf_; return true; }
      return false;
    }
    if (discarding_) return false;
    if (len_ >= kUartLineMax) {
      discarding_ = true;
      overflows_++;
      len_ = 0;
      return false;
    }
    buf_[len_++] = (char)c;
    return false;
  }
  uint32_t overflows() const { return overflows_; }

 private:
  char buf_[kUartLineMax + 1] = {};
  size_t len_ = 0;
  bool discarding_ = false;
  uint32_t overflows_ = 0;
};

}  // namespace reray
