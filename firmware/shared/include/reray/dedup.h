// 重複排除(RELAY_DESIGN §1)とリプレイ対策(MESSAGE_SPEC §7)。固定長、動的確保なし。
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace reray {

// (src, seq, type) の既知リスト。期限つき。
// observe() の戻り値は「これまでにこのフレームを聞いた回数(今回を含む)」。
// 1 なら初見、2 以上なら重複。再送抑制(同じフレームを k 回聞いたら自分は送らない)にも使う。
template <size_t N>
class DedupCache {
 public:
  explicit DedupCache(uint32_t ttlMs = 600000) : ttlMs_(ttlMs) {}

  uint8_t observe(uint16_t src, uint16_t seq, uint8_t type, uint32_t nowMs) {
    Entry* victim = nullptr;
    for (size_t i = 0; i < N; i++) {
      Entry& e = e_[i];
      if (!e.used) {
        if (!victim) victim = &e;
        continue;
      }
      const bool expired = (uint32_t)(nowMs - e.firstMs) >= ttlMs_;
      if (!expired && e.src == src && e.seq == seq && e.type == type) {
        if (e.count < 255) e.count++;
        return e.count;
      }
      if (expired) {
        e.used = false;
        if (!victim) victim = &e;
      }
    }
    if (!victim) victim = oldest(nowMs);  // 満杯なら一番古いものを追い出す
    *victim = Entry{true, src, seq, type, 1, nowMs};
    return 1;
  }

  // 参照のみ(カウントを増やさない)。
  uint8_t heardCount(uint16_t src, uint16_t seq, uint8_t type, uint32_t nowMs) const {
    for (size_t i = 0; i < N; i++) {
      const Entry& e = e_[i];
      if (e.used && (uint32_t)(nowMs - e.firstMs) < ttlMs_ && e.src == src && e.seq == seq && e.type == type)
        return e.count;
    }
    return 0;
  }

 private:
  struct Entry {
    bool used = false;
    uint16_t src = 0;
    uint16_t seq = 0;
    uint8_t type = 0;
    uint8_t count = 0;
    uint32_t firstMs = 0;
  };

  Entry* oldest(uint32_t nowMs) {
    Entry* o = &e_[0];
    uint32_t oAge = (uint32_t)(nowMs - o->firstMs);
    for (size_t i = 1; i < N; i++) {
      uint32_t age = (uint32_t)(nowMs - e_[i].firstMs);
      if (age > oAge) {
        o = &e_[i];
        oAge = age;
      }
    }
    return o;
  }

  Entry e_[N];
  uint32_t ttlMs_;
};

// 送信元ごとの「最後に見た seq」。u16 の回り込みを考慮して、古い seq を捨てる。
// 同じ seq(再送・重複)は通す。重複の判定は DedupCache の役目。
template <size_t N>
class SeqTracker {
 public:
  bool accept(uint16_t src, uint16_t seq) {
    for (size_t i = 0; i < N; i++) {
      if (used_[i] && src_[i] == src) {
        const int16_t diff = (int16_t)(uint16_t)(seq - last_[i]);
        if (diff < 0) return false;
        last_[i] = seq;
        return true;
      }
    }
    src_[next_] = src;
    last_[next_] = seq;
    used_[next_] = true;
    next_ = (next_ + 1) % N;
    return true;
  }

 private:
  uint16_t src_[N] = {};
  uint16_t last_[N] = {};
  bool used_[N] = {};
  size_t next_ = 0;
};

}  // namespace reray
