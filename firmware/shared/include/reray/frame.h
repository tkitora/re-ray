// LoRa フレームの符号化・復号・署名。仕様は docs/MESSAGE_SPEC.md。
// ttl は再送のたびに変わるので、MIC の計算では 0 として扱う(MESSAGE_SPEC §6)。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "reray/sha256.h"

namespace reray {

// ver は MESSAGE_SPEC に値の指定がない。0 は全ゼロの誤受信と区別がつかないため 1 を仮置き。
constexpr uint8_t kFrameVersion = 1;
constexpr size_t kMicLen = 4;
constexpr size_t kMaxFrameLen = 19;
constexpr uint8_t kMaxTtl = 7;  // ttl は 3bit

enum class Type : uint8_t { Report = 0, Sos = 1, Instruct = 2, Ack = 3, Beacon = 4 };

enum class AckKind : uint8_t { Near = 1, Server = 2, InstructReceived = 3 };

enum class DecodeStatus : uint8_t {
  Ok = 0,
  TooShort,
  BadVersion,
  BadType,
  BadLength,
  BadMic,
};

// フラグ(REPORT/SOS の flags)
constexpr uint8_t kFlagSos = 1 << 0;
constexpr uint8_t kFlagConfirmed = 1 << 1;
constexpr uint8_t kFlagGpsValid = 1 << 2;

struct Frame {
  Type type = Type::Report;
  uint8_t ttl = 0;
  uint16_t src = 0;
  uint16_t seq = 0;

  // REPORT / SOS
  uint8_t zone = 0;
  uint8_t people = 0;   // 4bit
  uint8_t injured = 0;  // 4bit
  uint8_t rescue = 0;   // 4bit
  uint8_t needs = 0;    // 4bit
  uint8_t flags = 0;

  // REPORT / SOS / INSTRUCT 共通
  uint32_t time = 0;

  // INSTRUCT
  uint8_t targetKind = 0;
  uint16_t target = 0;
  uint8_t code = 0;
  uint8_t param = 0;

  // ACK
  uint16_t refSrc = 0;
  uint16_t refSeq = 0;
  uint8_t ackKind = 0;

  // BEACON
  int32_t lat = 0;  // 度 × 1e5
  int32_t lon = 0;
  uint8_t batt = 0;
  uint8_t role = 0;
};

inline size_t frameLength(Type t) {
  switch (t) {
    case Type::Report:
    case Type::Sos: return 17;
    case Type::Instruct: return 18;
    case Type::Ack: return 14;
    case Type::Beacon: return 19;
  }
  return 0;
}

namespace detail {

inline void put16(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}
inline void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}
inline uint16_t get16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t get32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// MIC = HMAC-SHA256(key, フレーム(MIC除く、ttl=0))の先頭4バイト
inline void computeMic(const uint8_t* key, size_t keyLen, const uint8_t* frame, size_t bodyLen,
                       uint8_t mic[kMicLen]) {
  uint8_t tmp[kMaxFrameLen];
  memcpy(tmp, frame, bodyLen);
  tmp[0] &= 0xF8;  // 下位3bit(ttl)をゼロにする
  uint8_t digest[Sha256::kDigestLen];
  hmacSha256(key, keyLen, tmp, bodyLen, digest);
  memcpy(mic, digest, kMicLen);
}

}  // namespace detail

// フレームを out に符号化し、MIC を付ける。戻り値はバイト数。
// フィールドが範囲外(ttl > 7、4bit 項目 > 15)なら 0 を返す。
inline size_t encodeFrame(const Frame& f, const uint8_t* key, size_t keyLen, uint8_t out[kMaxFrameLen]) {
  using namespace detail;
  const size_t len = frameLength(f.type);
  if (len == 0 || f.ttl > kMaxTtl) return 0;
  memset(out, 0, kMaxFrameLen);
  out[0] = (uint8_t)((kFrameVersion << 6) | ((uint8_t)f.type << 3) | f.ttl);
  put16(out + 1, f.src);
  put16(out + 3, f.seq);
  switch (f.type) {
    case Type::Report:
    case Type::Sos:
      if (f.people > 15 || f.injured > 15 || f.rescue > 15 || f.needs > 15) return 0;
      out[5] = f.zone;
      out[6] = (uint8_t)((f.people << 4) | f.injured);
      out[7] = (uint8_t)((f.rescue << 4) | f.needs);
      out[8] = f.flags;
      put32(out + 9, f.time);
      break;
    case Type::Instruct:
      out[5] = f.targetKind;
      put16(out + 6, f.target);
      out[8] = f.code;
      out[9] = f.param;
      put32(out + 10, f.time);
      break;
    case Type::Ack:
      put16(out + 5, f.refSrc);
      put16(out + 7, f.refSeq);
      out[9] = f.ackKind;
      break;
    case Type::Beacon:
      put32(out + 5, (uint32_t)f.lat);
      put32(out + 9, (uint32_t)f.lon);
      out[13] = f.batt;
      out[14] = f.role;
      break;
  }
  computeMic(key, keyLen, out, len - kMicLen, out + len - kMicLen);
  return len;
}

// 受信バイト列を検証して復号する。署名不正なら BadMic で、f は更新しない。
inline DecodeStatus decodeFrame(const uint8_t* buf, size_t len, const uint8_t* key, size_t keyLen, Frame& f) {
  using namespace detail;
  if (len < 5) return DecodeStatus::TooShort;
  if ((buf[0] >> 6) != kFrameVersion) return DecodeStatus::BadVersion;
  const uint8_t t = (buf[0] >> 3) & 0x07;
  if (t > (uint8_t)Type::Beacon) return DecodeStatus::BadType;
  const Type type = (Type)t;
  const size_t expect = frameLength(type);
  if (len != expect) return DecodeStatus::BadLength;

  uint8_t mic[kMicLen];
  computeMic(key, keyLen, buf, len - kMicLen, mic);
  uint8_t diff = 0;  // 定数時間で比較
  for (size_t i = 0; i < kMicLen; i++) diff |= (uint8_t)(mic[i] ^ buf[len - kMicLen + i]);
  if (diff != 0) return DecodeStatus::BadMic;

  Frame r;
  r.type = type;
  r.ttl = buf[0] & 0x07;
  r.src = get16(buf + 1);
  r.seq = get16(buf + 3);
  switch (type) {
    case Type::Report:
    case Type::Sos:
      r.zone = buf[5];
      r.people = buf[6] >> 4;
      r.injured = buf[6] & 0x0F;
      r.rescue = buf[7] >> 4;
      r.needs = buf[7] & 0x0F;
      r.flags = buf[8];
      r.time = get32(buf + 9);
      break;
    case Type::Instruct:
      r.targetKind = buf[5];
      r.target = get16(buf + 6);
      r.code = buf[8];
      r.param = buf[9];
      r.time = get32(buf + 10);
      break;
    case Type::Ack:
      r.refSrc = get16(buf + 5);
      r.refSeq = get16(buf + 7);
      r.ackKind = buf[9];
      break;
    case Type::Beacon:
      r.lat = (int32_t)get32(buf + 5);
      r.lon = (int32_t)get32(buf + 9);
      r.batt = buf[13];
      r.role = buf[14];
      break;
  }
  f = r;
  return DecodeStatus::Ok;
}

// 再送用: 受信したバイト列の ttl だけを書き換える(MIC は ttl を含まないので有効なまま)。
inline void setTtl(uint8_t* buf, uint8_t ttl) { buf[0] = (uint8_t)((buf[0] & 0xF8) | (ttl & 0x07)); }

}  // namespace reray
