// 出口(GATEWAY)の純粋なロジック: LoRa フレーム → MQTT の JSON、サーバー到達 ACK、回線断キュー。
// JSON の形は Drive 共通仕様「02_メッセージ形式」に合わせる。
//   signal: {"v":1,"type":"signal","src":"dev-001","seq":42,"ts":1791849600,"code":2,"via":"lora"}
//   ack   : {"v":1,"type":"ack","src":"dev-002","seq":44,"ts":...,"ack_src":"pc","ack_seq":7}
// 共通仕様にない追加項目(zone、人数、RSSI など)は、`"x":{...}` の1つのオブジェクトにまとめる。
// 共通仕様側で正式に決まるまで、受け取る側は "x" を無視してよい【変更案】。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "reray/frame.h"
#include "reray/node_id.h"

namespace reray {

struct RxInfo {
  int16_t rssi = 0;
  float snr = 0;
  uint32_t rxMs = 0;  // 受信した時刻(millis)
};

constexpr uint8_t kSignalSos = 1;  // Drive の定型信号コード: 1=SOS

// 出口が MQTT に出す signal の JSON。戻り値は文字数。対象外のフレームやバッファ不足は 0。
//  - SOS フレームは code=1 とする。REPORT は flags の定型信号コード(無ければ 0)。
//  - ts は フレームの time(GPS 時刻)。未同期(0)なら fallbackTs。サーバーが受信時刻を正とする想定。
inline size_t signalJson(const Frame& f, const RxInfo& rx, uint32_t fallbackTs, bool withExtras, char* out,
                         size_t cap) {
  if (f.type != Type::Report && f.type != Type::Sos) return 0;
  char id[16];
  if (formatDeviceId(f.src, id, sizeof id) == 0) return 0;
  const unsigned code = f.type == Type::Sos ? kSignalSos : signalCodeOf(f.flags);
  const uint32_t ts = f.time != 0 ? f.time : fallbackTs;
  int n = snprintf(out, cap, "{\"v\":1,\"type\":\"signal\",\"src\":\"%s\",\"seq\":%u,\"ts\":%lu,\"code\":%u,\"via\":\"lora\"",
                   id, (unsigned)f.seq, (unsigned long)ts, code);
  if (n < 0 || (size_t)n >= cap) return 0;
  if (withExtras) {
    int m = snprintf(out + n, cap - (size_t)n,
                     ",\"x\":{\"zone\":%u,\"people\":%u,\"injured\":%u,\"rescue\":%u,\"needs\":%u,"
                     "\"gps\":%s,\"rssi\":%d,\"snr\":%.1f}",
                     (unsigned)f.zone, (unsigned)f.people, (unsigned)f.injured, (unsigned)f.rescue,
                     (unsigned)f.needs, (f.flags & kFlagGpsValid) ? "true" : "false", (int)rx.rssi, (double)rx.snr);
    if (m < 0 || (size_t)m >= cap - (size_t)n) return 0;
    n += m;
  }
  if ((size_t)n + 2 > cap) return 0;
  out[n++] = '}';
  out[n] = '\0';
  return (size_t)n;
}

// LoRa の ACK(指示受領)を MQTT の ack にする。他の ack_kind は LoRa 内部用なので対象外(0を返す)。
inline size_t ackJson(const Frame& f, uint32_t fallbackTs, char* out, size_t cap) {
  if (f.type != Type::Ack || f.ackKind != (uint8_t)AckKind::InstructReceived) return 0;
  char src[16], ref[16];
  if (formatDeviceId(f.src, src, sizeof src) == 0 || formatDeviceId(f.refSrc, ref, sizeof ref) == 0) return 0;
  int n = snprintf(out, cap, "{\"v\":1,\"type\":\"ack\",\"src\":\"%s\",\"seq\":%u,\"ts\":%lu,\"ack_src\":\"%s\",\"ack_seq\":%u}",
                   src, (unsigned)f.seq, (unsigned long)fallbackTs, ref, (unsigned)f.refSeq);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

// MQTT トピック(Drive 共通仕様「03_MQTTトピック」)。例: reray/v1/up/dev-001/signal
inline size_t upTopic(uint16_t src, const char* leaf, char* out, size_t cap) {
  char id[16];
  if (formatDeviceId(src, id, sizeof id) == 0) return 0;
  int n = snprintf(out, cap, "reray/v1/up/%s/%s", id, leaf);
  return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
}

// 「サーバー到達」の ACK(ack_kind=2)を作る。MQTT への publish が成功したときだけ送る。
inline Frame makeServerAck(uint16_t gwId, uint16_t gwSeq, uint8_t ttl, const Frame& ref) {
  Frame a;
  a.type = Type::Ack;
  a.ttl = ttl;
  a.src = gwId;
  a.seq = gwSeq;
  a.refSrc = ref.src;
  a.refSeq = ref.seq;
  a.ackKind = (uint8_t)AckKind::Server;
  return a;
}

// 回線断のあいだ、受信したフレームを溜める。満杯なら、SOS 以外の一番古いものを捨てる
// (SOS だけが残っていれば、一番古い SOS)。捨てた数は dropped() で見られる。
template <size_t N>
class GatewayQueue {
 public:
  struct Item {
    uint8_t bytes[kMaxFrameLen];
    uint8_t len;
    RxInfo rx;
  };

  bool push(const uint8_t* bytes, size_t len, const RxInfo& rx) {
    if (len == 0 || len > kMaxFrameLen) return false;
    if (count_ == N) evict();
    Item& it = items_[(head_ + count_) % N];
    memcpy(it.bytes, bytes, len);
    it.len = (uint8_t)len;
    it.rx = rx;
    count_++;
    return true;
  }

  bool empty() const { return count_ == 0; }
  size_t size() const { return count_; }
  uint32_t dropped() const { return dropped_; }
  const Item& front() const { return items_[head_]; }
  void pop() {
    if (count_ == 0) return;
    head_ = (head_ + 1) % N;
    count_--;
  }

 private:
  static bool isSos(const Item& it) { return ((it.bytes[0] >> 3) & 7) == (uint8_t)Type::Sos; }

  void evict() {
    size_t victim = 0;  // キュー内の位置
    for (size_t i = 0; i < count_; i++) {
      if (!isSos(items_[(head_ + i) % N])) { victim = i; break; }
    }
    // victim より後ろを1つ詰める
    for (size_t i = victim; i + 1 < count_; i++) items_[(head_ + i) % N] = items_[(head_ + i + 1) % N];
    count_--;
    dropped_++;
  }

  Item items_[N];
  size_t head_ = 0;
  size_t count_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace reray
