// 送信元側の「報告の状態」(ARCHITECTURE §4、RELAY_DESIGN §3)。
//   Sent → (Near | Server)。ACK がなければ待ち時間を延ばして再送(Retry)し、最大回数で Failed(未達)。
//   Near(近傍の ACK / 他ノードの再送を聞いた)になったら再送は止める。Server で完了。
// 再送のたびに seq は変えない(重複排除と ACK の突き合わせのため)。送信時間の予算を守るため、再送は有限。
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace reray {

enum class ReportState : uint8_t { Sent, Near, Server, Retry, Failed };

struct TrackerConfig {
  uint32_t retryMs[3] = {5000, 10000, 20000};  // RELAY_DESIGN §3 の初期案
  uint8_t maxRetries = 3;                      // retryMs の要素数以下
};

struct TrackerEvent {
  enum Kind : uint8_t { None, Resend, GiveUp } kind = None;
  uint16_t seq = 0;
};

template <size_t N = 4>
class ReportTracker {
 public:
  explicit ReportTracker(const TrackerConfig& cfg = TrackerConfig()) : cfg_(cfg) {
    if (cfg_.maxRetries > 3) cfg_.maxRetries = 3;
  }

  // 新しい報告を送り出した。空きがなければ、完了済みの一番古いものを使い、無ければ一番古い未完了を捨てる。
  void start(uint16_t seq, uint32_t nowMs) {
    Slot* s = pickSlot();
    *s = Slot{};
    s->used = true;
    s->seq = seq;
    s->state = ReportState::Sent;
    s->startedMs = nowMs;
    s->nextMs = nowMs + cfg_.retryMs[0];
    s->order = ++counter_;
  }

  // 戻り値は状態が変わったか。Server は Near より強い(Near が後から来ても戻さない)。
  bool onNear(uint16_t seq) {
    Slot* s = find(seq);
    if (!s || (s->state != ReportState::Sent && s->state != ReportState::Retry)) return false;
    s->state = ReportState::Near;
    return true;
  }

  bool onServer(uint16_t seq) {
    Slot* s = find(seq);
    if (!s || s->state == ReportState::Server) return false;
    s->state = ReportState::Server;
    return true;
  }

  // 時間が来た再送や諦めを1件ずつ返す。None が返るまで繰り返し呼ぶ。
  TrackerEvent poll(uint32_t nowMs) {
    for (size_t i = 0; i < N; i++) {
      Slot& s = s_[i];
      if (!s.used || (s.state != ReportState::Sent && s.state != ReportState::Retry)) continue;
      if ((int32_t)(nowMs - s.nextMs) < 0) continue;
      if (s.retries >= cfg_.maxRetries) {
        s.state = ReportState::Failed;
        return {TrackerEvent::GiveUp, s.seq};
      }
      s.state = ReportState::Retry;
      s.retries++;
      s.nextMs = nowMs + (s.retries < cfg_.maxRetries ? cfg_.retryMs[s.retries] : cfg_.retryMs[cfg_.maxRetries - 1]);
      return {TrackerEvent::Resend, s.seq};
    }
    return {};
  }

  bool state(uint16_t seq, ReportState& out) const {
    for (size_t i = 0; i < N; i++)
      if (s_[i].used && s_[i].seq == seq) { out = s_[i].state; return true; }
    return false;
  }

  uint8_t retries(uint16_t seq) const {
    for (size_t i = 0; i < N; i++)
      if (s_[i].used && s_[i].seq == seq) return s_[i].retries;
    return 0;
  }

 private:
  struct Slot {
    bool used = false;
    uint16_t seq = 0;
    ReportState state = ReportState::Sent;
    uint8_t retries = 0;
    uint32_t startedMs = 0;
    uint32_t nextMs = 0;
    uint32_t order = 0;
  };

  static bool finished(const Slot& s) {
    return s.state == ReportState::Server || s.state == ReportState::Failed || s.state == ReportState::Near;
  }

  Slot* find(uint16_t seq) {
    Slot* best = nullptr;
    for (size_t i = 0; i < N; i++)
      if (s_[i].used && s_[i].seq == seq && (!best || s_[i].order > best->order)) best = &s_[i];
    return best;
  }

  Slot* pickSlot() {
    Slot* free = nullptr;
    Slot* oldestFinished = nullptr;
    Slot* oldest = &s_[0];
    for (size_t i = 0; i < N; i++) {
      Slot& s = s_[i];
      if (!s.used) { if (!free) free = &s; continue; }
      if (finished(s) && (!oldestFinished || s.order < oldestFinished->order)) oldestFinished = &s;
      if (s.order < oldest->order) oldest = &s;
    }
    return free ? free : oldestFinished ? oldestFinished : oldest;
  }

  TrackerConfig cfg_;
  Slot s_[N];
  uint32_t counter_ = 0;
};

}  // namespace reray
