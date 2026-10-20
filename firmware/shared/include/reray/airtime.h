// LoRa の送信時間(Semtech の式)と、送信時間の予算管理。
//
// ★ 予算の既定値は暫定。COMPLIANCE.md の確認欄 #7 が埋まるまで、調査結果(COMPLIANCE.md §3 の「調査結果」)より
//    さらに保守的にしてある。主な根拠: 128µs 方式のキャリアセンスでは 1 回 0.4 秒以下・1 時間 360 秒以下、
//    ARIB 英訳では 200ms を超えると休止が送信時間の 10 倍以上(総務省告示の統合版にはこの規定がなく、食い違い)。
//    → 1 回を 200ms 以下に抑えれば 10 倍休止の論点を避けられる(17 バイトなら SF9 までは 165ms で収まる)。
//    規格で確認した値が分かったら、BudgetConfig を差し替える。
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace reray {

// 送信時間(マイクロ秒)。explicit header、CRC あり、IH=0 を前提。
// cr は 1=4/5, 2=4/6, 3=4/7, 4=4/8。
inline uint32_t loraAirtimeUs(uint8_t payloadLen, uint8_t sf, uint32_t bwHz = 125000, uint8_t cr = 1,
                              uint16_t preambleSyms = 8, bool crc = true, bool explicitHeader = true) {
  if (sf < 6 || sf > 12 || bwHz == 0) return 0;
  const uint64_t tsymNs = ((uint64_t)(1u << sf) * 1000000000ull) / bwHz;
  const bool de = tsymNs > 16000000ull;  // Tsym > 16ms で低速データレート最適化
  const int ih = explicitHeader ? 0 : 1;
  int num = 8 * (int)payloadLen - 4 * (int)sf + 28 + (crc ? 16 : 0) - 20 * ih;
  const int den = 4 * ((int)sf - (de ? 2 : 0));
  int blocks = num > 0 ? (num + den - 1) / den : 0;  // ceil
  const int payloadSyms = 8 + blocks * (cr + 4);
  // (preamble + 4.25 + payloadSyms) × Tsym = (4*preamble + 17 + 4*payloadSyms) × Tsym / 4
  const uint64_t quarterSyms = 4ull * preambleSyms + 17 + 4ull * payloadSyms;
  return (uint32_t)((quarterSyms * tsymNs) / 4 / 1000);
}

enum class Priority : uint8_t { Sos = 0, Instruct = 1, Ack = 2, Report = 3, Beacon = 4 };

struct BudgetConfig {
  uint32_t windowMs = 3600000;    // 集計する窓(1時間)
  uint32_t windowLimitMs = 18000; // 窓内の送信時間の上限。暫定(調査した上限 360 秒の 5%。LDC 方式の 36 秒の半分)
  uint32_t singleMaxMs = 200;     // 1回の送信の上限。暫定(上限 400ms の半分。ARIB の「200ms 超は休止 10 倍」も避ける)
  uint32_t minGapMs = 100;        // 送信後の休止。暫定(調査した下限 2ms より十分長い)
  uint8_t reportPercent = 80;     // REPORT は上限のこの割合まで
  uint8_t beaconPercent = 50;     // BEACON は上限のこの割合まで
};

// 送信時間の台帳。canSend() で可否を問い合わせ、実際に送ったら record() する。
// 台帳が満杯で窓内の記録を消せないときは、安全側に倒して送信を拒否する。
template <size_t N = 256>
class TxBudget {
 public:
  explicit TxBudget(const BudgetConfig& cfg = BudgetConfig()) : cfg_(cfg) {}

  bool canSend(Priority prio, uint32_t airtimeUs, uint32_t nowMs) {
    prune(nowMs);
    if (airtimeUs == 0) return false;
    if (airtimeUs / 1000 > cfg_.singleMaxMs) return false;
    if (hasLast_ && (int32_t)(nowMs - lastEndMs_) < (int32_t)cfg_.minGapMs) return false;
    if (count_ >= N) return false;
    const uint64_t after = (uint64_t)usedUs_ + airtimeUs;
    return after <= limitUs(prio);
  }

  void record(uint32_t airtimeUs, uint32_t nowMs) {
    prune(nowMs);
    if (count_ >= N) return;  // canSend が拒否するはずなので、通常は来ない
    const size_t idx = (head_ + count_) % N;
    startMs_[idx] = nowMs;
    us_[idx] = airtimeUs;
    count_++;
    usedUs_ += airtimeUs;
    lastEndMs_ = nowMs + airtimeUs / 1000;
    hasLast_ = true;
  }

  uint32_t usedUs(uint32_t nowMs) {
    prune(nowMs);
    return usedUs_;
  }

  uint32_t limitUs() const { return cfg_.windowLimitMs * 1000; }

 private:
  uint64_t limitUs(Priority p) const {
    const uint64_t full = (uint64_t)cfg_.windowLimitMs * 1000;
    switch (p) {
      case Priority::Report: return full * cfg_.reportPercent / 100;
      case Priority::Beacon: return full * cfg_.beaconPercent / 100;
      default: return full;
    }
  }

  void prune(uint32_t nowMs) {
    while (count_ > 0 && (uint32_t)(nowMs - startMs_[head_]) >= cfg_.windowMs) {
      usedUs_ -= us_[head_];
      head_ = (head_ + 1) % N;
      count_--;
    }
  }

  BudgetConfig cfg_;
  uint32_t startMs_[N] = {};
  uint32_t us_[N] = {};
  size_t head_ = 0;
  size_t count_ = 0;
  uint32_t usedUs_ = 0;
  uint32_t lastEndMs_ = 0;
  bool hasLast_ = false;
};

}  // namespace reray
