// リレーの再送予約(RELAY_DESIGN §2): ランダムなバックオフ + 優先度 + 再送の抑制。
// 乱数と時刻は呼び出し側から渡す(PC でテストできるように)。キャリアセンスと送信時間の予算確認は、
// pop() で取り出した後に呼び出し側が行う(送れなかったら drop() するか、schedule し直す)。
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "reray/airtime.h"
#include "reray/frame.h"

namespace reray {

struct BackoffRange {
  uint32_t minMs;
  uint32_t maxMs;  // この値は含まない
};

struct RelayConfig {
  // SF7 を想定した初期案。優先度が高いほど短い。SF を上げたら送信時間に合わせて伸ばすこと。
  BackoffRange range[5] = {
      {50, 100},   // Sos
      {80, 160},   // Instruct
      {100, 200},  // Ack
      {150, 300},  // Report
      {200, 400},  // Beacon
  };
  uint8_t suppressK = 2;  // 待っている間に同じフレームの再送を k 回聞いたら、自分の再送をやめる
};

template <size_t N = 8>
class RelayScheduler {
 public:
  struct Job {
    bool used = false;
    uint8_t bytes[kMaxFrameLen];  // ttl を減らした後の、送出するバイト列
    uint8_t len = 0;
    Priority prio = Priority::Report;
    uint16_t src = 0;
    uint16_t seq = 0;
    uint8_t type = 0;
    uint8_t heard = 0;
    uint32_t dueMs = 0;
  };

  explicit RelayScheduler(const RelayConfig& cfg = RelayConfig()) : cfg_(cfg) {}

  static Priority priorityOf(Type t) {
    switch (t) {
      case Type::Sos: return Priority::Sos;
      case Type::Instruct: return Priority::Instruct;
      case Type::Ack: return Priority::Ack;
      case Type::Report: return Priority::Report;
      case Type::Beacon: return Priority::Beacon;
    }
    return Priority::Beacon;
  }

  // 再送を予約する。受信したバイト列に対して ttl を 1 減らして保持する。ttl が 0 なら予約しない。
  // 戻り値: 予約できたか。満杯のときは、優先度が一番低い(同じなら一番遅い)予約を押しのける。
  bool schedule(const uint8_t* rxBytes, size_t len, const Frame& f, uint32_t nowMs, uint32_t rand32) {
    if (f.ttl == 0 || len == 0 || len > kMaxFrameLen) return false;
    const Priority prio = priorityOf(f.type);
    Job* slot = nullptr;
    for (size_t i = 0; i < N; i++)
      if (!j_[i].used) { slot = &j_[i]; break; }
    if (!slot) {
      Job* worst = &j_[0];
      for (size_t i = 1; i < N; i++)
        if ((uint8_t)j_[i].prio > (uint8_t)worst->prio ||
            (j_[i].prio == worst->prio && (int32_t)(j_[i].dueMs - worst->dueMs) > 0))
          worst = &j_[i];
      if ((uint8_t)worst->prio < (uint8_t)prio) return false;  // 新しい方が低優先なら、予約しない
      slot = worst;
    }
    const BackoffRange r = cfg_.range[(uint8_t)prio];
    const uint32_t span = r.maxMs > r.minMs ? r.maxMs - r.minMs : 1;
    slot->used = true;
    memcpy(slot->bytes, rxBytes, len);
    slot->len = (uint8_t)len;
    setTtl(slot->bytes, (uint8_t)(f.ttl - 1));
    slot->prio = prio;
    slot->src = f.src;
    slot->seq = f.seq;
    slot->type = (uint8_t)f.type;
    slot->heard = 0;
    slot->dueMs = nowMs + r.minMs + rand32 % span;
    return true;
  }

  // 同じフレームの再送を他のノードから聞いた。k 回に達したら予約を取り消す。戻り値は取り消したか。
  bool onHeardAgain(uint16_t src, uint16_t seq, uint8_t type) {
    for (size_t i = 0; i < N; i++) {
      Job& j = j_[i];
      if (j.used && j.src == src && j.seq == seq && j.type == type) {
        if (j.heard < 255) j.heard++;
        if (j.heard >= cfg_.suppressK) { j.used = false; return true; }
        return false;
      }
    }
    return false;
  }

  // 時間が来た予約を1件取り出す(一番早いもの。同時なら優先度が高い方)。
  bool pop(uint32_t nowMs, Job& out) {
    Job* best = nullptr;
    for (size_t i = 0; i < N; i++) {
      Job& j = j_[i];
      if (!j.used || (int32_t)(nowMs - j.dueMs) < 0) continue;
      if (!best || (int32_t)(j.dueMs - best->dueMs) < 0 ||
          (j.dueMs == best->dueMs && (uint8_t)j.prio < (uint8_t)best->prio))
        best = &j;
    }
    if (!best) return false;
    out = *best;
    best->used = false;
    return true;
  }

  size_t pending() const {
    size_t n = 0;
    for (size_t i = 0; i < N; i++) n += j_[i].used ? 1 : 0;
    return n;
  }

 private:
  RelayConfig cfg_;
  Job j_[N];
};

}  // namespace reray
