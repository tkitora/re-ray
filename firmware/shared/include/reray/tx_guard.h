// 電波法ガード(CLAUDE.md「最重要ルール」2)。
//  - RERAY_TX_ENABLE が 1 のときだけ送信を許可する。デフォルトは 0。
//  - 送信出力の上限 RERAY_MAX_TX_DBM は 12 を超えられない(static_assert)。
//    要求値が上限を超えたらクランプして、警告フラグを返す。
#pragma once
#include <stdint.h>

#ifndef RERAY_TX_ENABLE
#define RERAY_TX_ENABLE 0
#endif

#ifndef RERAY_MAX_TX_DBM
#define RERAY_MAX_TX_DBM 10
#endif

#ifndef RERAY_TX_DBM
#define RERAY_TX_DBM 10
#endif

namespace reray {

// 証明書: アンテナ端子出力 16mW ≒ 12.04dBm。これを超える設定はビルドさせない。
constexpr int kAbsoluteMaxTxDbm = 12;

static_assert(RERAY_TX_ENABLE == 0 || RERAY_TX_ENABLE == 1, "RERAY_TX_ENABLE must be 0 or 1");
static_assert(RERAY_MAX_TX_DBM <= kAbsoluteMaxTxDbm, "RERAY_MAX_TX_DBM must not exceed 12 dBm");
static_assert(RERAY_MAX_TX_DBM >= 0, "RERAY_MAX_TX_DBM must be >= 0");
static_assert(RERAY_TX_DBM <= RERAY_MAX_TX_DBM, "RERAY_TX_DBM must not exceed RERAY_MAX_TX_DBM");

constexpr bool txEnabled() { return RERAY_TX_ENABLE == 1; }
constexpr int maxTxDbm() { return RERAY_MAX_TX_DBM; }
constexpr int defaultTxDbm() { return RERAY_TX_DBM; }

struct TxPower {
  int dbm;
  bool clamped;  // true のとき、呼び出し側は警告ログを出すこと
};

// 要求された出力を上限に収める。
constexpr TxPower clampTxDbm(int requestedDbm) {
  return requestedDbm > RERAY_MAX_TX_DBM ? TxPower{RERAY_MAX_TX_DBM, true}
         : requestedDbm < 0              ? TxPower{0, true}
                                         : TxPower{requestedDbm, false};
}

}  // namespace reray
