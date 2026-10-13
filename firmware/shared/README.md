# firmware/shared/

ヘッダのみの C++(外部ライブラリ依存なし)。ESP32 と PC で同じコードを使う。

| ヘッダ | 内容 |
|---|---|
| `reray/sha256.h` | SHA-256、HMAC-SHA256 |
| `reray/frame.h` | フレームの符号化・復号、MIC(ttl 除外)、`setTtl` |
| `reray/dedup.h` | `DedupCache`(重複排除、聞いた回数)、`SeqTracker`(seq の回り込み対応) |
| `reray/airtime.h` | LoRa の送信時間、`TxBudget`(送信時間の予算。**既定値は暫定**) |
| `reray/tx_guard.h` | `RERAY_TX_ENABLE`(既定 0)、`RERAY_MAX_TX_DBM`(12 超は static_assert で拒否) |

## テスト

PlatformIO なしで、PC 上で動く(clang または gcc)。

```bash
make -C firmware/shared test        # ユニットテスト(ASan + UBSan)
make -C firmware/shared guard-test  # 電波法ガードがビルドで拒否されることの確認
```

結果は `docs/FINDINGS.md` の S3 を参照。
